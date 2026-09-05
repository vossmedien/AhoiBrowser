import Foundation
import AhoiCloudKitSpike

struct CompanionBookmarkMutation: Sendable {
    let bookmark: BookmarkRecord
    let changed: [BookmarkRecord]
}

extension LocalFirstRepository {
    @discardableResult
    public func upsert(_ incoming: BookmarkRecord) async throws -> BookmarkRecord {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let existing = snapshot.bookmarks.first { $0.id == incoming.id }
        let merged = try existing.map { try CompanionBookmarkFieldMerge.merge($0, incoming) } ?? incoming
        var candidate = snapshot
        if let index = candidate.bookmarks.firstIndex(where: { $0.id == merged.id }) {
            candidate.bookmarks[index] = merged
        } else {
            candidate.bookmarks.append(merged)
        }
        try CompanionBookmarkHierarchy.validate(candidate.bookmarks)
        if candidate != snapshot { try await commitImportedSnapshot(candidate) }
        return merged
    }

    func createBookmark(
        kind: BookmarkKind,
        rootKind: BookmarkRoot?,
        parentID: BookmarkID?,
        title: String,
        url: String
    ) async throws -> CompanionBookmarkMutation {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        try validateBookmarkDestination(rootKind: rootKind, parentID: parentID)
        let siblings = bookmarkSiblings(rootKind: rootKind, parentID: parentID)
        let version = try nextVersion().normalized(for: BookmarkRecord.syncFields)
        let candidate = try BookmarkRecord(
            kind: kind, rootKind: rootKind, parentID: parentID,
            sortKey: "M", title: title, url: url, version: version
        )
        let changed = try appendBookmark(candidate, after: siblings)
        guard let created = changed.first(where: { $0.id == candidate.id }) else {
            throw LocalCompanionStoreError.invalidSnapshot
        }
        try await commitBookmarkChanges(changed)
        return .init(bookmark: created, changed: changed)
    }

    func updateBookmark(
        _ id: BookmarkID, title: String, url: String?
    ) async throws -> [BookmarkRecord] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let previous = try liveBookmark(id)
        var candidate = previous
        candidate.title = title
        if let url { candidate.url = url }
        if candidate == previous { return [] }
        candidate.version = try nextVersion()
        candidate = CompanionBookmarkFieldMerge.stampLocal(previous: previous, candidate: candidate)
        try await commitBookmarkChanges([candidate])
        return [candidate]
    }

    func moveBookmark(
        _ id: BookmarkID, rootKind: BookmarkRoot?, parentID: BookmarkID?
    ) async throws -> [BookmarkRecord] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        try validateBookmarkDestination(rootKind: rootKind, parentID: parentID)
        let previous = try liveBookmark(id)
        if previous.rootKind == rootKind && previous.parentID == parentID { return [] }
        var moved = previous
        moved.rootKind = rootKind
        moved.parentID = parentID
        let changed = try appendBookmark(moved, after: bookmarkSiblings(
            rootKind: rootKind, parentID: parentID
        ))
        try await commitBookmarkChanges(changed)
        return changed
    }

    func reorderBookmark(_ id: BookmarkID, before successorID: BookmarkID?) async throws -> [BookmarkRecord] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let node = try liveBookmark(id)
        let previous = bookmarkSiblings(rootKind: node.rootKind, parentID: node.parentID)
        guard id != successorID else { return [] }
        var reordered = previous.filter { $0.id != id }
        if let successorID {
            guard let index = reordered.firstIndex(where: { $0.id == successorID }) else {
                throw LocalCompanionStoreError.invalidParent
            }
            reordered.insert(node, at: index)
        } else {
            reordered.append(node)
        }
        guard reordered.map(\.id) != previous.map(\.id) else { return [] }
        let changed = try renumberBookmarks(reordered)
        try await commitBookmarkChanges(changed)
        return changed
    }

    func deleteBookmark(_ id: BookmarkID) async throws -> [BookmarkRecord] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        _ = try liveBookmark(id)
        let ids = CompanionBookmarkHierarchy.descendants(of: id, in: snapshot.bookmarks)
        var deleted: [BookmarkRecord] = []
        for previous in snapshot.bookmarks where ids.contains(previous.id) && !previous.isDeleted {
            var candidate = previous
            candidate.version = try nextVersion()
            candidate.tombstone = makeTombstone(
                entityID: candidate.id.rawValue, version: candidate.version,
                parentID: candidate.parentID?.rawValue, orderKey: nil
            )
            deleted.append(CompanionBookmarkFieldMerge.stampLocal(previous: previous, candidate: candidate))
        }
        try await commitBookmarkChanges(deleted)
        return deleted
    }

    private func validateBookmarkDestination(rootKind: BookmarkRoot?, parentID: BookmarkID?) throws {
        guard (rootKind != nil) != (parentID != nil) else { throw BookmarkModelError.invalidLocation }
        if let parentID {
            guard snapshot.visibleBookmarks.contains(where: { $0.id == parentID && $0.kind == .folder }) else {
                throw LocalCompanionStoreError.invalidParent
            }
        }
    }

    private func liveBookmark(_ id: BookmarkID) throws -> BookmarkRecord {
        guard let node = snapshot.visibleBookmarks.first(where: { $0.id == id }) else {
            throw LocalCompanionStoreError.notFound
        }
        return node
    }

    private func bookmarkSiblings(rootKind: BookmarkRoot?, parentID: BookmarkID?) -> [BookmarkRecord] {
        snapshot.bookmarks.filter {
            !$0.isDeleted && $0.rootKind == rootKind && $0.parentID == parentID
        }.sorted(by: CompanionBookmarkHierarchy.orderedBefore)
    }

    private func appendBookmark(_ node: BookmarkRecord, after siblings: [BookmarkRecord]) throws -> [BookmarkRecord] {
        // ASCII lexical append interoperates with native sort keys without
        // converting them to the unrelated workspace OrderKey representation.
        let key = (siblings.last?.sortKey ?? "") + "M"
        if key.utf8.count <= 1_024 {
            var candidate = node
            candidate.sortKey = key
            candidate.version = try nextVersion()
            return [CompanionBookmarkFieldMerge.stampLocal(
                previous: snapshot.bookmarks.first { $0.id == node.id }, candidate: candidate
            )]
        }
        return try renumberBookmarks(siblings + [node])
    }

    private func renumberBookmarks(_ nodes: [BookmarkRecord]) throws -> [BookmarkRecord] {
        var changed: [BookmarkRecord] = []
        for (index, node) in nodes.enumerated() {
            let key = String(format: "%016llx", UInt64(index) + 1)
            let previous = snapshot.bookmarks.first { $0.id == node.id }
            if previous?.sortKey == key && previous?.rootKind == node.rootKind &&
                previous?.parentID == node.parentID { continue }
            var candidate = node
            candidate.sortKey = key
            candidate.version = try nextVersion()
            changed.append(CompanionBookmarkFieldMerge.stampLocal(previous: previous, candidate: candidate))
        }
        return changed
    }

    private func commitBookmarkChanges(_ changes: [BookmarkRecord]) async throws {
        var candidate = snapshot.bookmarks
        for change in changes {
            if let index = candidate.firstIndex(where: { $0.id == change.id }) {
                candidate[index] = change
            } else {
                candidate.append(change)
            }
        }
        try CompanionBookmarkHierarchy.validate(candidate)
        snapshot.bookmarks = candidate
        try await persist()
    }
}
