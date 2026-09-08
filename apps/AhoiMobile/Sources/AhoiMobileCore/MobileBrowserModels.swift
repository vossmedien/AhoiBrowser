import Foundation
import AhoiCloudKitSpike

public enum MobileBrowsingMode: String, Codable, CaseIterable, Sendable {
    case normal
    case privateBrowsing
}

/// Local projection state, never a replicated field or an authority grant.
public enum MobileSharedTabBindingState: String, Codable, Sendable {
    case unbound
    case current
    case deferred
    case deleted
}

public struct MobileTabRecord: Codable, Equatable, Identifiable, Sendable {
    public static let maximumFaviconDataBytes = 128 * 1_024
    public static let maximumCustomTitleCharacters = 160
    public static let maximumTitleUTF8Bytes = 1_024
    public static let maximumURLUTF8Bytes = 16 * 1_024

    /// Stable local runtime identity. It is never a Presence record ID.
    public let id: UUID
    public var workspaceID: WorkspaceID?
    /// Nil for private tabs and dormant shared mirrors not opened locally.
    public internal(set) var presenceID: TabID?
    /// Global page identity, distinct from both runtime and Presence identity.
    public internal(set) var treeNodeID: TreeNodeID?
    /// Transport-safe logical metadata, independent of the loaded runtime URL.
    public internal(set) var sharedTarget: SharedTabTarget?
    public internal(set) var sharedBindingState: MobileSharedTabBindingState
    /// Explicit user-created empty tabs participate; automatic placeholders do not.
    public internal(set) var participatesInSharedTabs: Bool
    /// A page-independent title chosen explicitly by the user. Keeping it
    /// separate prevents later WebKit metadata from erasing that choice.
    public var customTitle: String?
    public var title: String
    /// Local navigation/recovery URL. Passive sync must never overwrite it.
    public var url: String?
    public var createdAt: Date
    public var lastActiveAt: Date
    public var isSaved: Bool
    public var mode: MobileBrowsingMode {
        didSet {
            if mode == .privateBrowsing { clearPrivateSharedMetadata() }
        }
    }
    /// Bounded local image bytes fetched inside the owning tab's WebKit
    /// context. No remote favicon URL is persisted or synchronized.
    public var faviconData: Data?
    /// Local-only, contrast-filtered website accent for the browser chrome.
    /// It is deliberately absent from Ahoi sync wire records.
    public var websiteTintARGB: UInt32?

    public init(
        id: UUID = UUID(),
        workspaceID: WorkspaceID? = nil,
        treeNodeID: TreeNodeID? = nil,
        customTitle: String? = nil,
        title: String = "",
        url: String? = nil,
        createdAt: Date = Date(),
        lastActiveAt: Date = Date(),
        isSaved: Bool = false,
        mode: MobileBrowsingMode = .normal,
        faviconData: Data? = nil,
        websiteTintARGB: UInt32? = nil,
        presenceID: TabID? = TabID(),
        sharedTarget: SharedTabTarget? = nil,
        sharedBindingState: MobileSharedTabBindingState? = nil,
        participatesInSharedTabs: Bool = true
    ) {
        self.id = id
        self.workspaceID = workspaceID
        self.presenceID = mode == .normal ? presenceID : nil
        self.treeNodeID = mode == .normal ? treeNodeID : nil
        self.sharedTarget = mode == .normal ? sharedTarget : nil
        self.sharedBindingState = sharedBindingState ?? (treeNodeID == nil ? .unbound : .deferred)
        self.participatesInSharedTabs = mode == .normal && participatesInSharedTabs
        self.customTitle = Self.normalizedCustomTitle(customTitle)
        self.title = Self.normalizedTitle(title)
        self.url = Self.normalizedURLString(url)
        self.createdAt = createdAt
        self.lastActiveAt = lastActiveAt
        self.isSaved = isSaved
        self.mode = mode
        self.faviconData = faviconData.flatMap { data in
            data.count <= Self.maximumFaviconDataBytes ? data : nil
        }
        self.websiteTintARGB = websiteTintARGB
        // Only explicit local creation derives a target from its local input.
        // A restored/bound row never guesses its logical target from WebKit.
        if self.sharedTarget == nil, treeNodeID == nil, sharedBindingState == nil,
           self.participatesInSharedTabs {
            if let safeURL = self.url {
                self.sharedTarget = try? SharedTabTarget(kind: .web, url: safeURL)
            } else if url == nil, !isSaved {
                self.sharedTarget = try? SharedTabTarget(kind: .newTab, url: "")
            }
        }
        sanitizeSharedMetadata()
    }

    private enum CodingKeys: String, CodingKey {
        case id, workspaceID, presenceID, treeNodeID, sharedTarget, sharedBindingState
        case participatesInSharedTabs, customTitle, title, url, createdAt, lastActiveAt
        case isSaved, mode, faviconData, websiteTintARGB
    }

    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        var invalidBinding = false
        let workspace = Self.decodeBinding(WorkspaceID.self, from: values,
                                           key: .workspaceID, invalid: &invalidBinding)
        let presence = Self.decodeBinding(TabID.self, from: values,
                                          key: .presenceID, invalid: &invalidBinding)
        let node = Self.decodeBinding(TreeNodeID.self, from: values,
                                      key: .treeNodeID, invalid: &invalidBinding)
        let target = Self.decodeBinding(SharedTabTarget.self, from: values,
                                        key: .sharedTarget, invalid: &invalidBinding)
        let state = Self.decodeBinding(MobileSharedTabBindingState.self, from: values,
                                       key: .sharedBindingState, invalid: &invalidBinding)
        let participates = Self.decodeBinding(Bool.self, from: values,
                                              key: .participatesInSharedTabs, invalid: &invalidBinding)
        self.init(
            id: try values.decode(UUID.self, forKey: .id),
            workspaceID: workspace,
            treeNodeID: node,
            customTitle: try values.decodeIfPresent(String.self, forKey: .customTitle),
            title: try values.decode(String.self, forKey: .title),
            url: try values.decodeIfPresent(String.self, forKey: .url),
            createdAt: try values.decode(Date.self, forKey: .createdAt),
            lastActiveAt: try values.decode(Date.self, forKey: .lastActiveAt),
            isSaved: try values.decode(Bool.self, forKey: .isSaved),
            mode: try values.decode(MobileBrowsingMode.self, forKey: .mode),
            faviconData: try values.decodeIfPresent(Data.self, forKey: .faviconData),
            websiteTintARGB: try values.decodeIfPresent(UInt32.self, forKey: .websiteTintARGB),
            presenceID: presence,
            sharedTarget: target,
            sharedBindingState: state == .current ? .deferred : (state ?? .deferred),
            participatesInSharedTabs: participates ?? false
        )
        // Restored current bindings need fresh domain readback. Damaged/missing
        // metadata never discards work or allocates replacement shared identities.
        if invalidBinding, mode == .normal { sharedBindingState = .deferred }
    }

    private static func decodeBinding<T: Decodable>(
        _ type: T.Type, from values: KeyedDecodingContainer<CodingKeys>,
        key: CodingKeys, invalid: inout Bool
    ) -> T? {
        do { return try values.decodeIfPresent(type, forKey: key) }
        catch { invalid = true; return nil }
    }

    var hasDistinctSharedIdentities: Bool {
        var identities = [id]
        if let presenceID { identities.append(presenceID.rawValue) }
        if let treeNodeID { identities.append(treeNodeID.rawValue) }
        return mode == .normal && identities.allSatisfy(Self.isNonzeroUUID) &&
            Set(identities).count == identities.count
    }

    /// An unbound local tab may be submitted for atomic page creation. Presence
    /// publication itself additionally requires `canPublishSharedPresence`.
    public var canPublishSharedTab: Bool {
        guard participatesInSharedTabs, presenceID != nil, hasDistinctSharedIdentities,
              workspaceID.map({ Self.isNonzeroUUID($0.rawValue) }) ?? true,
              let sharedTarget,
              (try? sharedTarget.validatePage(isTemporary: !isSaved)) != nil else { return false }
        return (sharedBindingState == .unbound && treeNodeID == nil) ||
            (sharedBindingState == .current && treeNodeID != nil)
    }

    public var canPublishSharedPresence: Bool {
        canPublishSharedTab && sharedBindingState == .current && treeNodeID != nil
    }

    mutating func sanitizeSharedMetadata() {
        guard mode == .normal else { clearPrivateSharedMetadata(); return }
        if let sharedTarget, (try? sharedTarget.validatePage(isTemporary: !isSaved)) == nil {
            self.sharedTarget = nil
            sharedBindingState = .deferred
        }
        if !hasDistinctSharedIdentities || workspaceID.map({ !Self.isNonzeroUUID($0.rawValue) }) == true ||
            (sharedBindingState == .current && (treeNodeID == nil || sharedTarget == nil)) ||
            (sharedBindingState == .unbound && treeNodeID != nil) {
            sharedBindingState = .deferred
        }
    }

    private mutating func clearPrivateSharedMetadata() {
        presenceID = nil
        treeNodeID = nil
        sharedTarget = nil
        sharedBindingState = .unbound
        participatesInSharedTabs = false
    }

    static func isNonzeroUUID(_ id: UUID) -> Bool {
        id != UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    }

    public static func normalizedTitle(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return utf8Prefix(trimmed, maximumBytes: maximumTitleUTF8Bytes)
    }

    public static func normalizedCustomTitle(_ value: String?) -> String? {
        guard let value else { return nil }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        let normalized = normalizedTitle(
            String(trimmed.prefix(maximumCustomTitleCharacters))
        )
        return normalized.isEmpty ? nil : normalized
    }

    public static func normalizedURLString(_ value: String?) -> String? {
        guard let value,
              value.utf8.count <= maximumURLUTF8Bytes,
              let url = URL(string: value),
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else {
            return nil
        }
        return url.absoluteString
    }

    private static func utf8Prefix(_ value: String, maximumBytes: Int) -> String {
        guard value.utf8.count > maximumBytes else { return value }
        var result = ""
        result.reserveCapacity(maximumBytes)
        var usedBytes = 0
        for character in value {
            let characterBytes = String(character).utf8.count
            guard usedBytes + characterBytes <= maximumBytes else { break }
            result.append(character)
            usedBytes += characterBytes
        }
        return result
    }

    public var effectiveTitle: String {
        customTitle ?? title
    }

    public var displayTitle: String {
        let trimmedTitle = effectiveTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedTitle.isEmpty { return trimmedTitle }
        if let url, let host = URL(string: url)?.host(), !host.isEmpty { return host }
        return CompanionL10n.string("browser.new_tab", fallback: "New tab")
    }
}

public struct MobileBrowserSessionSnapshot: Codable, Equatable, Sendable {
    public static let currentSchemaVersion = 1
    public static let maximumTotalFaviconBytes = 8 * 1_024 * 1_024

    public var schemaVersion: Int
    public var tabs: [MobileTabRecord]
    public var selectedTabID: UUID?

    public init(
        schemaVersion: Int = Self.currentSchemaVersion,
        tabs: [MobileTabRecord] = [],
        selectedTabID: UUID? = nil
    ) {
        self.schemaVersion = schemaVersion
        var seenIDs = Set<UUID>()
        let normal = tabs.filter { $0.mode == .normal }
        let runtimeIDs = Set(normal.map(\.id))
        let nodeCounts = Dictionary(grouping: normal.compactMap(\.treeNodeID), by: { $0 })
        let presenceCounts = Dictionary(grouping: normal.compactMap(\.presenceID), by: { $0 })
        let nodeUUIDs = Set(nodeCounts.keys.map(\.rawValue))
        let presenceUUIDs = Set(presenceCounts.keys.map(\.rawValue))
        var retainedFaviconBytes = 0
        self.tabs = tabs.compactMap { tab in
            guard tab.mode == .normal, seenIDs.insert(tab.id).inserted else {
                return nil
            }
            var sanitized = tab
            sanitized.sanitizeSharedMetadata()
            let invalidNode = sanitized.treeNodeID.map {
                nodeCounts[$0]?.count != 1 || runtimeIDs.contains($0.rawValue) ||
                    presenceUUIDs.contains($0.rawValue)
            } ?? false
            let invalidPresence = sanitized.presenceID.map {
                presenceCounts[$0]?.count != 1 || runtimeIDs.contains($0.rawValue) ||
                    nodeUUIDs.contains($0.rawValue)
            } ?? false
            if invalidNode || invalidPresence {
                // Keep every distinct runtime and its work. Conflicting bindings
                // remain visible for recovery but cannot publish on restart.
                sanitized.sharedBindingState = .deferred
            }
            sanitized.customTitle = MobileTabRecord.normalizedCustomTitle(
                sanitized.customTitle
            )
            sanitized.title = MobileTabRecord.normalizedTitle(sanitized.title)
            sanitized.url = MobileTabRecord.normalizedURLString(sanitized.url)
            if let faviconData = sanitized.faviconData,
               faviconData.count > MobileTabRecord.maximumFaviconDataBytes ||
                retainedFaviconBytes + faviconData.count > Self.maximumTotalFaviconBytes {
                    sanitized.faviconData = nil
            } else if let faviconData = sanitized.faviconData {
                retainedFaviconBytes += faviconData.count
            }
            return sanitized
        }
        self.selectedTabID = self.tabs.contains { $0.id == selectedTabID }
            ? selectedTabID
            : self.tabs.first?.id
    }

    public static let empty = Self()
}

public enum MobileBrowserInputError: Error, Equatable, Sendable {
    case empty
    case unsafeURL
    case invalidSearchTemplate
}

public enum MobilePageFailureKind: String, Equatable, Sendable {
    case offline
    case dnsLookupFailed
    case timedOut
    case transportSecurity
    case httpClientError
    case httpServerError
    case webContentTerminated
    case invalidURL
    case failed
}

public struct MobileNavigationObservation: Equatable, Sendable {
    public let tabID: UUID
    public let title: String
    public let url: URL
    public let tab: MobileTabRecord

    public init(tabID: UUID, title: String, url: URL, tab: MobileTabRecord) {
        self.tabID = tabID
        self.title = title
        self.url = url
        self.tab = tab
    }
}

public enum MobileBrowserInputRouter {
    public static let defaultSearchTemplate = "https://duckduckgo.com/?q=%@"

    public static func resolve(
        _ input: String,
        searchTemplate: String = defaultSearchTemplate
    ) throws -> URL {
        let value = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !value.isEmpty else { throw MobileBrowserInputError.empty }

        if let explicit = URL(string: value), explicit.scheme != nil {
            return try validateWebURL(explicit)
        }

        if !value.contains(where: { $0.isWhitespace }),
           (value.contains(".") || value.caseInsensitiveCompare("localhost") == .orderedSame),
           let inferred = URL(string: "https://\(value)") {
            return try validateWebURL(inferred)
        }

        guard searchTemplate.contains("%@") else {
            throw MobileBrowserInputError.invalidSearchTemplate
        }
        let allowed = CharacterSet.urlQueryAllowed.subtracting(CharacterSet(charactersIn: "+&="))
        guard let encoded = value.addingPercentEncoding(withAllowedCharacters: allowed),
              let searchURL = URL(string: searchTemplate.replacingOccurrences(of: "%@", with: encoded)) else {
            throw MobileBrowserInputError.invalidSearchTemplate
        }
        return try validateWebURL(searchURL)
    }

    public static func validateWebURL(_ url: URL) throws -> URL {
        guard url.absoluteString.utf8.count <= MobileTabRecord.maximumURLUTF8Bytes else {
            throw MobileBrowserInputError.unsafeURL
        }
        guard let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              let scheme = components.scheme?.lowercased(),
              scheme == "https" || scheme == "http",
              components.host?.isEmpty == false,
              components.user == nil,
              components.password == nil else {
            throw MobileBrowserInputError.unsafeURL
        }
        return url
    }
}

public protocol MobileBrowserSessionStoring: Sendable {
    func load() async throws -> MobileBrowserSessionSnapshot
    func save(_ snapshot: MobileBrowserSessionSnapshot) async throws
}

public actor InMemoryMobileBrowserSessionStore: MobileBrowserSessionStoring {
    private var snapshot: MobileBrowserSessionSnapshot

    public init(snapshot: MobileBrowserSessionSnapshot = .empty) {
        self.snapshot = MobileBrowserSessionSnapshot(
            schemaVersion: snapshot.schemaVersion,
            tabs: snapshot.tabs,
            selectedTabID: snapshot.selectedTabID
        )
    }

    public func load() async throws -> MobileBrowserSessionSnapshot {
        MobileBrowserSessionSnapshot(
            schemaVersion: snapshot.schemaVersion,
            tabs: snapshot.tabs,
            selectedTabID: snapshot.selectedTabID
        )
    }

    public func save(_ snapshot: MobileBrowserSessionSnapshot) async throws {
        self.snapshot = MobileBrowserSessionSnapshot(
            schemaVersion: snapshot.schemaVersion,
            tabs: snapshot.tabs,
            selectedTabID: snapshot.selectedTabID
        )
    }
}

public enum MobileBrowserSessionStoreError: Error, Equatable, Sendable {
    case unsupportedSchema
    case invalidSnapshot
}

public actor FileMobileBrowserSessionStore: MobileBrowserSessionStoring {
    private static let maximumSessionBytes: UInt64 = 64 * 1_024 * 1_024
    private let fileURL: URL
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(fileURL: URL) {
        self.fileURL = fileURL
        self.encoder = JSONEncoder()
        self.encoder.outputFormatting = [.sortedKeys]
        self.encoder.dateEncodingStrategy = .millisecondsSince1970
        self.decoder = JSONDecoder()
        self.decoder.dateDecodingStrategy = .millisecondsSince1970
    }

    public func load() async throws -> MobileBrowserSessionSnapshot {
        guard FileManager.default.fileExists(atPath: fileURL.path) else { return .empty }
        let attributes = try FileManager.default.attributesOfItem(atPath: fileURL.path)
        guard let fileSize = attributes[.size] as? NSNumber,
              fileSize.uint64Value <= Self.maximumSessionBytes else {
            throw MobileBrowserSessionStoreError.invalidSnapshot
        }
        let data = try Data(contentsOf: fileURL)
        let snapshot: MobileBrowserSessionSnapshot
        do {
            snapshot = try decoder.decode(MobileBrowserSessionSnapshot.self, from: data)
        } catch {
            throw MobileBrowserSessionStoreError.invalidSnapshot
        }
        guard snapshot.schemaVersion == MobileBrowserSessionSnapshot.currentSchemaVersion else {
            throw MobileBrowserSessionStoreError.unsupportedSchema
        }
        guard snapshot.tabs.allSatisfy({ $0.mode == .normal }) else {
            throw MobileBrowserSessionStoreError.invalidSnapshot
        }
        // Decoding a Codable struct bypasses its memberwise initializer. Run
        // the decoded value through the same normalization boundary so a
        // corrupt file cannot restore duplicate tab identities.
        return MobileBrowserSessionSnapshot(
            schemaVersion: snapshot.schemaVersion,
            tabs: snapshot.tabs,
            selectedTabID: snapshot.selectedTabID
        )
    }

    public func save(_ snapshot: MobileBrowserSessionSnapshot) async throws {
        let persistent = MobileBrowserSessionSnapshot(
            tabs: snapshot.tabs,
            selectedTabID: snapshot.selectedTabID
        )
        let directory = fileURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        let data = try encoder.encode(persistent)
        guard UInt64(data.count) <= Self.maximumSessionBytes else {
            throw MobileBrowserSessionStoreError.invalidSnapshot
        }
        try data.write(to: fileURL, options: [.atomic])
    }
}
