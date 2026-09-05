import Foundation
import AhoiCloudKitSpike

public enum MobileBrowsingMode: String, Codable, CaseIterable, Sendable {
    case normal
    case privateBrowsing
}

public struct MobileTabRecord: Codable, Equatable, Identifiable, Sendable {
    public static let maximumFaviconDataBytes = 128 * 1_024
    public static let maximumCustomTitleCharacters = 160
    public static let maximumTitleUTF8Bytes = 1_024
    public static let maximumURLUTF8Bytes = 16 * 1_024

    public let id: UUID
    public var workspaceID: WorkspaceID?
    /// Global page identity; the local runtime UUID remains a distinct presence.
    public internal(set) var treeNodeID: TreeNodeID?
    /// A page-independent title chosen explicitly by the user. Keeping it
    /// separate prevents later WebKit metadata from erasing that choice.
    public var customTitle: String?
    public var title: String
    public var url: String?
    public var createdAt: Date
    public var lastActiveAt: Date
    public var isSaved: Bool
    public var mode: MobileBrowsingMode
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
        websiteTintARGB: UInt32? = nil
    ) {
        self.id = id
        self.workspaceID = workspaceID
        self.treeNodeID = mode == .normal ? treeNodeID : nil
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
        var seenNodeIDs = Set<TreeNodeID>()
        var retainedFaviconBytes = 0
        self.tabs = tabs.compactMap { tab in
            guard tab.mode == .normal, seenIDs.insert(tab.id).inserted else {
                return nil
            }
            var sanitized = tab
            if let nodeID = sanitized.treeNodeID,
               nodeID.rawValue == sanitized.id || !seenNodeIDs.insert(nodeID).inserted {
                // Preserve the user's local tab, but never restore two runtime
                // authorities for one shared node or a colliding record UUID.
                sanitized.treeNodeID = nil
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
