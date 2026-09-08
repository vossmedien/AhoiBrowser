import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// The explicit local close commits before the runtime disappears. A close
    /// that overtakes initial publication creates the same stable tombstones,
    /// so a delayed capture cannot recreate the just-closed temporary page.
    func closeSharedMobileTab(_ tab: MobileTabRecord) async throws -> CompanionMobilePublicationBatch {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard tab.mode == .normal, tab.participatesInSharedTabs else { return .init() }
        let before = snapshot
        var committed = false
        defer { if !committed { snapshot = before } }
        var result = CompanionMobilePublicationBatch()
        let expectedID = tab.treeNodeID ?? tab.presenceID.map {
            SharedTabContract.localPageID(device: localDeviceID, presence: $0)
        }
        var page = expectedID.flatMap { id in snapshot.treeNodes.first { $0.id == id } }
        if page == nil, tab.treeNodeID == nil, tab.canPublishSharedTab {
            page = try resolveLocalMobilePage(tab, outbound: &result)
        }
        if var current = page, !current.isDeleted, current.isTemporary {
            let previous = current
            current.version = try nextVersion()
            current.tombstone = mobileTabTombstone(id: current.id.rawValue, version: current.version,
                                                  parent: current.parentID?.rawValue)
            current = CompanionFieldMerge.stampLocal(previous: previous, candidate: current)
            if let index = snapshot.treeNodes.firstIndex(where: { $0.id == current.id }) {
                snapshot.treeNodes[index] = current
            }
            result.nodes.removeAll { $0.id == current.id }
            result.nodes.append(current)
            page = current
        }
        if let presenceID = tab.presenceID, tab.hasDistinctSharedIdentities {
            let existing = snapshot.remoteTabs.first { $0.id == presenceID }
            if let existing {
                guard existing.deviceID == localDeviceID else { throw MobileSharedCaptureError.identityConflict }
                if !existing.isDeleted {
                    var closed = existing
                    closed.version = try nextVersion()
                    closed.isOpen = false
                    closed.tombstone = mobileTabTombstone(id: closed.id.rawValue, version: closed.version,
                                                          parent: closed.workspaceID?.rawValue)
                    closed = CompanionReadModelFieldMerge.stampLocal(previous: existing, candidate: closed)
                    result.tabs.append(try mergeTabIntoSnapshot(closed))
                }
            } else if let page, let device = snapshot.devices.first(where: { $0.id == localDeviceID }),
                      let session = snapshot.sessions.filter({ $0.deviceID == localDeviceID && !$0.isDeleted })
                        .max(by: { $0.lastActiveAt < $1.lastActiveAt }) {
                // Use the actual current logical target, never the older loaded
                // page's URL. Presence deletion does not require a live Page.
                let version = try nextVersion().normalized(for: SharedTabWireReadPolicy.remoteTabBaseFields)
                let closed = try RemoteTab(
                    tabID: presenceID, deviceID: localDeviceID, deviceKind: device.kind,
                    deviceName: device.name, sessionID: session.id, workspaceID: page.workspaceID,
                    treeNodeID: page.id, title: MobileTabRecord.normalizedTitle(page.title),
                    url: page.url ?? "", targetKind: page.targetKind, localScheme: page.localScheme,
                    lastActiveAt: version.modifiedAt, isOpen: false, pinned: !page.isTemporary,
                    version: version, tombstone: mobileTabTombstone(id: presenceID.rawValue,
                                                                   version: version, parent: page.workspaceID.rawValue)
                )
                result.tabs.append(try mergeTabIntoSnapshot(closed))
            }
        }
        if snapshot != before { try await persist() }
        committed = true
        return result
    }

    private func mobileTabTombstone(id: UUID, version: SyncVersion, parent: UUID?) -> Tombstone {
        .init(entityID: id, deletedAt: version.modifiedAt, deletedBy: localDeviceID,
              originalParentID: parent, originalOrderKey: nil,
              purgeAfterMilliseconds: version.modifiedAt.physicalMilliseconds + 2_592_000_000)
    }
}

extension CompanionAppModel {
    @discardableResult
    func closePublishedMobileTab(_ tab: MobileTabRecord, didCommit: @MainActor () -> Void) async -> Bool {
        await mobileSharedIntentTasks[tab.id]?.value
        return await performLocalFirstMutation({
            try await repository.closeSharedMobileTab(tab)
        }, didCommit: { _ in didCommit() }, enqueue: { records in
            guard let bridge = self.syncBridge else { return }
            try await records.enqueue(using: bridge)
        }) != nil
    }
}
