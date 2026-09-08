import Foundation
import WebKit
import AhoiCloudKitSpike

extension MobileBrowserController {
    public func undoClose() {
        guard var record = recentlyClosedTab, record.mode == .normal else {
            recentlyClosedTab = nil
            return
        }
        record.pendingSharedMutations = [] // Reopen is not a replay of pre-close mutation IDs.
        if let nodeID = record.treeNodeID, let existingID = localTabID(for: nodeID),
           let existing = tabs.first(where: { $0.id == existingID }) {
            if existing.presenceID != nil || pages[existingID] != nil {
                recentlyClosedTab = nil
                select(existingID)
                return
            }
            // Replace only a dormant mirror; never discard another running page.
            tabs.removeAll { $0.id == existingID }
        }
        // Closed Presence IDs remain tombstoned. Explicit undo creates a fresh
        // Presence, and a closed temporary page receives a new global identity.
        record.presenceID = TabID(rawValue: freshSharedUUID(excluding: [record.id]))
        if !record.isSaved || record.sharedBindingState == .deleted {
            record.treeNodeID = nil
            record.isSaved = false
            record.sharedBindingState = .unbound
            if let url = record.url {
                record.sharedTarget = try? SharedTabTarget(kind: .web, url: url)
            } else if record.sharedTarget?.kind != .localOnly {
                record.sharedTarget = try? SharedTabTarget(kind: .newTab, url: "")
            }
        } else {
            record.sharedBindingState = .deferred
        }
        record.participatesInSharedTabs = true
        record.lastActiveAt = Date()
        guard sharingIdentifiersAreUnique(for: record, in: tabs) else { return }
        tabs.append(record)
        selectedTabID = record.id
        recentlyClosedTab = nil
        if let value = record.url, let url = URL(string: value) {
            let restoredPage = makePage(tabID: record.id, mode: record.mode)
            pages[record.id] = restoredPage
            observeNavigations(of: restoredPage, tabID: record.id)
            restoredPage.load(url)
        }
        discardInactivePages(keeping: 5)
        persistSoon()
    }

    public func localTabID(for treeNodeID: TreeNodeID) -> UUID? {
        tabs.first { $0.mode == .normal && $0.treeNodeID == treeNodeID }?.id
    }

    /// ID-only binding is provisional until the current domain node is read back.
    /// It cannot allocate a Presence or infer a logical target from a runtime URL.
    @discardableResult
    public func bindTab(_ id: UUID, to treeNodeID: TreeNodeID) -> Bool {
        guard let index = tabs.firstIndex(where: { $0.id == id && $0.mode == .normal }),
              tabs[index].presenceID != nil,
              tabs[index].treeNodeID == nil || tabs[index].treeNodeID == treeNodeID,
              tabs[index].sharedBindingState != .deleted else { return false }
        var candidate = tabs[index]
        if candidate.treeNodeID != treeNodeID {
            candidate.treeNodeID = treeNodeID
            candidate.sharedBindingState = .deferred
        }
        guard sharingIdentifiersAreUnique(for: candidate, in: tabs) else { return false }
        guard candidate != tabs[index] else { return true }
        tabs[index] = candidate
        persistSoon()
        return true
    }

    /// Explicit local activation/save binds against current domain metadata.
    /// The caller must supply the repository's current node, not an old UI copy.
    @discardableResult
    public func bindTab(_ id: UUID, to node: TreeNode) -> Bool {
        guard !node.isDeleted, let target = validatedSharedTarget(for: node),
              let index = tabs.firstIndex(where: { $0.id == id && $0.mode == .normal }),
              tabs[index].treeNodeID == nil || tabs[index].treeNodeID == node.id else { return false }
        var candidate = tabs[index]
        candidate.treeNodeID = node.id
        if candidate.presenceID == nil {
            candidate.presenceID = TabID(rawValue: freshSharedUUID(excluding: [node.id.rawValue]))
        }
        guard sharingIdentifiersAreUnique(for: candidate, in: tabs) else { return false }
        updateSharedMetadata(&candidate, from: node, target: target)
        candidate.participatesInSharedTabs = true
        guard candidate != tabs[index] else { return true }
        tabs[index] = candidate
        persistSoon()
        return true
    }

    /// Passive projection only. Missing rows are not deletions, and even a real
    /// tombstone preserves local runtime work until a safe close flow exists.
    /// No URL, WebKit instance, selection, activation time or ordering is changed.
    public func reconcileSharedTabs(snapshot: CompanionSnapshot, preservingRuntimeIDs: Set<UUID> = [],
                                    reservingPageIDs: Set<TreeNodeID> = []) {
        let nodes = Dictionary(grouping: snapshot.treeNodes, by: \.id)
        var updated = tabs
        for index in updated.indices where updated[index].mode == .normal {
            if preservingRuntimeIDs.contains(updated[index].id) { continue }
            guard let nodeID = updated[index].treeNodeID else { continue }
            guard sharingIdentifiersAreUnique(for: updated[index], in: tabs),
                  let matches = nodes[nodeID], matches.count == 1,
                  let node = matches.first,
                  let target = validatedSharedTarget(for: node) else {
                if updated[index].sharedBindingState != .deleted {
                    updated[index].sharedBindingState = .deferred
                }
                continue
            }
            if node.isDeleted {
                updated[index].sharedBindingState = .deleted
            } else {
                updateSharedMetadata(&updated[index], from: node, target: target)
            }
        }
        // Append only; existing local order/scroll anchors remain stable. A
        // dormant mirror reserves no local Presence until deliberate activation.
        let boundIDs = Set(updated.compactMap(\.treeNodeID))
        for node in snapshot.visibleTreeNodes where !boundIDs.contains(node.id) && !reservingPageIDs.contains(node.id) {
            guard nodes[node.id]?.count == 1,
                  let target = validatedSharedTarget(for: node) else { continue }
            let record = dormantSharedTab(node, target: target, records: updated)
            guard sharingIdentifiersAreUnique(for: record, in: updated) else { continue }
            updated.append(record)
        }
        if var closed = recentlyClosedTab, closed.mode == .normal, let nodeID = closed.treeNodeID {
            if let matches = nodes[nodeID], matches.count == 1, let node = matches.first,
               let target = validatedSharedTarget(for: node) {
                if node.isDeleted { closed.sharedBindingState = .deleted }
                else { updateSharedMetadata(&closed, from: node, target: target) }
            } else if closed.sharedBindingState != .deleted {
                closed.sharedBindingState = .deferred
            }
            if closed != recentlyClosedTab { recentlyClosedTab = closed }
        }
        guard updated != tabs else { return }
        tabs = updated
        persistSoon()
    }

    /// Adds only a dormant local reference, including unavailable local-only
    /// targets. It never fabricates an executable URL or a local Presence.
    @discardableResult
    func ensureSharedTab(_ node: TreeNode) -> UUID? {
        guard !node.isDeleted, let target = validatedSharedTarget(for: node) else { return nil }
        if let id = localTabID(for: node.id) { return id }
        let record = dormantSharedTab(node, target: target, records: tabs)
        guard sharingIdentifiersAreUnique(for: record, in: tabs) else { return nil }
        tabs.append(record)
        persistSoon()
        return record.id
    }

    /// Used for deliberate activation from the workspace tree or Library.
    /// Identity, not URL equality, decides whether an existing tab is focused.
    @discardableResult
    public func openSharedPage(_ node: TreeNode) -> UUID? {
        guard !node.isDeleted, let target = validatedSharedTarget(for: node),
              let id = ensureSharedTab(node),
              let index = tabs.firstIndex(where: { $0.id == id }) else { return nil }
        let destination: URL?
        switch target.kind {
        case .localOnly:
            return nil // The logical row remains; this device has no executable target.
        case .newTab:
            // Replacing a live page with a blank surface needs a separate safe
            // before-unload flow. Do not destroy potentially unsaved work here.
            guard tabs[index].url == nil, pages[id] == nil else { return nil }
            destination = nil
        case .web:
            guard let value = MobileTabRecord.normalizedURLString(target.url),
                  let url = URL(string: value) else { return nil }
            destination = url
        }
        guard bindTab(id, to: node) else { return nil }
        if let destination, tabs[index].url != destination.absoluteString {
            // Only this deliberate activation updates local navigation state.
            tabs[index].url = destination.absoluteString
            tabs[index].faviconData = nil
            tabs[index].websiteTintARGB = nil
            if let page = pages[id] {
                observeNavigations(of: page, tabID: id)
                page.load(destination)
            }
        }
        select(id)
        return id
    }

    func prepareExplicitSharedNavigation(tabID: UUID, url: URL) {
        guard tabs.contains(where: { $0.id == tabID && $0.mode == .normal }),
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else { return }
        sharedNavigationRequests[tabID] = url
    }

    func noteAllowedSharedNavigation(tabID: UUID, url: URL, kind: WKNavigationType) {
        switch kind {
        case .linkActivated, .backForward, .formSubmitted, .formResubmitted:
            prepareExplicitSharedNavigation(tabID: tabID, url: url)
        default:
            // Keep a matching address-bar/load intent, not a stale cancelled
            // click across a later automatic restore or unrelated navigation.
            if sharedNavigationRequests[tabID] != url {
                sharedNavigationRequests.removeValue(forKey: tabID)
            }
        }
    }

    func beginSharedNavigation(tabID: UUID, generation: UInt64) {
        if sharedNavigationRequests.removeValue(forKey: tabID) != nil {
            sharedNavigationGenerations[tabID] = generation
        } else {
            sharedNavigationGenerations.removeValue(forKey: tabID)
        }
    }

    func clearSharedNavigation(for tabID: UUID) {
        sharedNavigationRequests.removeValue(forKey: tabID)
        sharedNavigationGenerations.removeValue(forKey: tabID)
    }

    func cancelSharedNavigation(for tabID: UUID, failedURL: URL?) {
        sharedNavigationGenerations.removeValue(forKey: tabID)
        if let failedURL, sharedNavigationRequests[tabID] == failedURL {
            sharedNavigationRequests.removeValue(forKey: tabID)
        }
        // A cancellation of A can arrive after the policy accepted a newer B.
        // Its pending request belongs to B, not to the cancelled document.
    }

    /// Only a committed (or explicitly failed) user navigation authors a Page.
    /// Passive restore/metadata and automatic recovery do not acquire authority.
    func commitSharedNavigation(tabID: UUID, generation: UInt64, url: URL?, title: String?) {
        guard sharedNavigationGenerations[tabID] == generation, let url else { return }
        noteExplicitSharedNavigation(tabID: tabID, url: url, title: title)
    }

    private func noteExplicitSharedNavigation(tabID: UUID, url: URL, title: String?) {
        guard let index = tabs.firstIndex(where: { $0.id == tabID && $0.mode == .normal }),
              tabs[index].sharedBindingState != .deleted,
              let value = MobileTabRecord.normalizedURLString(url.absoluteString),
              let target = try? SharedTabTarget(kind: .web, url: value) else { return }
        var candidate = tabs[index]
        let targetChanged = candidate.sharedTarget != target
        candidate.url = value
        if let title, !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            candidate.title = MobileTabRecord.normalizedTitle(title)
        }
        if candidate.presenceID == nil {
            candidate.presenceID = TabID(rawValue: freshSharedUUID())
        }
        guard sharingIdentifiersAreUnique(for: candidate, in: tabs) else { return }
        candidate.sharedTarget = target
        candidate.participatesInSharedTabs = true
        if targetChanged { candidate.stageSharedIntent(.navigate(target)) }
        else if candidate.customTitle == nil, title != nil { candidate.stageSharedIntent(.rename(nil)) }
        if candidate != tabs[index] {
            tabs[index] = candidate
            persistSoon()
        }
        // This is the final action: the callback may close/reorder/select tabs.
        if targetChanged { onSharedTabIntent?(candidate, .navigate(target)) }
        else if candidate.customTitle == nil, title != nil {
            onSharedTabIntent?(candidate, .rename(nil))
        }
    }

    private func validatedSharedTarget(for node: TreeNode) -> SharedTabTarget? {
        guard node.kind == .savedPage, MobileTabRecord.isNonzeroUUID(node.id.rawValue),
              MobileTabRecord.isNonzeroUUID(node.workspaceID.rawValue),
              node.parentID != node.id,
              node.parentID.map({ MobileTabRecord.isNonzeroUUID($0.rawValue) }) ?? true,
              (try? SharedSyncFormat.validate(node.version,
                  fields: SharedTabWireReadPolicy.treeNodeBaseFields)) != nil,
              let kind = node.targetKind,
              let target = try? SharedTabTarget(kind: kind, url: node.url ?? "", localScheme: node.localScheme),
              (try? target.validatePage(isTemporary: node.isTemporary)) != nil else { return nil }
        return target
    }

    private func updateSharedMetadata(_ tab: inout MobileTabRecord, from node: TreeNode,
                                      target: SharedTabTarget) {
        tab.workspaceID = node.workspaceID
        if let customTitle = tab.customTitle, customTitle != node.title { tab.customTitle = nil }
        tab.title = MobileTabRecord.normalizedTitle(node.title)
        tab.isSaved = !node.isTemporary
        tab.sharedTarget = target
        tab.sharedBindingState = .current
    }

    /// Resolve metadata identity without erasing an uncommitted local intent.
    /// No runtime Presence is allocated for a dormant row, and nothing loads.
    func resolvePendingSharedBinding(_ id: UUID, node: TreeNode) -> Bool {
        guard let target = validatedSharedTarget(for: node),
              let index = tabs.firstIndex(where: { $0.id == id && $0.mode == .normal }),
              tabs[index].treeNodeID == nil || tabs[index].treeNodeID == node.id else { return false }
        var candidate = tabs[index]
        if node.isDeleted {
            candidate.sharedBindingState = .deleted
            if candidate != tabs[index] { tabs[index] = candidate; persistSoon() }
            return false
        }
        let fields = Set(candidate.pendingSharedMutations.map(\.field))
        candidate.treeNodeID = node.id
        candidate.sharedBindingState = .current
        candidate.isSaved = !node.isTemporary
        if !fields.contains("url") { candidate.sharedTarget = target }
        if !fields.contains("location") { candidate.workspaceID = node.workspaceID }
        if !fields.contains("title") { candidate.title = node.title }
        guard sharingIdentifiersAreUnique(for: candidate, in: tabs) else { return false }
        if candidate != tabs[index] { tabs[index] = candidate; persistSoon() }
        return true
    }

    func acknowledgeSharedMutation(tabID: UUID, presenceID: TabID?, mutationID: UUID) {
        guard let index = tabs.firstIndex(where: {
            $0.id == tabID && $0.mode == .normal && $0.presenceID == presenceID
        }), tabs[index].pendingSharedMutations.contains(where: { $0.id == mutationID }) else { return }
        tabs[index].pendingSharedMutations.removeAll { $0.id == mutationID }
        persistSoon()
    }

    private func dormantSharedTab(_ node: TreeNode, target: SharedTabTarget,
                                  records: [MobileTabRecord]) -> MobileTabRecord {
        MobileTabRecord(
            id: freshSharedUUID(excluding: [node.id.rawValue], records: records),
            workspaceID: node.workspaceID,
            treeNodeID: node.id,
            title: node.title,
            isSaved: !node.isTemporary,
            presenceID: nil,
            sharedTarget: target,
            sharedBindingState: .current
        )
    }

    private func freshSharedUUID(excluding extra: Set<UUID> = [],
                                 records: [MobileTabRecord]? = nil) -> UUID {
        let records = records ?? tabs
        var reserved = Set(records.map(\.id)).union(extra)
        reserved.formUnion(records.compactMap { $0.presenceID?.rawValue })
        reserved.formUnion(records.compactMap { $0.treeNodeID?.rawValue })
        var result = UUID()
        while !MobileTabRecord.isNonzeroUUID(result) || reserved.contains(result) { result = UUID() }
        return result
    }

    private func sharingIdentifiersAreUnique(for candidate: MobileTabRecord,
                                              in records: [MobileTabRecord]) -> Bool {
        guard candidate.hasDistinctSharedIdentities,
              records.filter({ $0.id == candidate.id }).count <= 1 else { return false }
        let own = Set([candidate.id, candidate.presenceID?.rawValue,
                       candidate.treeNodeID?.rawValue].compactMap { $0 })
        return records.allSatisfy { other in
            other.id == candidate.id || own.isDisjoint(with:
                [other.id, other.presenceID?.rawValue, other.treeNodeID?.rawValue].compactMap { $0 })
        }
    }

    /// Save the initiating tab, even if selection changes while local storage
    /// is awaited. Concurrent UI entry points share this per-tab guard.
    func saveSharedPage(
        for tabID: UUID,
        commit: @MainActor (MobileTabRecord, @MainActor (TreeNode) -> Void) async -> TreeNode?
    ) async -> MobileTabRecord? {
        await setSharedPagePersistence(for: tabID, saved: true, commit: commit)
    }

    func setSharedPagePersistence(
        for tabID: UUID, saved: Bool,
        commit: @MainActor (MobileTabRecord, @MainActor (TreeNode) -> Void) async -> TreeNode?
    ) async -> MobileTabRecord? {
        guard let tab = tabs.first(where: { $0.id == tabID && $0.mode == .normal }),
              tab.sharedBindingState != .deferred, tab.sharedBindingState != .deleted,
              tab.treeNodeID != nil || MobileTabRecord.normalizedURLString(tab.url) != nil,
              sharedPageSavesInFlight.insert(tabID).inserted else { return nil }
        defer { sharedPageSavesInFlight.remove(tabID) }
        guard let node = await commit(tab, { node in
            self.didCommitPagePersistence(node, for: tab)
        }) else { return nil }
        return tabs.first {
            $0.id == tabID && $0.mode == .normal && $0.treeNodeID == node.id &&
                $0.presenceID == tab.presenceID && $0.isSaved == saved
        }
    }

    /// Runs before the model publishes the newly saved row. Otherwise a click
    /// during outbound queuing could open a second, unbound runtime presence.
    private func didCommitPagePersistence(_ node: TreeNode, for original: MobileTabRecord) {
        let tabID = original.id
        guard !node.isDeleted,
              let target = validatedSharedTarget(for: node) else { return }
        if !tabs.contains(where: { $0.id == tabID }),
           var closed = recentlyClosedTab, closed.id == tabID, closed.mode == .normal,
           closed.presenceID == original.presenceID,
           closed.treeNodeID == nil || closed.treeNodeID == node.id {
            closed.treeNodeID = node.id
            guard sharingIdentifiersAreUnique(for: closed, in: tabs) else { return }
            updateSharedMetadata(&closed, from: node, target: target)
            recentlyClosedTab = closed
            return
        }
        guard let index = tabs.firstIndex(where: {
            $0.id == tabID && $0.mode == .normal && $0.presenceID == original.presenceID
        }) else { return }
        if original.presenceID == nil {
            // Editing a dormant shared row must not create a runtime Presence,
            // instantiate WebKit or select/load the page.
            guard tabs[index].treeNodeID == node.id else { return }
            var candidate = tabs[index]
            updateSharedMetadata(&candidate, from: node, target: target)
            tabs[index] = candidate
            persistSoon()
            return
        }
        _ = bindTab(tabID, to: node)
    }
}
