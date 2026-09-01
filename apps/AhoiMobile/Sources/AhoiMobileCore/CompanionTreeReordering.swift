import AhoiCloudKitSpike

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

        let ordered = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == previous.workspaceID &&
                $0.parentID == previous.parentID
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
        withoutMoving.insert(previous, at: insertionIndex)
        guard withoutMoving.map(\.id) != ordered.map(\.id) else { return previous }

        let lower = insertionIndex > 0 ? withoutMoving[insertionIndex - 1] : nil
        let upper = insertionIndex + 1 < withoutMoving.count
            ? withoutMoving[insertionIndex + 1]
            : nil
        var candidate = previous
        candidate.orderKey = try OrderKey.between(
            lower?.orderKey,
            upper?.orderKey,
            tieBreaker: localDeviceID
        )
        candidate.wireSortKey = nil
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
        if left.syncSortKey != right.syncSortKey {
            return left.syncSortKey < right.syncSortKey
        }
        return left.id < right.id
    }
}
