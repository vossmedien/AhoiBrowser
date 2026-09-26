import Foundation
import AhoiCloudKitSpike

/// ADR 0012, WS-MERGE-07: "Zusammenführen mit …" on mobile.
extension CompanionAppModel {
    /// Wires the browser so a merge also moves the source's open tabs to
    /// the target, and an undo moves exactly those tabs back.
    public func connectWorkspaceMerges(to browser: MobileBrowserController) {
        workspaceMergeTabMover = { [weak browser] source, target in
            browser?.moveTabs(fromWorkspace: source, to: target) ?? []
        }
        workspaceMergeTabRestorer = { [weak browser] ids, source in
            browser?.moveTabs(ids, to: source)
        }
    }

    /// Whether `source` can be merged into `target`: both are shared
    /// Workspaces of this library. A fully separated Workspace (ADR 0011)
    /// never merges, because its sign-ins and website data cannot move.
    public func canMergeWorkspace(_ source: WorkspaceID, into target: WorkspaceID) -> Bool {
        source != target
            && !separatedWorkspaces.isSeparatedWorkspace(source.rawValue)
            && !separatedWorkspaces.isSeparatedWorkspace(target.rawValue)
            && snapshot.visibleWorkspaces.contains { $0.id == source }
            && snapshot.visibleWorkspaces.contains { $0.id == target }
    }

    public func mergeWorkspace(
        _ source: WorkspaceID,
        into target: WorkspaceID,
        intoFolder: Bool = true
    ) async {
        guard canMergeWorkspace(source, into: target) else {
            loadError = CompanionL10n.string(
                "workspace.merge.separated",
                fallback: "Fully separated workspaces can't be merged: their sign-ins and website data never move. Move pages one by one instead."
            )
            return
        }
        _ = await performLocalFirstMutation({
            try await repository.mergeWorkspace(source, into: target, intoFolder: intoFolder)
        }, didCommit: { receipt in
            self.pendingWorkspaceMergeUndo = receipt
            self.workspaceMergeTabIDs = self.workspaceMergeTabMover?(source, target) ?? []
        }, enqueue: { receipt in
            guard let bridge = self.syncBridge else { return }
            for node in receipt.nodes { try await bridge.enqueue(node) }
            try await bridge.enqueue(receipt.source)
        })
    }

    /// One step back: the source Workspace and its pages return, and the
    /// tabs the merge moved go back with it.
    public func undoWorkspaceMerge() async {
        guard let receipt = pendingWorkspaceMergeUndo else { return }
        let tabIDs = workspaceMergeTabIDs
        dismissWorkspaceMergeUndo()
        _ = await performLocalFirstMutation({
            try await repository.undoWorkspaceMerge(receipt)
        }, didCommit: { _ in
            self.workspaceMergeTabRestorer?(tabIDs, receipt.sourceID)
        }, enqueue: { restored in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(restored.workspace)
            for node in restored.nodes { try await bridge.enqueue(node) }
        })
    }

    public func dismissWorkspaceMergeUndo() {
        pendingWorkspaceMergeUndo = nil
        workspaceMergeTabIDs = []
    }
}
