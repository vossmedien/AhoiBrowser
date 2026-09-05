import Foundation
import WebKit
import AhoiCloudKitSpike

extension MobileBrowserController {
    public func localTabID(for treeNodeID: TreeNodeID) -> UUID? {
        tabs.first { $0.mode == .normal && $0.treeNodeID == treeNodeID }?.id
    }

    /// A shared page can have at most one runtime presence on this device.
    /// Its presence UUID must not collide with the shared CloudKit record name.
    @discardableResult
    public func bindTab(_ id: UUID, to treeNodeID: TreeNodeID) -> Bool {
        guard id != treeNodeID.rawValue,
              let index = tabs.firstIndex(where: { $0.id == id && $0.mode == .normal }),
              tabs[index].treeNodeID == nil || tabs[index].treeNodeID == treeNodeID,
              localTabID(for: treeNodeID).map({ $0 == id }) ?? true else { return false }
        guard tabs[index].treeNodeID != treeNodeID else { return true }
        tabs[index].treeNodeID = treeNodeID
        persistSoon()
        return true
    }

    /// Adds only a lightweight local reference. Incoming shared tabs must not
    /// instantiate WebKit pages, issue requests or change the current selection.
    @discardableResult
    func ensureSharedTab(_ node: TreeNode) -> UUID? {
        guard node.kind == .savedPage, !node.isDeleted else { return nil }
        let url = MobileTabRecord.normalizedURLString(node.url)
        guard url != nil || (node.isTemporary && node.url == nil) else { return nil }
        if let id = localTabID(for: node.id) { return id }

        let record = MobileTabRecord(
            workspaceID: node.workspaceID,
            treeNodeID: node.id,
            title: node.title,
            url: url,
            isSaved: !node.isTemporary
        )
        tabs.append(record)
        persistSoon()
        return record.id
    }

    /// Used for deliberate activation from the workspace tree or Library.
    /// Identity, not URL equality, decides whether an existing tab is focused.
    @discardableResult
    public func openSharedPage(_ node: TreeNode) -> UUID? {
        guard let id = ensureSharedTab(node),
              let index = tabs.firstIndex(where: { $0.id == id }) else { return nil }
        tabs[index].workspaceID = node.workspaceID
        tabs[index].title = MobileTabRecord.normalizedTitle(node.title)
        tabs[index].isSaved = !node.isTemporary
        if let value = MobileTabRecord.normalizedURLString(node.url),
           let url = URL(string: value), tabs[index].url != value {
            // Explicitly opening the saved entry restores its destination in
            // the bound runtime, rather than loading another site's stale URL.
            tabs[index].url = value
            tabs[index].faviconData = nil
            tabs[index].websiteTintARGB = nil
            if let page = pages[id] {
                observeNavigations(of: page, tabID: id)
                page.load(url)
            }
        }
        select(id)
        return id
    }

    /// Save the initiating tab, even if selection changes while local storage
    /// is awaited. Concurrent UI entry points share this per-tab guard.
    func saveSharedPage(
        for tabID: UUID,
        commit: @MainActor (MobileTabRecord, @MainActor (TreeNode) -> Void) async -> TreeNode?
    ) async -> MobileTabRecord? {
        guard let tab = tabs.first(where: { $0.id == tabID && $0.mode == .normal }),
              MobileTabRecord.normalizedURLString(tab.url) != nil,
              sharedPageSavesInFlight.insert(tabID).inserted else { return nil }
        defer { sharedPageSavesInFlight.remove(tabID) }
        guard let node = await commit(tab, { node in
            self.didCommitSavedPage(node, for: tabID)
        }) else { return nil }
        return tabs.first { $0.id == tabID && $0.mode == .normal && $0.treeNodeID == node.id }
    }

    /// Runs before the model publishes the newly saved row. Otherwise a click
    /// during outbound queuing could open a second, unbound runtime presence.
    private func didCommitSavedPage(_ node: TreeNode, for tabID: UUID) {
        guard node.kind == .savedPage, !node.isDeleted, !node.isTemporary else { return }
        if !tabs.contains(where: { $0.id == tabID }),
           var closed = recentlyClosedTab, closed.id == tabID, closed.mode == .normal,
           closed.treeNodeID == nil || closed.treeNodeID == node.id,
           closed.id != node.id.rawValue {
            closed.treeNodeID = node.id
            closed.workspaceID = node.workspaceID
            closed.isSaved = true
            recentlyClosedTab = closed
            return
        }
        guard bindTab(tabID, to: node.id),
              let index = tabs.firstIndex(where: { $0.id == tabID && $0.mode == .normal })
        else { return }
        tabs[index].workspaceID = node.workspaceID
        tabs[index].isSaved = true
        persistSoon()
    }
}
