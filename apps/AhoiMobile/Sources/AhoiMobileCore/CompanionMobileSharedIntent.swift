import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// Explicit local intent is the only runtime-to-Page mutation path.
    /// Passive capture uses the repository Page and cannot undo a peer's URL.
    func applyLocalSharedIntent(
        tab: MobileTabRecord, intent: MobileSharedTabIntent,
        sessionID: DeviceSessionID, deviceName: String, deviceKind: DeviceKind
    ) async throws -> LocalMobileSharedCapture {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard tab.mode == .normal, tab.canPublishSharedTab, let presenceID = tab.presenceID else {
            throw MobileSharedCaptureError.deferred
        }
        let before = snapshot
        var committed = false
        defer { if !committed { snapshot = before } }
        var result = LocalMobileSharedCapture()
        let previous = try resolveLocalMobilePage(tab, outbound: &result.outbound)
        var page = previous
        switch intent {
        case .navigate(let target):
            try target.validatePage(isTemporary: page.isTemporary)
            page.url = target.url
            page.targetKind = target.kind
            page.localScheme = target.localScheme
            let title = MobileTabRecord.normalizedTitle(tab.effectiveTitle)
            if !title.isEmpty { page.title = title }
            else if let host = URL(string: target.url)?.host {
                page.title = host
            }
        case .move(let requested):
            let workspace = try ensureMobileSharedWorkspace(requested, outbound: &result.outbound)
            if page.workspaceID != workspace {
                let last = snapshot.visibleTreeNodes.filter {
                    $0.workspaceID == workspace && $0.parentID == nil && $0.id != page.id
                }.max { $0.syncSortKey < $1.syncSortKey }
                page.workspaceID = workspace
                page.parentID = nil
                page.orderKey = try OrderKey.between(last?.orderKey, nil, tieBreaker: localDeviceID)
                page.wireSortKey = nil
            }
        case .rename(let title):
            page.title = MobileTabRecord.normalizedTitle(title ?? tab.title)
        }
        if page != previous {
            page.version = try nextVersion()
            page = CompanionFieldMerge.stampLocal(previous: previous, candidate: page)
            guard let index = snapshot.treeNodes.firstIndex(where: { $0.id == page.id }) else {
                throw LocalCompanionStoreError.notFound
            }
            snapshot.treeNodes[index] = page
            result.outbound.nodes.removeAll { $0.id == page.id }
            result.outbound.nodes.append(page)
        }
        result.bindings[tab.id] = page
        let session = try publishLocalMobileSessionInSnapshot(
            sessionID: sessionID, deviceName: deviceName, deviceKind: deviceKind, workspaceID: page.workspaceID
        )
        result.outbound.devices.append(session.device)
        result.outbound.sessions.append(session.session)
        let oldPresence = snapshot.remoteTabs.first { $0.id == presenceID }
        let presence = try publishLinkedPresenceInSnapshot(
            id: presenceID, page: page, sessionID: sessionID, deviceName: deviceName, deviceKind: deviceKind
        )
        if oldPresence != presence { result.outbound.tabs.append(presence) }
        try await persist()
        committed = true
        return result
    }
}

extension CompanionAppModel {
    func receiveSharedTabIntent(_ tab: MobileTabRecord, _ intent: MobileSharedTabIntent,
                                browser: MobileBrowserController) {
        guard let mobileSessionID else { return }
        let previous = mobileSharedIntentTasks[tab.id]
        let bridge = syncBridge
        let token = UUID()
        mobileSharedIntentTokens[tab.id] = token
        mobileSharedIntentTasks[tab.id] = Task { [weak self, weak browser] in
            guard let self else { return }
            defer {
                if self.mobileSharedIntentTokens[tab.id] == token {
                    self.mobileSharedIntentTokens.removeValue(forKey: tab.id)
                    self.mobileSharedIntentTasks.removeValue(forKey: tab.id)
                }
                if let browser { self.reconcileBrowserSharedProjection(browser) }
            }
            _ = await self.performLocalFirstMutation({
                await previous?.value
                return try await self.repository.applyLocalSharedIntent(
                    tab: tab, intent: intent, sessionID: mobileSessionID,
                    deviceName: self.mobileDeviceName, deviceKind: self.mobileDeviceKind
                )
            }, didCommit: { result in
                guard self.mobileSharedIntentTokens[tab.id] == token,
                      let browser, let node = result.bindings[tab.id],
                      browser.tabs.first(where: { $0.id == tab.id })?.presenceID == tab.presenceID else { return }
                _ = browser.bindTab(tab.id, to: node)
            }, ignoreFailure: { $0 as? MobileSharedCaptureError == .deferred }, enqueue: { result in
                guard let bridge else { return }
                try await result.outbound.enqueue(using: bridge)
            })
        }
    }

    func reconcileBrowserSharedProjection(_ browser: MobileBrowserController) {
        let pending = Set(mobileSharedIntentTasks.keys)
        var reserved = Set<TreeNodeID>()
        for tab in browser.normalTabs where tab.treeNodeID == nil && tab.hasDistinctSharedIdentities {
            guard let presence = tab.presenceID else { continue }
            let id = SharedTabContract.localPageID(device: repository.localDeviceID, presence: presence)
            reserved.insert(id)
            // Restore the exact pre-existing binding before passive projection
            // can create a competing dormant mirror for the same logical page.
            let matches = snapshot.treeNodes.filter { $0.id == id && !$0.isDeleted }
            if !pending.contains(tab.id), matches.count == 1, let node = matches.first {
                _ = browser.bindTab(tab.id, to: node)
            }
        }
        browser.reconcileSharedTabs(snapshot: snapshot, preservingRuntimeIDs: pending,
                                    reservingPageIDs: reserved)
    }
}
