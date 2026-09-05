import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// Saving an already linked browser tab updates its page, never allocates
    /// another identity. The workspace move and content update commit together.
    func saveBrowserPage(
        _ tab: MobileTabRecord,
        workspaceID: WorkspaceID
    ) async throws -> TreeNode {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard tab.mode == .normal,
              let url = MobileTabRecord.normalizedURLString(tab.url),
              snapshot.visibleWorkspaces.contains(where: { $0.id == workspaceID })
        else { throw LocalCompanionStoreError.invalidSnapshot }

        let previous: TreeNode?
        if let nodeID = tab.treeNodeID {
            guard let node = snapshot.visibleTreeNodes.first(where: { $0.id == nodeID }),
                  node.kind == .savedPage else { throw LocalCompanionStoreError.notFound }
            previous = node
        } else {
            previous = nil
        }
        let version = try nextVersion()
        let title = MobileTabRecord.normalizedTitle(tab.effectiveTitle)
        var candidate: TreeNode
        if let previous {
            candidate = previous
            candidate.title = title.isEmpty ? url : title
            candidate.url = url
            candidate.version = version
            if previous.workspaceID != workspaceID {
                candidate.workspaceID = workspaceID
                candidate.parentID = nil
                candidate.orderKey = try savedPageRootOrder(workspaceID: workspaceID)
                candidate.wireSortKey = nil
            }
        } else {
            candidate = try TreeNode(
                treeNodeID: TreeNodeID(), workspaceID: workspaceID,
                kind: .savedPage, title: title.isEmpty ? url : title, url: url,
                orderKey: savedPageRootOrder(workspaceID: workspaceID),
                version: version
            )
        }
        candidate.isTemporary = false
        candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
        if let index = snapshot.treeNodes.firstIndex(where: { $0.id == candidate.id }) {
            snapshot.treeNodes[index] = candidate
        } else {
            snapshot.treeNodes.append(candidate)
        }
        try await persist()
        return candidate
    }

    private func savedPageRootOrder(workspaceID: WorkspaceID) throws -> OrderKey {
        let last = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == workspaceID && $0.parentID == nil
        }.max { $0.syncSortKey < $1.syncSortKey }
        return try OrderKey.between(last?.orderKey, nil, tieBreaker: localDeviceID)
    }
}

extension CompanionAppModel {
    func saveBrowserPage(
        _ tab: MobileTabRecord,
        workspaceID: WorkspaceID,
        didCommit: @MainActor (TreeNode) -> Void
    ) async -> TreeNode? {
        await performLocalFirstMutation({
            try await repository.saveBrowserPage(tab, workspaceID: workspaceID)
        }, didCommit: didCommit, enqueue: { node in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(node)
        })
    }
}
