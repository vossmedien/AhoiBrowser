import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// Explicit Home intent, independent of navigation and usable without Sync.
    func setSavedPageHome(_ id: TreeNodeID, expectedHome: SharedTabTarget?,
                          target: SharedTabTarget) async throws -> TreeNode {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        try Task.checkCancellation()
        try target.validatePage(isTemporary: false)
        guard let index = snapshot.treeNodes.firstIndex(where: { $0.id == id }),
              !snapshot.treeNodes[index].isDeleted,
              snapshot.treeNodes[index].kind == .savedPage,
              !snapshot.treeNodes[index].isTemporary,
              snapshot.treeNodes[index].homeTarget == expectedHome else {
            throw MobileSharedCaptureError.deferred
        }
        let previous = snapshot.treeNodes[index]
        if previous.homeTarget == target { return previous }
        var candidate = previous
        candidate.homeTarget = target
        candidate.version = try nextVersion()
        candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
        try SharedSyncFormat.validate(candidate.version, fields: CompanionFieldMerge.treeNodeFields)
        snapshot.treeNodes[index] = candidate
        do { try await persist() }
        catch { snapshot.treeNodes[index] = previous; throw error }
        return candidate
    }
}

extension CompanionAppModel {
    @discardableResult
    func setSavedPageHome(_ tab: MobileTabRecord, expectedHome: SharedTabTarget?,
                          target: SharedTabTarget) async -> TreeNode? {
        guard tab.mode == .normal, tab.isSaved, tab.sharedBindingState == .current,
              let id = tab.treeNodeID else { return nil }
        let pending = mobileSharedIntentTasks[tab.id]
        let bridge = syncBridge
        return await performLocalFirstMutation({
            await pending?.value
            try Task.checkCancellation()
            return try await repository.setSavedPageHome(id, expectedHome: expectedHome, target: target)
        }, enqueue: { node in
            try await bridge?.enqueue(node)
        })
    }
}
