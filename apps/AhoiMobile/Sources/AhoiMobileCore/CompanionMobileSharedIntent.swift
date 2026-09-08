import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// Explicit local intent is the only runtime-to-Page mutation path.
    /// Passive capture uses the repository Page and cannot undo a peer's URL.
    func applyLocalSharedIntent(
        tab: MobileTabRecord, intent: MobileSharedTabIntent, mutationID: UUID,
        sessionID: DeviceSessionID, deviceName: String, deviceKind: DeviceKind
    ) async throws -> LocalMobileSharedCapture {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard tab.mode == .normal, tab.participatesInSharedTabs,
              tab.hasDistinctSharedIdentities, MobileTabRecord.isNonzeroUUID(mutationID) else {
            throw MobileSharedCaptureError.deferred
        }
        if snapshot.mobileAppliedIntents.contains(mutationID) {
            var result = LocalMobileSharedCapture()
            if let id = tab.treeNodeID, let node = snapshot.treeNodes.first(where: { $0.id == id && !$0.isDeleted }) {
                result.bindings[tab.id] = node
            }
            return result // Do not re-author after a domain->session-ACK crash.
        }
        let before = snapshot
        var committed = false
        defer { if !committed { snapshot = before } }
        var result = LocalMobileSharedCapture()
        let previous: TreeNode
        if let id = tab.treeNodeID {
            guard tab.sharedBindingState == .current,
                  let node = snapshot.treeNodes.first(where: { $0.id == id && !$0.isDeleted }),
                  node.kind == .savedPage,
                  snapshot.visibleWorkspaces.contains(where: { $0.id == node.workspaceID }) else {
                throw MobileSharedCaptureError.deferred
            }
            try SharedSyncFormat.validate(node.version, fields: CompanionFieldMerge.treeNodeFields)
            _ = try SharedTabURLGroup.of(node)
            previous = node
        } else {
            guard tab.canPublishSharedTab else { throw MobileSharedCaptureError.deferred }
            previous = try resolveLocalMobilePage(tab, outbound: &result.outbound)
        }
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
        if let presenceID = tab.presenceID {
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
        }
        snapshot.mobileAppliedIntents.insert(mutationID)
        try await persist()
        committed = true
        return result
    }
}

extension CompanionAppModel {
    func receiveSharedTabIntent(_ tab: MobileTabRecord, _ intent: MobileSharedTabIntent,
                                browser: MobileBrowserController) {
        guard let mobileSessionID, mobileSharedIntentTasks[tab.id] == nil,
              !tab.pendingSharedMutations.isEmpty else { return }
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
                if let browser { self.reconcileBrowserSharedProjection(browser, resumePending: false) }
            }
            while !Task.isCancelled {
                guard let browser, let current = browser.tabs.first(where: { $0.id == tab.id }),
                      current.mode == .normal, current.presenceID == tab.presenceID,
                      let mutation = current.pendingSharedMutations.first else { break }
                let result = await self.performLocalFirstMutation({
                    try await self.repository.applyLocalSharedIntent(
                        tab: current, intent: mutation.intent, mutationID: mutation.id,
                        sessionID: mobileSessionID, deviceName: self.mobileDeviceName, deviceKind: self.mobileDeviceKind
                    )
                }, didCommit: { result in
                    guard browser.tabs.first(where: { $0.id == current.id })?.presenceID == current.presenceID else { return }
                    browser.acknowledgeSharedMutation(tabID: current.id, presenceID: current.presenceID,
                                                       mutationID: mutation.id)
                    if let node = result.bindings[current.id] {
                        _ = browser.resolvePendingSharedBinding(current.id, node: node)
                    }
                }, ignoreFailure: {
                    $0 as? MobileSharedCaptureError == .deferred || $0 is CancellationError
                }, enqueue: { result in
                    guard let bridge else { return }
                    try await result.outbound.enqueue(using: bridge)
                })
                if result == nil { break } // Resume on a new domain/user event, not a retry loop.
            }
        }
    }

    func reconcileBrowserSharedProjection(_ browser: MobileBrowserController, resumePending: Bool = true) {
        let pending = Set(mobileSharedIntentTasks.keys).union(
            browser.normalTabs.filter { !$0.pendingSharedMutations.isEmpty }.map(\.id))
        var reserved = Set<TreeNodeID>()
        for tab in browser.normalTabs where tab.hasDistinctSharedIdentities {
            let id: TreeNodeID
            if let bound = tab.treeNodeID { id = bound }
            else if let presence = tab.presenceID {
                id = SharedTabContract.localPageID(device: repository.localDeviceID, presence: presence)
            } else { continue }
            reserved.insert(id)
            // Restore the exact pre-existing binding before passive projection
            // can create a competing dormant mirror for the same logical page.
            let matches = snapshot.treeNodes.filter { $0.id == id }
            if matches.count == 1, let node = matches.first {
                if !tab.pendingSharedMutations.isEmpty {
                    _ = browser.resolvePendingSharedBinding(tab.id, node: node)
                } else if tab.treeNodeID == nil {
                    _ = browser.bindTab(tab.id, to: node)
                }
            }
        }
        browser.reconcileSharedTabs(snapshot: snapshot, preservingRuntimeIDs: pending,
                                    reservingPageIDs: reserved)
        if resumePending {
            for tab in browser.normalTabs where !tab.pendingSharedMutations.isEmpty {
                guard let mutation = tab.pendingSharedMutations.first else { continue }
                receiveSharedTabIntent(tab, mutation.intent, browser: browser)
            }
        }
    }
}
