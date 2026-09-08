import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// The optional argument preserves the old source call shape only to reject
    /// unlinked calls explicitly. It is never an old-format writer/fallback.
    public func publishLocalMobileTab(
        tabID: UUID, sessionID: DeviceSessionID, deviceName: String, deviceKind: DeviceKind,
        workspaceID: WorkspaceID?, title: String, url: String, pinned: Bool,
        treeNodeID: TreeNodeID? = nil
    ) async throws -> LocalMobileTabPublication {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard let treeNodeID,
              let page = snapshot.treeNodes.first(where: { $0.id == treeNodeID && !$0.isDeleted }),
              page.kind == .savedPage,
              treeNodeID.rawValue != tabID else { throw SharedTabTargetError.missingPageLink }
        let before = snapshot
        var committed = false
        defer { if !committed { snapshot = before } }
        let session = try publishLocalMobileSessionInSnapshot(
            sessionID: sessionID, deviceName: deviceName,
            deviceKind: deviceKind, workspaceID: page.workspaceID
        )
        // Presentation hints from a loaded web view are not Page authority.
        let tab = try publishLinkedPresenceInSnapshot(
            id: TabID(rawValue: tabID), page: page, sessionID: sessionID,
            deviceName: deviceName, deviceKind: deviceKind
        )
        try await persist()
        committed = true
        return .init(device: session.device, session: session.session, tab: tab)
    }

    func publishLinkedPresenceInSnapshot(
        id: TabID, page: TreeNode, sessionID: DeviceSessionID,
        deviceName: String, deviceKind: DeviceKind
    ) throws -> RemoteTab {
        guard page.kind == .savedPage, !page.isDeleted, id.rawValue != page.id.rawValue else {
            throw SharedTabTargetError.missingPageLink
        }
        _ = try SharedTabURLGroup.of(page)
        try SharedSyncFormat.validate(page.version, fields: CompanionFieldMerge.treeNodeFields)
        let previous = snapshot.remoteTabs.first { $0.id == id }
        guard previous.map({ $0.deviceID == localDeviceID && $0.sessionID == sessionID && !$0.isDeleted }) ?? true else {
            throw LocalCompanionStoreError.invalidSnapshot
        }
        let title = MobileTabRecord.normalizedTitle(page.title)
        if let previous, previous.treeNodeID == page.id,
           previous.workspaceID == page.workspaceID, previous.url == page.url ?? "",
           previous.targetKind == page.targetKind, previous.localScheme == page.localScheme,
           previous.title == title, previous.pinned == !page.isTemporary {
            return previous
        }
        let version = try nextVersion().normalized(for: SharedTabWireReadPolicy.remoteTabBaseFields)
        var candidate = try RemoteTab(
            tabID: id, deviceID: localDeviceID, deviceKind: deviceKind,
            deviceName: deviceName, sessionID: sessionID, workspaceID: page.workspaceID,
            treeNodeID: page.id,
            workspaceName: snapshot.workspaces.first(where: { $0.id == page.workspaceID })?.name,
            title: title, url: page.url ?? "", targetKind: page.targetKind, localScheme: page.localScheme,
            openedAt: previous?.openedAt ?? version.modifiedAt, lastActiveAt: version.modifiedAt,
            pinned: !page.isTemporary, version: version
        )
        if let previous {
            candidate = CompanionReadModelFieldMerge.stampLocal(previous: previous, candidate: candidate)
        }
        return try mergeTabIntoSnapshot(candidate)
    }
}
