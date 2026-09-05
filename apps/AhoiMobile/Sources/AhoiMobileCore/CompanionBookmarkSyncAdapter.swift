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
        guard bookmarkSyncEnabled else { return }
        try await provider.enqueue(makeBookmarkRecord(bookmark))
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
