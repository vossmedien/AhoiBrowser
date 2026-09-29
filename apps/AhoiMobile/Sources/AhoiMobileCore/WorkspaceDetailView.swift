import Foundation
import SwiftUI
import AhoiCloudKitSpike

// A Workspace's saved tree in the Library, with its rows (split from
// CompanionViews.swift, source line budget).
public struct WorkspaceDetailView: View {
    public let workspace: Workspace
    public let nodes: [TreeNode]
    public let tabs: [RemoteTab]
    public let moveTargets: [CompanionTreeMoveTarget]
    public let actionableTabIDs: Set<TabID>
    public let openURL: OpenURLAction
    public let onOpenTreeNode: ((TreeNodeID) -> Void)?
    public let remoteControlAvailable: Bool
    public let onRemoteOpen: ((RemoteTab) -> Void)?
    public let onRemoteFocus: ((RemoteTab) -> Void)?
    public let onRemoteClose: ((RemoteTab) -> Void)?
    public let onDeleteNode: ((TreeNode) -> Void)?
    public let onRenameNode: ((TreeNode, String) -> Void)?
    public let onMoveNode: ((TreeNode, CompanionTreeMoveTarget) -> Void)?
    public let onReorderNode: ((TreeNode, TreeNodeID?) -> Void)?
    @State private var nodePendingRename: TreeNode?
    @State private var nodePendingDeletion: TreeNode?
    @State private var renameDraft = ""

    public init(
        workspace: Workspace,
        nodes: [TreeNode],
        tabs: [RemoteTab],
        moveTargets: [CompanionTreeMoveTarget] = [],
        actionableTabIDs: Set<TabID> = [],
        openURL: OpenURLAction,
        onOpenTreeNode: ((TreeNodeID) -> Void)? = nil,
        remoteControlAvailable: Bool = false,
        onRemoteOpen: ((RemoteTab) -> Void)? = nil,
        onRemoteFocus: ((RemoteTab) -> Void)? = nil,
        onRemoteClose: ((RemoteTab) -> Void)? = nil,
        onDeleteNode: ((TreeNode) -> Void)? = nil,
        onRenameNode: ((TreeNode, String) -> Void)? = nil,
        onMoveNode: ((TreeNode, CompanionTreeMoveTarget) -> Void)? = nil,
        onReorderNode: ((TreeNode, TreeNodeID?) -> Void)? = nil
    ) {
        self.workspace = workspace
        self.nodes = nodes
        self.tabs = tabs
        self.moveTargets = moveTargets
        self.actionableTabIDs = actionableTabIDs
        self.openURL = openURL
        self.onOpenTreeNode = onOpenTreeNode
        self.remoteControlAvailable = remoteControlAvailable
        self.onRemoteOpen = onRemoteOpen
        self.onRemoteFocus = onRemoteFocus
        self.onRemoteClose = onRemoteClose
        self.onDeleteNode = onDeleteNode
        self.onRenameNode = onRenameNode
        self.onMoveNode = onMoveNode
        self.onReorderNode = onReorderNode
    }

    public var body: some View {
        List {
            Section(workspace.name) {
                ForEach(orderedNodes) { item in
                    TreeNodeRow(
                        node: item.node, depth: item.depth,
                        openURL: openURL, onOpenTreeNode: onOpenTreeNode
                    )
                        .contextMenu {
                            Button(L("action.rename", "Rename")) {
                                renameDraft = item.node.title
                                nodePendingRename = item.node
                            }
                            .disabled(onRenameNode == nil)
                            .accessibilityIdentifier(
                                "browser.library.node.rename.\(stableUUID(item.node.id.rawValue))"
                            )
                            Menu(CompanionL10n.string(
                                "tree.move",
                                fallback: "Move to"
                            )) {
                                ForEach(moveTargets) { target in
                                    Button(target.label) {
                                        onMoveNode?(item.node, target)
                                    }
                                    .disabled(isInvalidMoveTarget(target, for: item.node))
                                    .accessibilityIdentifier(
                                        "browser.library.node.move-target.\(stableMoveTarget(target))"
                                    )
                                }
                            }
                            .disabled(onMoveNode == nil)
                            .accessibilityIdentifier(
                                "browser.library.node.move.\(stableUUID(item.node.id.rawValue))"
                            )
                            Button(L("tree.move_up", "Move up")) {
                                onReorderNode?(
                                    item.node,
                                    successorWhenMovingUp(item.node)
                                )
                            }
                            .disabled(!canMoveUp(item.node) || onReorderNode == nil)
                            .accessibilityIdentifier(
                                "browser.library.node.move-up.\(stableUUID(item.node.id.rawValue))"
                            )
                            Button(L("tree.move_down", "Move down")) {
                                onReorderNode?(
                                    item.node,
                                    successorWhenMovingDown(item.node)
                                )
                            }
                            .disabled(!canMoveDown(item.node) || onReorderNode == nil)
                            .accessibilityIdentifier(
                                "browser.library.node.move-down.\(stableUUID(item.node.id.rawValue))"
                            )
                            Button(L("action.delete", "Delete"), role: .destructive) {
                                nodePendingDeletion = item.node
                            }
                            .disabled(onDeleteNode == nil)
                            .accessibilityIdentifier(
                                "browser.library.node.delete.\(stableUUID(item.node.id.rawValue))"
                            )
                        }
                }
            }
            if !tabs.isEmpty {
                Section(L("workspace.normal_device_tabs", "Normal tabs on devices")) {
                    ForEach(tabs) { tab in
                        RemoteTabRow(
                            tab: tab,
                            openURL: openURL,
                            remoteControlAvailable: remoteControlAvailable &&
                                actionableTabIDs.contains(tab.id),
                            onRemoteOpen: onRemoteOpen.map { action in
                                { action(tab) }
                            },
                            onRemoteFocus: onRemoteFocus.map { action in
                                { action(tab) }
                            },
                            onRemoteClose: onRemoteClose.map { action in
                                { action(tab) }
                            }
                        )
                    }
                }
            }
        }
        .accessibilityIdentifier(
            "browser.library.workspace-detail.\(stableUUID(workspace.id.rawValue))"
        )
        .navigationTitle(workspace.name)
        .alert(
            L("tree.rename", "Rename item"),
            isPresented: Binding(
                get: { nodePendingRename != nil },
                set: { if !$0 { nodePendingRename = nil } }
            )
        ) {
            TextField(L("field.name", "Name"), text: $renameDraft)
                .accessibilityIdentifier("browser.library.node.rename.field")
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                nodePendingRename = nil
            }
            .accessibilityIdentifier("browser.library.node.rename.cancel")
            Button(L("action.save", "Save")) {
                guard let node = nodePendingRename else { return }
                onRenameNode?(node, renameDraft)
                nodePendingRename = nil
            }
            .accessibilityIdentifier("browser.library.node.rename.save")
        }
        .confirmationDialog(
            nodePendingDeletion.map {
                CompanionL10n.format(
                    "tree.delete.confirmation",
                    fallback: "Delete %@ and its contents?",
                    $0.title
                )
            } ?? "",
            isPresented: Binding(
                get: { nodePendingDeletion != nil },
                set: { if !$0 { nodePendingDeletion = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button(L("action.delete", "Delete"), role: .destructive) {
                guard let node = nodePendingDeletion else { return }
                nodePendingDeletion = nil
                onDeleteNode?(node)
            }
            .accessibilityIdentifier("browser.library.node.delete.confirm")
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                nodePendingDeletion = nil
            }
            .accessibilityIdentifier("browser.library.node.delete.cancel")
        } message: {
            Text(CompanionL10n.string(
                "tree.delete.message",
                fallback: "Deleted items are removed from Ahoi sync on your other devices too."
            ))
        }
    }

    private var orderedNodes: [IndentedTreeNode] {
        let liveIDs = Set(nodes.map(\.id))
        let children = Dictionary(grouping: nodes) { node in
            node.parentID.flatMap { liveIDs.contains($0) ? $0 : nil }
        }
        var result: [IndentedTreeNode] = []
        var visited = Set<TreeNodeID>()

        func appendForest(_ roots: [TreeNode], depth: Int) {
            var pending = roots.reversed().map { ($0, depth) }
            while let (node, rawDepth) = pending.popLast() {
                guard visited.insert(node.id).inserted else { continue }
                result.append(.init(
                    node: node,
                    depth: min(rawDepth, CompanionHierarchyPolicy.maximumDepth)
                ))
                let descendants = (children[node.id] ?? []).sorted(by: nodeOrder)
                pending.append(contentsOf: descendants.reversed().map {
                    ($0, rawDepth + 1)
                })
            }
        }
        appendForest((children[nil] ?? []).sorted(by: nodeOrder), depth: 0)
        for orphan in nodes.sorted(by: nodeOrder) where !visited.contains(orphan.id) {
            appendForest([orphan], depth: 0)
        }
        return result
    }

    private func nodeOrder(_ left: TreeNode, _ right: TreeNode) -> Bool {
        CompanionTreePosition.precedes(left, right)
    }

    private func orderedSiblings(of node: TreeNode) -> [TreeNode] {
        nodes.filter {
            $0.workspaceID == node.workspaceID && $0.parentID == node.parentID
        }.sorted(by: nodeOrder)
    }

    private func canMoveUp(_ node: TreeNode) -> Bool {
        orderedSiblings(of: node).first?.id != node.id
    }

    private func canMoveDown(_ node: TreeNode) -> Bool {
        orderedSiblings(of: node).last?.id != node.id
    }

    private func successorWhenMovingUp(_ node: TreeNode) -> TreeNodeID? {
        let siblings = orderedSiblings(of: node)
        guard let index = siblings.firstIndex(where: { $0.id == node.id }),
              index > 0 else { return node.id }
        return siblings[index - 1].id
    }

    private func successorWhenMovingDown(_ node: TreeNode) -> TreeNodeID? {
        let siblings = orderedSiblings(of: node)
        guard let index = siblings.firstIndex(where: { $0.id == node.id }),
              index + 1 < siblings.count else { return node.id }
        let successorIndex = index + 2
        return successorIndex < siblings.count ? siblings[successorIndex].id : nil
    }

    private func isInvalidMoveTarget(
        _ target: CompanionTreeMoveTarget,
        for node: TreeNode
    ) -> Bool {
        if target.workspaceID == node.workspaceID && target.parentID == node.parentID {
            return true
        }
        guard node.kind == .folder, let parentID = target.parentID else {
            return false
        }
        var descendants = Set<TreeNodeID>()
        var pending = [node.id]
        while let current = pending.popLast(), descendants.insert(current).inserted {
            pending.append(contentsOf: nodes.filter {
                $0.parentID == current
            }.map(\.id))
        }
        return descendants.contains(parentID)
    }
}

private struct IndentedTreeNode: Identifiable {
    let node: TreeNode
    let depth: Int
    var id: TreeNodeID { node.id }
}

private struct TreeNodeRow: View {
    let node: TreeNode
    let depth: Int
    let openURL: OpenURLAction
    let onOpenTreeNode: ((TreeNodeID) -> Void)?

    var body: some View {
        Button {
            if let onOpenTreeNode {
                onOpenTreeNode(node.id)
                return
            }
            guard let url = node.url.flatMap(URL.init(string:)) else { return }
            openURL(url)
        } label: {
            HStack(spacing: 8) {
                if !node.icon.isEmpty {
                    Text(node.icon)
                } else {
                    Image(systemName: node.kind == .folder ? "folder" : "bookmark")
                }
                Text(node.title)
                if let accent = node.accent.flatMap(Color.init(argbHex:)) {
                    Circle().fill(accent).frame(width: 7, height: 7)
                }
            }
                .padding(.leading, CGFloat(depth) * 18)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .buttonStyle(.plain)
        .disabled(node.kind == .folder || node.url == nil)
        .accessibilityIdentifier(
            node.kind == .folder
                ? "browser.library.folder.\(stableUUID(node.id.rawValue))"
                : "browser.library.saved-page.\(stableUUID(node.id.rawValue))"
        )
    }
}
