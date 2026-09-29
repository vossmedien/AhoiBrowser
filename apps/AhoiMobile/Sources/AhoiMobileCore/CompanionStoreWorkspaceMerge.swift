import Foundation
import AhoiCloudKitSpike

enum CompanionWorkspaceCompactionError: Error, Equatable {
    case notExpiredTombstone
    case sourceChanged
    case conflictingWatermark
}

/// The result of one Workspace merge (ADR 0012, WS-MERGE-07): everything to
/// enqueue for sync, plus what a single undo needs to put it back.
public struct CompanionWorkspaceMergeReceipt: Sendable, Equatable {
    public let sourceID: WorkspaceID
    public let targetID: WorkspaceID
    /// The tombstoned source Workspace.
    public let source: Workspace
    /// Every node the merge rewrote, including the new folder, as stored.
    public let nodes: [TreeNode]
    /// The same nodes before the merge (the new folder has none).
    let previousNodes: [TreeNodeID: TreeNode]
    let previousSource: Workspace
    public let folderID: TreeNodeID?

    public var previousSourceName: String { previousSource.name }
}

extension LocalFirstRepository {
    /// Commits the receiver-local route and removes the matching tombstone in
    /// one Snapshot write. The caller must first verify the exact encrypted
    /// upload receipt and an empty pending-save/fetched-inbox gate. This method
    /// cannot create that server acknowledgement itself.
    func compactWorkspaceTombstone(
        matching expected: Workspace, nowMilliseconds: UInt64
    ) async throws -> Bool {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard let tombstone = expected.tombstone,
              tombstone.entityID == expected.id.rawValue else {
            throw CompanionWorkspaceCompactionError.notExpiredTombstone
        }
        let prior = snapshot.deletionWatermarks.filter {
            $0.dataClass == .workspace && $0.entityID == expected.id.rawValue
        }
        if !prior.isEmpty {
            guard prior.count == 1, prior[0].version == expected.version,
                  prior[0].mergedInto == expected.mergedInto,
                  !snapshot.workspaces.contains(where: { $0.id == expected.id }) else {
                throw CompanionWorkspaceCompactionError.conflictingWatermark
            }
            return false
        }
        let (retentionFloor, overflow) = tombstone.deletedAt.physicalMilliseconds
            .addingReportingOverflow(30 * 24 * 60 * 60 * 1_000)
        guard !overflow, nowMilliseconds >= tombstone.purgeAfterMilliseconds,
              nowMilliseconds >= retentionFloor else {
            throw CompanionWorkspaceCompactionError.notExpiredTombstone
        }
        let current = snapshot.workspaces.filter { $0.id == expected.id }
        guard current.count == 1 else {
            throw CompanionWorkspaceCompactionError.sourceChanged
        }
        let codec = DesktopWirePayloadCodec()
        let currentBytes = try codec.encode(current[0])
        let expectedBytes = try codec.encode(expected)
        guard currentBytes == expectedBytes else {
            throw CompanionWorkspaceCompactionError.sourceChanged
        }
        var candidate = snapshot
        candidate.workspaces.removeAll { $0.id == expected.id }
        let watermark = CompanionDeletionWatermark(
            dataClass: .workspace, entityID: expected.id.rawValue,
            version: expected.version, compactedAtMilliseconds: nowMilliseconds,
            mergedInto: expected.mergedInto)
        try watermark.validate()
        candidate.deletionWatermarks.append(watermark)
        try await commitImportedSnapshot(candidate)
        return true
    }

    /// Merges `source` into `target` in one mutation and one persist. The
    /// source's live roots move, in order, to the end of the target: into one
    /// new folder named after the source (with its icon and accent) or flat.
    /// Every live descendant takes the target Workspace. The source is
    /// tombstoned. Undo with `undoWorkspaceMerge(_:)`.
    public func mergeWorkspace(
        _ sourceID: WorkspaceID,
        into targetID: WorkspaceID,
        intoFolder: Bool
    ) async throws -> CompanionWorkspaceMergeReceipt {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard sourceID != targetID,
              let sourceIndex = snapshot.workspaces.firstIndex(where: {
                  $0.id == sourceID && !$0.isDeleted
              }),
              snapshot.workspaces.contains(where: { $0.id == targetID && !$0.isDeleted })
        else {
            throw LocalCompanionStoreError.notFound
        }
        let previousSource = snapshot.workspaces[sourceIndex]
        let roots = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == sourceID && $0.parentID == nil
        }.sorted(by: CompanionTreePosition.precedes)
        var lastTargetNode = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == targetID && $0.parentID == nil
        }.max(by: CompanionTreePosition.precedes)

        var previousNodes: [TreeNodeID: TreeNode] = [:]
        var changed: [TreeNode] = []
        var folderID: TreeNodeID?
        if intoFolder, !roots.isEmpty {
            let version = try nextVersion()
            let position = try CompanionTreePosition.between(lastTargetNode, nil, device: localDeviceID)
            let folder = CompanionFieldMerge.stampLocal(
                previous: nil,
                candidate: try TreeNode(
                    treeNodeID: TreeNodeID(),
                    workspaceID: targetID,
                    parentID: nil,
                    kind: .folder,
                    title: previousSource.name,
                    url: nil,
                    icon: previousSource.icon,
                    accent: previousSource.accent,
                    orderKey: position.orderKey,
                    wireSortKey: position.wireSortKey,
                    targetKind: nil,
                    homeTarget: nil,
                    createdAt: version.modifiedAt,
                    version: version
                )
            )
            snapshot.treeNodes.append(folder)
            folderID = folder.id
        }
        for root in roots {
            guard let index = snapshot.treeNodes.firstIndex(where: { $0.id == root.id }) else {
                throw LocalCompanionStoreError.notFound
            }
            let previous = snapshot.treeNodes[index]
            var candidate = previous
            candidate.workspaceID = targetID
            if let folderID {
                // Siblings keep their keys: their order inside the new folder
                // is the order they had at the source's root.
                candidate.parentID = folderID
                candidate.orderKey = root.orderKey
                candidate.wireSortKey = root.wireSortKey
            } else {
                candidate.parentID = nil
                let position = try CompanionTreePosition.between(lastTargetNode, nil, device: localDeviceID)
                candidate.orderKey = position.orderKey
                candidate.wireSortKey = position.wireSortKey
                lastTargetNode = candidate
            }
            candidate.version = try nextVersion()
            candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
            snapshot.treeNodes[index] = candidate
            previousNodes[previous.id] = previous
            changed.append(candidate)
            for descendant in try moveLiveDescendants(of: root.id, to: targetID) {
                previousNodes[descendant.previous.id] = descendant.previous
                changed.append(descendant.moved)
            }
        }

        // The folder goes last, so undo tombstones it after its children
        // are back and peers never see a live child under a deleted folder.
        if let folderID,
           let folder = snapshot.treeNodes.first(where: { $0.id == folderID }) {
            changed.append(folder)
        }
        let version = try nextVersion()
        var deleted = previousSource
        deleted.version = version
        deleted.tombstone = makeTombstone(
            entityID: sourceID.rawValue,
            version: version,
            parentID: nil,
            orderKey: nil
        )
        // Peers re-home nodes that reach the merged Workspace later (crest 084).
        deleted.mergedInto = targetID
        deleted = CompanionFieldMerge.stampLocal(previous: previousSource, candidate: deleted)
        snapshot.workspaces[sourceIndex] = deleted
        try await persist()
        return CompanionWorkspaceMergeReceipt(
            sourceID: sourceID,
            targetID: targetID,
            source: deleted,
            nodes: changed,
            previousNodes: previousNodes,
            previousSource: previousSource,
            folderID: folderID
        )
    }

    /// Puts one merge back: the source is live again and every node returns
    /// to its place there, each as a new version so the undo wins on synced
    /// devices; the merge folder is tombstoned. Refused once any of these
    /// records changed after the merge, e.g. by sync or another edit.
    /// Returns the records to enqueue.
    public func undoWorkspaceMerge(
        _ receipt: CompanionWorkspaceMergeReceipt
    ) async throws -> (workspace: Workspace, nodes: [TreeNode]) {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard let sourceIndex = snapshot.workspaces.firstIndex(where: {
                  $0.id == receipt.sourceID
              }),
              snapshot.workspaces[sourceIndex].version == receipt.source.version,
              snapshot.workspaces.contains(where: {
                  $0.id == receipt.targetID && !$0.isDeleted
              })
        else {
            throw LocalCompanionStoreError.mergeUndoOutdated
        }
        var indices: [TreeNodeID: Int] = [:]
        for node in receipt.nodes {
            guard let index = snapshot.treeNodes.firstIndex(where: { $0.id == node.id }),
                  snapshot.treeNodes[index].version == node.version else {
                throw LocalCompanionStoreError.mergeUndoOutdated
            }
            indices[node.id] = index
        }

        // New children do not bump their parent's version. Undo would either
        // tombstone the merge folder or move an existing folder back to the
        // source, leaving an unrecorded child orphaned or in another Workspace.
        // Reject before changing any record; unrelated target edits are safe.
        guard !snapshot.treeNodes.contains(where: { node in
            guard !node.isDeleted, indices[node.id] == nil,
                  let parentID = node.parentID else { return false }
            return indices[parentID] != nil
        }) else {
            throw LocalCompanionStoreError.mergeUndoOutdated
        }

        var restored: [TreeNode] = []
        for node in receipt.nodes {
            guard let index = indices[node.id] else { continue }
            let current = snapshot.treeNodes[index]
            var candidate = current
            let version = try nextVersion()
            if let previous = receipt.previousNodes[node.id] {
                candidate.workspaceID = previous.workspaceID
                candidate.parentID = previous.parentID
                candidate.orderKey = previous.orderKey
                candidate.wireSortKey = previous.wireSortKey
            } else {
                // The merge folder: empty again once its children are back.
                candidate.tombstone = makeTombstone(
                    entityID: current.id.rawValue,
                    version: version,
                    parentID: current.parentID?.rawValue,
                    orderKey: current.orderKey
                )
            }
            candidate.version = version
            candidate = CompanionFieldMerge.stampLocal(previous: current, candidate: candidate)
            snapshot.treeNodes[index] = candidate
            restored.append(candidate)
        }

        let current = snapshot.workspaces[sourceIndex]
        var revived = current
        revived.tombstone = nil
        revived.mergedInto = nil
        revived.version = try nextVersion()
        revived = CompanionFieldMerge.stampLocal(previous: current, candidate: revived)
        snapshot.workspaces[sourceIndex] = revived
        try await persist()
        return (revived, restored)
    }

    /// Gives every live descendant of `id` the Workspace `workspaceID`.
    private func moveLiveDescendants(
        of id: TreeNodeID,
        to workspaceID: WorkspaceID
    ) throws -> [(previous: TreeNode, moved: TreeNode)] {
        var result: [(previous: TreeNode, moved: TreeNode)] = []
        var pending = [id]
        var visited = Set<TreeNodeID>()
        while let current = pending.popLast(), visited.insert(current).inserted {
            for index in snapshot.treeNodes.indices
                where snapshot.treeNodes[index].parentID == current
                    && !snapshot.treeNodes[index].isDeleted {
                let previous = snapshot.treeNodes[index]
                pending.append(previous.id)
                guard previous.workspaceID != workspaceID else { continue }
                var moved = previous
                moved.workspaceID = workspaceID
                moved.version = try nextVersion()
                moved = CompanionFieldMerge.stampLocal(previous: previous, candidate: moved)
                snapshot.treeNodes[index] = moved
                result.append((previous, moved))
            }
        }
        return result
    }
}
