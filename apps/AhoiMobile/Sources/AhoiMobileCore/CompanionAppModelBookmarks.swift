import Foundation
import AhoiCloudKitSpike

extension CompanionAppModel {
    static let bookmarkSyncApprovalKey = "AhoiBookmarkSyncApproved"

    public func enableBookmarkSync() async {
        guard isSyncConfigured, desiredSyncEnabled else { return }
        defaults.set(true, forKey: Self.bookmarkSyncApprovalKey)
        isBookmarkSyncEnabled = true
        localSnapshotReseedRequired = true
        if let bridge = syncBridge { await bridge.setBookmarkSyncEnabled(true) }
        await sync()
    }

    @discardableResult
    public func createBookmark(
        kind: BookmarkKind, rootKind: BookmarkRoot?, parentID: BookmarkID?,
        title: String, url: String = ""
    ) async -> BookmarkRecord? {
        let mutation = await performLocalFirstMutation({
            try await repository.createBookmark(
                kind: kind, rootKind: rootKind, parentID: parentID, title: title, url: url
            )
        }, enqueue: { mutation in
            try await self.enqueueBookmarks(mutation.changed)
        })
        return mutation?.bookmark
    }

    @discardableResult
    public func updateBookmark(_ id: BookmarkID, title: String, url: String? = nil) async -> Bool {
        let committed = await performLocalFirstMutation({
            try await repository.updateBookmark(id, title: title, url: url)
        }, enqueue: enqueueBookmarks)
        return committed != nil
    }

    public func moveBookmark(_ id: BookmarkID, rootKind: BookmarkRoot?, parentID: BookmarkID?) async {
        _ = await performLocalFirstMutation({
            try await repository.moveBookmark(id, rootKind: rootKind, parentID: parentID)
        }, enqueue: enqueueBookmarks)
    }

    public func reorderBookmark(_ id: BookmarkID, before successorID: BookmarkID?) async {
        _ = await performLocalFirstMutation({
            try await repository.reorderBookmark(id, before: successorID)
        }, enqueue: enqueueBookmarks)
    }

    public func deleteBookmark(_ id: BookmarkID) async {
        _ = await performLocalFirstMutation({
            try await repository.deleteBookmark(id)
        }, enqueue: enqueueBookmarks)
    }

    private func enqueueBookmarks(_ bookmarks: [BookmarkRecord]) async throws {
        guard isBookmarkSyncEnabled, let bridge = syncBridge else { return }
        await bridge.setBookmarkSyncEnabled(true)
        for bookmark in bookmarks { try await bridge.enqueue(bookmark) }
    }
}
