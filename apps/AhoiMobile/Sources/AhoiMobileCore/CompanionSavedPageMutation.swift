import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// Local repository entry point. An already linked Presence, if any, is
    /// updated in the same commit even when no new session is supplied.
    func saveBrowserPage(
        _ tab: MobileTabRecord,
        workspaceID: WorkspaceID
    ) async throws -> TreeNode {
        let result = try await setBrowserPageSaved(tab, saved: true, workspaceID: workspaceID)
        guard let page = result.bindings[tab.id] else { throw LocalCompanionStoreError.notFound }
        return page
    }

    func unsaveBrowserPage(_ tab: MobileTabRecord) async throws -> TreeNode {
        let result = try await setBrowserPageSaved(tab, saved: false)
        guard let page = result.bindings[tab.id] else { throw LocalCompanionStoreError.notFound }
        return page
    }

    func saveBrowserPage(
        _ tab: MobileTabRecord, workspaceID: WorkspaceID,
        sessionID: DeviceSessionID?, deviceName: String, deviceKind: DeviceKind
    ) async throws -> LocalMobileSharedCapture {
        try await setBrowserPageSaved(tab, saved: true, workspaceID: workspaceID,
            session: sessionID.map { (id: $0, name: deviceName, kind: deviceKind) })
    }

    func unsaveBrowserPage(
        _ tab: MobileTabRecord,
        sessionID: DeviceSessionID?, deviceName: String, deviceKind: DeviceKind
    ) async throws -> LocalMobileSharedCapture {
        try await setBrowserPageSaved(tab, saved: false,
            session: sessionID.map { (id: $0, name: deviceName, kind: deviceKind) })
    }

    private func setBrowserPageSaved(
        _ tab: MobileTabRecord, saved: Bool, workspaceID: WorkspaceID? = nil,
        session: (id: DeviceSessionID, name: String, kind: DeviceKind)? = nil
    ) async throws -> LocalMobileSharedCapture {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        try Task.checkCancellation()
        guard tab.mode == .normal, tab.participatesInSharedTabs,
              tab.hasDistinctSharedIdentities,
              (tab.treeNodeID == nil && tab.presenceID != nil && tab.sharedBindingState == .unbound) ||
                (tab.treeNodeID != nil && tab.sharedBindingState == .current) else {
            throw MobileSharedCaptureError.deferred
        }
        if saved {
            guard let workspaceID, MobileTabRecord.isNonzeroUUID(workspaceID.rawValue),
                  snapshot.visibleWorkspaces.contains(where: { $0.id == workspaceID }) else {
                throw LocalCompanionStoreError.invalidParent
            }
        } else if tab.treeNodeID == nil {
            throw SharedTabTargetError.missingPageLink // Unsave never creates a page.
        }
        let presenceID = tab.presenceID
        let nodeID: TreeNodeID
        if let boundID = tab.treeNodeID { nodeID = boundID }
        else if let presenceID { nodeID = SharedTabContract.localPageID(device: localDeviceID, presence: presenceID) }
        else { throw SharedTabTargetError.missingPageLink }
        guard MobileTabRecord.isNonzeroUUID(nodeID.rawValue),
              nodeID.rawValue != tab.id,
              presenceID.map({ nodeID.rawValue != $0.rawValue }) ?? true else {
            throw MobileSharedCaptureError.identityConflict
        }
        let matches = snapshot.treeNodes.filter { $0.id == nodeID }
        guard matches.count <= 1 else { throw MobileSharedCaptureError.identityConflict }
        let previous = matches.first
        guard previous.map({ !$0.isDeleted && $0.kind == .savedPage }) ?? (saved && tab.treeNodeID == nil) else {
            throw LocalCompanionStoreError.notFound
        }
        let presences = presenceID.map { id in snapshot.remoteTabs.filter { $0.id == id } } ?? []
        guard presences.count <= 1 else { throw MobileSharedCaptureError.identityConflict }
        let oldPresence = presences.first
        if let oldPresence {
            guard oldPresence.deviceID == localDeviceID, !oldPresence.isDeleted,
                  oldPresence.treeNodeID == nodeID else {
                throw MobileSharedCaptureError.identityConflict
            }
        }

        let before = snapshot
        var committed = false
        defer { if !committed { snapshot = before } }
        var result = LocalMobileSharedCapture()
        var candidate: TreeNode
        if let previous {
            try SharedSyncFormat.validate(previous.version, fields: CompanionFieldMerge.treeNodeFields)
            guard let target = try SharedTabURLGroup.of(previous),
                  snapshot.visibleWorkspaces.contains(where: { $0.id == previous.workspaceID }) else {
                throw LocalCompanionStoreError.invalidSnapshot
            }
            // Save changes persistence/location, never the stale WebKit target,
            // title or custom-title cache. Empty new-tab targets cannot be saved.
            try target.validatePage(isTemporary: !saved)
            candidate = previous
            candidate.isTemporary = !saved
            if saved, let workspaceID, previous.workspaceID != workspaceID {
                candidate.workspaceID = workspaceID
                candidate.parentID = nil
                candidate.orderKey = try savedPageRootOrder(workspaceID: workspaceID)
                candidate.wireSortKey = nil
            }
            if candidate != previous {
                candidate.version = try nextVersion()
                candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
            }
        } else {
            guard saved, tab.canPublishSharedTab, let workspaceID else {
                throw MobileSharedCaptureError.deferred
            }
            let target: SharedTabTarget
            if let url = MobileTabRecord.normalizedURLString(tab.url) {
                target = try SharedTabTarget(kind: .web, url: url)
            } else if tab.url == nil, let localTarget = tab.sharedTarget, localTarget.kind == .localOnly {
                target = localTarget
            } else {
                throw SharedTabTargetError.invalidTarget
            }
            try target.validatePage(isTemporary: false)
            let title = MobileTabRecord.normalizedTitle(tab.effectiveTitle)
            candidate = try TreeNode(
                treeNodeID: nodeID, workspaceID: workspaceID,
                kind: .savedPage, title: title.isEmpty ? target.url : title, url: target.url,
                orderKey: savedPageRootOrder(workspaceID: workspaceID),
                targetKind: target.kind, localScheme: target.localScheme,
                version: nextVersion()
            )
            candidate = CompanionFieldMerge.stampLocal(previous: nil, candidate: candidate)
        }
        try SharedSyncFormat.validate(candidate.version, fields: CompanionFieldMerge.treeNodeFields)
        if candidate != previous {
            if let index = snapshot.treeNodes.firstIndex(where: { $0.id == candidate.id }) {
                snapshot.treeNodes[index] = candidate
            } else {
                snapshot.treeNodes.append(candidate)
            }
            result.outbound.nodes.append(candidate)
        }
        result.bindings[tab.id] = candidate

        if let session, let presenceID {
            guard MobileTabRecord.isNonzeroUUID(session.id.rawValue),
                  snapshot.sessions.first(where: { $0.id == session.id }).map({
                      $0.deviceID == localDeviceID && !$0.isDeleted
                  }) ?? true else { throw LocalCompanionStoreError.invalidSnapshot }
            let publication = try publishLocalMobileSessionInSnapshot(
                sessionID: session.id, deviceName: session.name,
                deviceKind: session.kind, workspaceID: candidate.workspaceID
            )
            result.outbound.devices.append(publication.device)
            result.outbound.sessions.append(publication.session)
            let presence = try publishLinkedPresenceInSnapshot(
                id: presenceID, page: candidate, sessionID: session.id,
                deviceName: session.name, deviceKind: session.kind
            )
            if presence != oldPresence { result.outbound.tabs.append(presence) }
        } else if let oldPresence, let presenceID {
            let presence = try publishLinkedPresenceInSnapshot(
                id: presenceID, page: candidate, sessionID: oldPresence.sessionID,
                deviceName: oldPresence.deviceName, deviceKind: oldPresence.deviceKind
            )
            if presence != oldPresence { result.outbound.tabs.append(presence) }
        }
        try Task.checkCancellation()
        if snapshot != before { try await persist() }
        committed = true
        return result
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
        let pending = mobileSharedIntentTasks[tab.id]
        let bridge = syncBridge
        // Enter the existing helper before awaiting intent tasks, so its
        // original runtime-generation guard covers that wait as well.
        let result = await performLocalFirstMutation({
            await pending?.value
            try Task.checkCancellation()
            return try await repository.saveBrowserPage(tab, workspaceID: workspaceID,
                sessionID: mobileSessionID, deviceName: mobileDeviceName, deviceKind: mobileDeviceKind)
        }, didCommit: { result in
            if let node = result.bindings[tab.id] { didCommit(node) }
        }, ignoreFailure: { $0 is CancellationError }, enqueue: { result in
            guard let bridge else { return }
            try await result.outbound.enqueue(using: bridge)
        })
        return result?.bindings[tab.id]
    }

    func unsaveBrowserPage(
        _ tab: MobileTabRecord,
        didCommit: @MainActor (TreeNode) -> Void
    ) async -> TreeNode? {
        let pending = mobileSharedIntentTasks[tab.id]
        let bridge = syncBridge
        let result = await performLocalFirstMutation({
            await pending?.value
            try Task.checkCancellation()
            return try await repository.unsaveBrowserPage(tab,
                sessionID: mobileSessionID, deviceName: mobileDeviceName, deviceKind: mobileDeviceKind)
        }, didCommit: { result in
            if let node = result.bindings[tab.id] { didCommit(node) }
        }, ignoreFailure: { $0 is CancellationError }, enqueue: { result in
            guard let bridge else { return }
            try await result.outbound.enqueue(using: bridge)
        })
        return result?.bindings[tab.id]
    }
}
