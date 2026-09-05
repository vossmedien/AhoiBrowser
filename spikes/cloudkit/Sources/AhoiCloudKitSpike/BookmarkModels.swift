import Foundation

public enum BookmarkKind: Int, Codable, CaseIterable, Sendable {
    case folder = 0
    case url = 1
}

public enum BookmarkRoot: Int, Codable, CaseIterable, Sendable {
    case bar = 0
    case other = 1
    case mobile = 2
}

public enum BookmarkModelError: Error, Equatable, Sendable {
    case invalidIdentity
    case invalidLocation
    case invalidSortKey
    case invalidTitle
    case invalidURL
    case invalidCreatedAt
    case invalidVersion
    case invalidFieldVersions
    case invalidTombstone
}

/// One logical bookmark identity independent from native Chromium/WebKit node
/// handles. Location consists atomically of root-or-parent plus `sortKey`.
public struct BookmarkRecord: Codable, Hashable, Sendable, Identifiable {
    public static let syncFields: Set<String> = [
        "location", "kind", "title", "url", "created_at", "tombstone",
    ]

    public let bookmarkID: BookmarkID
    public var kind: BookmarkKind
    public var rootKind: BookmarkRoot?
    public var parentID: BookmarkID?
    public var sortKey: String
    public var title: String
    /// Native URL metadata. Activation remains a platform-policy decision.
    public var url: String
    /// Positive microseconds since the Windows epoch. Keeping the wire value as
    /// an integer preserves pre-1970 native creation dates without Date/Double
    /// rounding or an unsigned Unix-epoch conversion.
    public var createdAt: Int64
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        bookmarkID: BookmarkID = BookmarkID(),
        kind: BookmarkKind,
        rootKind: BookmarkRoot? = nil,
        parentID: BookmarkID? = nil,
        sortKey: String,
        title: String,
        url: String,
        createdAt: Int64? = nil,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) throws {
        self.bookmarkID = bookmarkID
        self.kind = kind
        self.rootKind = rootKind
        self.parentID = parentID
        self.sortKey = sortKey
        self.title = title
        self.url = url
        if let createdAt {
            self.createdAt = createdAt
        } else if let defaultCreatedAt = Self.windowsMicroseconds(for: version.modifiedAt) {
            self.createdAt = defaultCreatedAt
        } else {
            throw BookmarkModelError.invalidCreatedAt
        }
        self.version = version
        self.tombstone = tombstone
        try validate()
    }

    public var id: BookmarkID { bookmarkID }
    public var isDeleted: Bool { tombstone != nil }

    /// Re-validates values assembled by field merge before persistence or wire
    /// encoding. Missing local field clocks are allowed and normalized by the
    /// common v2 encoder; unknown or future clocks are rejected.
    public func validate() throws {
        guard Self.isValid(bookmarkID.rawValue) else {
            throw BookmarkModelError.invalidIdentity
        }
        guard (rootKind != nil) != (parentID != nil) else {
            throw BookmarkModelError.invalidLocation
        }
        if let parentID {
            guard Self.isValid(parentID.rawValue), parentID != bookmarkID else {
                throw BookmarkModelError.invalidLocation
            }
        }
        guard !sortKey.isEmpty, sortKey.utf8.count <= 1_024,
              sortKey.utf8.allSatisfy({ $0 >= 0x21 && $0 <= 0x7e }) else {
            throw BookmarkModelError.invalidSortKey
        }
        guard title.utf8.count <= 65_536, !title.contains("\0") else {
            throw BookmarkModelError.invalidTitle
        }
        try validateURL()
        guard createdAt > 0 else {
            throw BookmarkModelError.invalidCreatedAt
        }
        try validateVersion()
        if let tombstone, tombstone.entityID != bookmarkID.rawValue {
            throw BookmarkModelError.invalidTombstone
        }
    }

    private func validateURL() throws {
        guard url.utf8.count <= 131_072, !url.contains("\0") else {
            throw BookmarkModelError.invalidURL
        }
        if kind == .folder {
            guard url.isEmpty else { throw BookmarkModelError.invalidURL }
            return
        }
        guard !url.isEmpty,
              let parsed = URL(string: url), parsed.scheme != nil,
              let components = URLComponents(string: url),
              let scheme = components.scheme, !scheme.isEmpty,
              components.user == nil, components.password == nil else {
            throw BookmarkModelError.invalidURL
        }
        if scheme.caseInsensitiveCompare("http") == .orderedSame ||
            scheme.caseInsensitiveCompare("https") == .orderedSame {
            guard let host = components.host, !host.isEmpty else {
                throw BookmarkModelError.invalidURL
            }
        }
    }

    private func validateVersion() throws {
        guard version.schemaVersion == 2,
              Self.isValid(version.modifiedBy.rawValue),
              version.modifiedAt.nodeID == version.modifiedBy,
              Self.windowsMicroseconds(for: version.modifiedAt) != nil else {
            throw BookmarkModelError.invalidVersion
        }
        guard Set(version.fieldVersions.keys).isSubset(of: Self.syncFields) else {
            throw BookmarkModelError.invalidFieldVersions
        }
        for clock in version.fieldVersions.values {
            guard Self.isValid(clock.nodeID.rawValue),
                  Self.windowsMicroseconds(for: clock) != nil,
                  clock <= version.modifiedAt else {
                throw BookmarkModelError.invalidFieldVersions
            }
        }
    }

    private static func windowsMicroseconds(
        for clock: HybridLogicalClock
    ) -> Int64? {
        let (milliseconds, multiplyOverflow) = clock.physicalMilliseconds
            .multipliedReportingOverflow(by: 1_000)
        guard !multiplyOverflow else { return nil }
        let (unixMicroseconds, submillisecondOverflow) = milliseconds
            .addingReportingOverflow(UInt64(clock.submillisecondMicroseconds))
        guard !submillisecondOverflow else { return nil }
        let windowsEpochOffset: UInt64 = 11_644_473_600_000_000
        let (windowsMicroseconds, epochOverflow) = unixMicroseconds
            .addingReportingOverflow(windowsEpochOffset)
        guard !epochOverflow, windowsMicroseconds <= UInt64(Int64.max) else { return nil }
        return Int64(windowsMicroseconds)
    }

    private static func isValid(_ value: UUID) -> Bool {
        value != UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    }

    private enum CodingKeys: String, CodingKey {
        case bookmarkID, kind, rootKind, parentID, sortKey, title, url, createdAt
        case version, tombstone
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        try self.init(
            bookmarkID: container.decode(BookmarkID.self, forKey: .bookmarkID),
            kind: container.decode(BookmarkKind.self, forKey: .kind),
            rootKind: container.decodeIfPresent(BookmarkRoot.self, forKey: .rootKind),
            parentID: container.decodeIfPresent(BookmarkID.self, forKey: .parentID),
            sortKey: container.decode(String.self, forKey: .sortKey),
            title: container.decode(String.self, forKey: .title),
            url: container.decode(String.self, forKey: .url),
            createdAt: container.decode(Int64.self, forKey: .createdAt),
            version: container.decode(SyncVersion.self, forKey: .version),
            tombstone: container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }
}
