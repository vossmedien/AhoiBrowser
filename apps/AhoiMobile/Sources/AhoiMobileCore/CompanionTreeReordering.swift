import Foundation
import AhoiCloudKitSpike

/// Preserves Chromium's opaque lexical sort keys when choosing a local
/// position. A decoded native key's numeric adapter is not its wire order.
struct CompanionTreePosition {
    let orderKey: OrderKey
    let wireSortKey: String?

    /// Scoped position in the appended root segment. A deliberate move can
    /// retain this suffix in its newly stamped opaque key. Reprojection rebases
    /// the segment instead of pushing its remaining passive nodes past that
    /// explicit position again. This is an existing sort_key value, not a new
    /// field clock, record type or implicit structural write.
    static func mergeRootMarker(_ workspace: WorkspaceID) -> String {
        "!:ahoi-merge-root/" + workspace.rawValue.uuidString.lowercased() + "/"
    }

    static func mergeRootSuffix(_ key: String, workspace: WorkspaceID) -> String? {
        guard let range = key.range(of: mergeRootMarker(workspace), options: [.backwards, .literal]),
              range.upperBound != key.endIndex else { return nil }
        return String(key[range.upperBound...])
    }

    static func mergeRootToken(_ key: String, node: TreeNodeID) -> String {
        // Escape low bytes and '!' in an order-preserving way. The !/ terminal
        // sorts before every !xx escape, so prefix keys keep their original
        // order. It also prevents a nested raw key from imitating our marker.
        let hex = Array("0123456789abcdef".utf8)
        var bytes: [UInt8] = []
        for byte in key.utf8 {
            if byte <= 0x21 {
                bytes.append(0x21)
                bytes.append(hex[Int(byte >> 4)])
                bytes.append(hex[Int(byte & 0x0f)])
            } else {
                bytes.append(byte)
            }
        }
        return String(decoding: bytes, as: UTF8.self) + "!/" + node.rawValue.uuidString.lowercased()
    }

    static func less(_ lhs: String, _ rhs: String) -> Bool {
        lhs.utf8.lexicographicallyPrecedes(rhs.utf8)
    }

    static func precedes(_ lhs: TreeNode, _ rhs: TreeNode) -> Bool {
        if less(lhs.syncSortKey, rhs.syncSortKey) { return true }
        if less(rhs.syncSortKey, lhs.syncSortKey) { return false }
        return lhs.id < rhs.id
    }

    static func between(
        _ lower: TreeNode?, _ upper: TreeNode?, device: DeviceID
    ) throws -> Self {
        if lower?.wireSortKey == nil, upper?.wireSortKey == nil,
           let order = try? OrderKey.between(
               lower?.orderKey, upper?.orderKey, tieBreaker: device) {
            return Self(orderKey: order, wireSortKey: nil)
        }
        let key = try lexicalBetween(lower?.syncSortKey, upper?.syncSortKey)
        return try Self(orderKey: OrderKey(
            components: key.utf8.prefix(OrderKey.maximumDepth).map(UInt16.init),
            tieBreaker: device), wireSortKey: key)
    }

    /// Same fractional lexical rules as the native sidebar for ASCII keys.
    /// Scalar boundaries also keep non-ASCII wire keys valid UTF-8.
    private static func lexicalBetween(_ lower: String?, _ upper: String?) throws -> String {
        guard lower?.isEmpty != true, upper?.isEmpty != true else {
            throw OrderKeyError.invalidBounds
        }
        if let lower, let upper, !less(lower, upper) { throw OrderKeyError.invalidBounds }
        if let lower, upper == nil { return lower + "@" }
        guard let upper else { return "@" }
        let right = Array(upper.unicodeScalars)
        guard let lower else {
            if right[0].value > 1 {
                return String(Unicode.Scalar(right[0].value / 2) ?? "\u{1}")
            }
            if right.count > 1 { return String(right[0]) }
            throw OrderKeyError.invalidBounds
        }
        let left = Array(lower.unicodeScalars)
        var common = 0
        while common < left.count, common < right.count, left[common] == right[common] {
            common += 1
        }
        if common == left.count {
            if right[common].value > 1 {
                return lower + String(Unicode.Scalar(right[common].value / 2) ?? "\u{1}")
            }
            if right.count > common + 1 {
                return lower + String(right[common])
            }
            throw OrderKeyError.invalidBounds
        }
        let lhs = left[common].value
        let rhs = right[common].value
        if lhs + 1 < rhs, let middle = Unicode.Scalar(lhs + (rhs - lhs) / 2) {
            var result = ""
            for scalar in left.prefix(common) { result.unicodeScalars.append(scalar) }
            result.unicodeScalars.append(middle)
            return result
        }
        return lower + "@"
    }
}

extension LocalFirstRepository {
    /// Repositions a live node among siblings without changing its workspace
    /// or parent. `successorID == nil` means the end of the sibling list.
    @discardableResult
    public func reorderTreeNode(
        _ id: TreeNodeID,
        before successorID: TreeNodeID?
    ) async throws -> TreeNode {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()

        guard let index = snapshot.treeNodes.firstIndex(where: {
            $0.id == id && !$0.isDeleted
        }) else {
            throw LocalCompanionStoreError.notFound
        }
        let previous = snapshot.treeNodes[index]
        if successorID == id { return previous }
        let presented = snapshot.presentationNode(id) ?? previous

        let ordered = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == presented.workspaceID &&
                $0.parentID == presented.parentID
        }.sorted(by: siblingOrder)
        var withoutMoving = ordered.filter { $0.id != id }
        let insertionIndex: Int
        if let successorID {
            guard let successorIndex = withoutMoving.firstIndex(where: {
                $0.id == successorID
            }) else {
                throw LocalCompanionStoreError.notFound
            }
            insertionIndex = successorIndex
        } else {
            insertionIndex = withoutMoving.endIndex
        }
        withoutMoving.insert(presented, at: insertionIndex)
        guard withoutMoving.map(\.id) != ordered.map(\.id) else { return previous }

        let lower = insertionIndex > 0 ? withoutMoving[insertionIndex - 1] : nil
        let upper = insertionIndex + 1 < withoutMoving.count
            ? withoutMoving[insertionIndex + 1]
            : nil
        var candidate = previous
        // The explicit user reorder commits the displayed location; passive
        // projection alone never changes the authoritative location register.
        candidate.workspaceID = presented.workspaceID
        candidate.parentID = presented.parentID
        let position = try CompanionTreePosition.between(lower, upper, device: localDeviceID)
        candidate.orderKey = position.orderKey
        candidate.wireSortKey = position.wireSortKey
        candidate.version = try nextVersion()
        candidate = CompanionFieldMerge.stampLocal(
            previous: previous,
            candidate: candidate
        )
        snapshot.treeNodes[index] = candidate
        try await persist()
        return candidate
    }

    private func siblingOrder(_ left: TreeNode, _ right: TreeNode) -> Bool {
        CompanionTreePosition.precedes(left, right)
    }
}
