import Foundation
import AhoiCloudKitSpike

public enum MobileBrowsingMode: String, Codable, CaseIterable, Sendable {
    case normal
    case privateBrowsing
}

public struct MobileTabRecord: Codable, Equatable, Identifiable, Sendable {
    public let id: UUID
    public var workspaceID: WorkspaceID?
    public var title: String
    public var url: String?
    public var createdAt: Date
    public var lastActiveAt: Date
    public var isSaved: Bool
    public var mode: MobileBrowsingMode
    /// Local-only favicon candidate reported by the loaded document.
    public var faviconURL: String?
    /// Local-only, contrast-filtered website accent for the browser chrome.
    /// It is deliberately absent from Ahoi sync wire records.
    public var websiteTintARGB: UInt32?

    public init(
        id: UUID = UUID(),
        workspaceID: WorkspaceID? = nil,
        title: String = "",
        url: String? = nil,
        createdAt: Date = Date(),
        lastActiveAt: Date = Date(),
        isSaved: Bool = false,
        mode: MobileBrowsingMode = .normal,
        faviconURL: String? = nil,
        websiteTintARGB: UInt32? = nil
    ) {
        self.id = id
        self.workspaceID = workspaceID
        self.title = title
        self.url = url
        self.createdAt = createdAt
        self.lastActiveAt = lastActiveAt
        self.isSaved = isSaved
        self.mode = mode
        self.faviconURL = faviconURL
        self.websiteTintARGB = websiteTintARGB
    }

    public var displayTitle: String {
        let trimmedTitle = title.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedTitle.isEmpty { return trimmedTitle }
        if let url, let host = URL(string: url)?.host(), !host.isEmpty { return host }
        return CompanionL10n.string("browser.new_tab", fallback: "New tab")
    }
}

public struct MobileBrowserSessionSnapshot: Codable, Equatable, Sendable {
    public static let currentSchemaVersion = 1

    public var schemaVersion: Int
    public var tabs: [MobileTabRecord]
    public var selectedTabID: UUID?

    public init(
        schemaVersion: Int = Self.currentSchemaVersion,
        tabs: [MobileTabRecord] = [],
        selectedTabID: UUID? = nil
    ) {
        self.schemaVersion = schemaVersion
        self.tabs = tabs.filter { $0.mode == .normal }
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
    case timedOut
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
        self.snapshot = snapshot
    }

    public func load() async throws -> MobileBrowserSessionSnapshot { snapshot }

    public func save(_ snapshot: MobileBrowserSessionSnapshot) async throws {
        self.snapshot = snapshot
    }
}

public enum MobileBrowserSessionStoreError: Error, Equatable, Sendable {
    case unsupportedSchema
    case invalidSnapshot
}

public actor FileMobileBrowserSessionStore: MobileBrowserSessionStoring {
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
        let data = try Data(contentsOf: fileURL)
        let snapshot = try decoder.decode(MobileBrowserSessionSnapshot.self, from: data)
        guard snapshot.schemaVersion == MobileBrowserSessionSnapshot.currentSchemaVersion else {
            throw MobileBrowserSessionStoreError.unsupportedSchema
        }
        guard snapshot.tabs.allSatisfy({ $0.mode == .normal }) else {
            throw MobileBrowserSessionStoreError.invalidSnapshot
        }
        return snapshot
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
        try data.write(to: fileURL, options: [.atomic])
    }
}
