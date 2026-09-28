import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    public func setBookmarkSyncEnabled(_ enabled: Bool) {
        provider.setBookmarkCategoryApproved(enabled)
        guard bookmarkSyncEnabled != enabled else { return }
        bookmarkSyncEnabled = enabled
        bookmarkHydrationRequired = enabled
    }

    public func enqueue(_ bookmark: BookmarkRecord) async throws {
        guard bookmarkSyncEnabled, !Self.isLocalOnlyBookmark(bookmark) else { return }
        try await provider.enqueue(makeBookmarkRecord(bookmark))
    }

    /// DoD 14, owner decision 28 Sep 2026: only http(s) bookmarks with a host
    /// leave the device. file:, chrome:, javascript: bookmarklets and data:
    /// stay local. Mirrors C++ `IsLocalOnlyBookmarkUrl`.
    static func isLocalOnlyBookmark(_ bookmark: BookmarkRecord) -> Bool {
        guard bookmark.kind == .url else { return false }
        guard let components = URLComponents(string: bookmark.url),
              let scheme = components.scheme?.lowercased() else { return true }
        return !((scheme == "http" || scheme == "https") &&
            components.host?.isEmpty == false)
    }

    func makeBookmarkRecord(_ bookmark: BookmarkRecord) throws -> SyncRecord {
        try codec.makeRecord(
            recordID: bookmark.id.rawValue, entityID: bookmark.id.rawValue,
            dataClass: .bookmark, version: bookmark.version,
            plaintext: wireCodec.encode(bookmark), tombstone: bookmark.tombstone
        )
    }

    func decodeBookmarkRecord(_ record: SyncRecord, plaintext: Data) throws -> BookmarkRecord {
        let value = try wireCodec.decodeBookmark(record, plaintext: plaintext)
        try validate(record, identity: value.id.rawValue, version: value.version,
                     tombstone: value.tombstone)
        return value
    }
}
