import Foundation
import AhoiCloudKitSpike

enum CompanionBookmarkHierarchy {
    static func index(_ records: [BookmarkRecord]) throws -> [BookmarkID: BookmarkRecord] {
        var result: [BookmarkID: BookmarkRecord] = [:]
        for record in records {
            try record.validate()
            guard result.updateValue(record, forKey: record.id) == nil else {
                throw LocalCompanionStoreError.invalidSnapshot
            }
        }
        return result
    }

    static func validate(_ records: [BookmarkRecord]) throws {
        let nodes = try index(records)
        var completed = Set<BookmarkID>()
        for node in records where !node.isDeleted && !completed.contains(node.id) {
            var path: [BookmarkID] = []
            var visiting = Set<BookmarkID>()
            var cursor: BookmarkRecord? = node
            while let current = cursor, !current.isDeleted, !completed.contains(current.id) {
                guard visiting.insert(current.id).inserted else {
                    throw LocalCompanionStoreError.treeCycle
                }
                path.append(current.id)
                guard let parentID = current.parentID, let parent = nodes[parentID] else { break }
                guard parent.kind == .folder else { throw LocalCompanionStoreError.invalidParent }
                cursor = parent
            }
            completed.formUnion(path)
        }
        // Even an already completed/deleted URL is not a valid known parent.
        for node in records where !node.isDeleted {
            if let parentID = node.parentID, let parent = nodes[parentID], parent.kind != .folder {
                throw LocalCompanionStoreError.invalidParent
            }
        }
    }

    static func visible(_ records: [BookmarkRecord]) -> [BookmarkRecord] {
        // A partial provider page can contain children before their parents.
        // Keep those records durable, but expose only fully live rooted paths.
        var children: [BookmarkID: [BookmarkRecord]] = [:]
        var pending: [BookmarkRecord] = []
        for node in records where !node.isDeleted {
            if let parentID = node.parentID {
                children[parentID, default: []].append(node)
            } else if node.rootKind != nil {
                pending.append(node)
            }
        }
        var visible: [BookmarkRecord] = []
        var visited = Set<BookmarkID>()
        while let node = pending.popLast() {
            guard visited.insert(node.id).inserted else { continue }
            visible.append(node)
            if node.kind == .folder { pending.append(contentsOf: children[node.id] ?? []) }
        }
        return visible.sorted(by: orderedBefore)
    }

    static func orderedBefore(_ lhs: BookmarkRecord, _ rhs: BookmarkRecord) -> Bool {
        if lhs.sortKey != rhs.sortKey { return lhs.sortKey < rhs.sortKey }
        return lhs.id < rhs.id
    }

    static func descendants(of id: BookmarkID, in records: [BookmarkRecord]) -> Set<BookmarkID> {
        var children: [BookmarkID: [BookmarkID]] = [:]
        for node in records where !node.isDeleted {
            if let parentID = node.parentID { children[parentID, default: []].append(node.id) }
        }
        var result = Set<BookmarkID>()
        var pending = [id]
        while let current = pending.popLast() {
            guard result.insert(current).inserted else { continue }
            pending.append(contentsOf: children[current] ?? [])
        }
        return result
    }
}

extension CompanionSnapshot {
    public var visibleBookmarks: [BookmarkRecord] {
        CompanionBookmarkHierarchy.visible(bookmarks)
    }
}
