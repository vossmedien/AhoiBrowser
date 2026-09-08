import Foundation
import AhoiCloudKitSpike

enum MobileSharedCaptureError: Error, Equatable {
    case deferred
    case identityConflict
}

struct LocalMobileSharedCapture: Sendable {
    var bindings: [UUID: TreeNode] = [:]
    var outbound = CompanionMobilePublicationBatch()
}

extension LocalFirstRepository {
    /// Complete local upsert capture. It intentionally infers NO closures from
    /// absence: only the explicit local close path owns deletion intent.
    func captureLocalMobileTabs(
        _ tabs: [MobileTabRecord], sessionID: DeviceSessionID,
        deviceName: String, deviceKind: DeviceKind
    ) async throws -> LocalMobileSharedCapture {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        try Task.checkCancellation()
        var inputs: [MobileTabRecord] = []
        var runtimeIDs = Set<UUID>()
        var presenceIDs = Set<TabID>()
        var pageIDs = Set<TreeNodeID>()
        for tab in tabs where tab.mode == .normal && tab.participatesInSharedTabs {
            // Dormant mirrors are logical rows, not open local Presence.
            if tab.presenceID == nil, tab.sharedBindingState == .current,
               let id = tab.treeNodeID, snapshot.treeNodes.contains(where: { $0.id == id && !$0.isDeleted }) {
                continue
            }
            if let id = tab.treeNodeID, snapshot.treeNodes.contains(where: { $0.id == id && $0.isDeleted }) {
                continue // Keep local recovery content; never resurrect its node.
            }
            guard tab.canPublishSharedTab, let presence = tab.presenceID else {
                throw MobileSharedCaptureError.deferred
            }
            guard runtimeIDs.insert(tab.id).inserted, presenceIDs.insert(presence).inserted else {
                throw MobileSharedCaptureError.identityConflict
            }
            if let id = tab.treeNodeID, !pageIDs.insert(id).inserted {
                throw MobileSharedCaptureError.identityConflict
            }
            inputs.append(tab)
        }

        let before = snapshot
        var committed = false
        defer { if !committed { snapshot = before } }
        var result = LocalMobileSharedCapture()
        for tab in inputs {
            let page = try resolveLocalMobilePage(tab, outbound: &result.outbound)
            result.bindings[tab.id] = page
        }
        let session = try publishLocalMobileSessionInSnapshot(
            sessionID: sessionID, deviceName: deviceName, deviceKind: deviceKind,
            workspaceID: inputs.first.flatMap { result.bindings[$0.id]?.workspaceID }
        )
        result.outbound.devices.append(session.device)
        result.outbound.sessions.append(session.session)
        for tab in inputs {
            guard let page = result.bindings[tab.id], let presenceID = tab.presenceID else {
                throw MobileSharedCaptureError.deferred
            }
            let previous = snapshot.remoteTabs.first { $0.id == presenceID }
            let presence = try publishLinkedPresenceInSnapshot(
                id: presenceID, page: page, sessionID: sessionID,
                deviceName: deviceName, deviceKind: deviceKind
            )
            if previous != presence { result.outbound.tabs.append(presence) }
        }
        try Task.checkCancellation()
        try await persist()
        committed = true
        return result
    }

    func resolveLocalMobilePage(
        _ tab: MobileTabRecord, outbound: inout CompanionMobilePublicationBatch
    ) throws -> TreeNode {
        guard let presenceID = tab.presenceID, let target = tab.sharedTarget else {
            throw MobileSharedCaptureError.deferred
        }
        let id = tab.treeNodeID ?? SharedTabContract.localPageID(device: localDeviceID, presence: presenceID)
        guard id.rawValue != tab.id, id.rawValue != presenceID.rawValue else {
            throw MobileSharedCaptureError.identityConflict
        }
        if let existing = snapshot.treeNodes.first(where: { $0.id == id }) {
            guard !existing.isDeleted, existing.kind == .savedPage,
                  snapshot.workspaces.contains(where: { $0.id == existing.workspaceID && !$0.isDeleted }) else {
                throw MobileSharedCaptureError.deferred
            }
            try SharedSyncFormat.validate(existing.version, fields: CompanionFieldMerge.treeNodeFields)
            _ = try SharedTabURLGroup.of(existing)
            return existing // Capture is not a second navigation/title authority.
        }
        guard tab.treeNodeID == nil, tab.sharedBindingState == .unbound else {
            throw MobileSharedCaptureError.deferred
        }
        let workspaceID = try ensureMobileSharedWorkspace(tab.workspaceID, outbound: &outbound)
        let previousOrder = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == workspaceID && $0.parentID == nil
        }.max { $0.syncSortKey < $1.syncSortKey }?.orderKey
        let version = try nextVersion()
        let page = CompanionFieldMerge.stampLocal(previous: nil, candidate: try TreeNode(
            treeNodeID: id, workspaceID: workspaceID, kind: .savedPage,
            title: tab.effectiveTitle, url: target.url,
            orderKey: OrderKey.between(previousOrder, nil, tieBreaker: localDeviceID),
            isTemporary: !tab.isSaved, targetKind: target.kind, localScheme: target.localScheme,
            version: version
        ))
        snapshot.treeNodes.append(page)
        outbound.nodes.append(page)
        return page
    }

    func ensureMobileSharedWorkspace(
        _ requested: WorkspaceID?, outbound: inout CompanionMobilePublicationBatch
    ) throws -> WorkspaceID {
        let workspaceID = requested ?? SharedTabContract.inboxID
        if !snapshot.workspaces.contains(where: { $0.id == workspaceID && !$0.isDeleted }) {
            guard requested == nil, !snapshot.workspaces.contains(where: { $0.id == workspaceID }) else {
                throw MobileSharedCaptureError.deferred
            }
            let version = SyncVersion(modifiedAt: SharedTabContract.bottom, modifiedBy: SharedTabContract.systemActor)
                .normalized(for: CompanionFieldMerge.workspaceFields)
            let inbox = Workspace(workspaceID: workspaceID, name: "Inbox", icon: "", sortKey: "0",
                                  createdAt: SharedTabContract.bottom, modifiedAt: SharedTabContract.bottom,
                                  version: version)
            snapshot.workspaces.append(inbox)
            outbound.workspaces.append(inbox)
        }
        return workspaceID
    }
}
