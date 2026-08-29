import Foundation
import SwiftUI
import AhoiCloudKitSpike

private func L(_ key: String, _ fallback: String) -> String {
    CompanionL10n.string(key, fallback: fallback)
}

public struct CompanionRootView: View {
    @ObservedObject private var model: CompanionAppModel
    @Environment(\.openURL) private var systemOpenURL
    private let overriddenOpenURL: OpenURLAction?
    private let accentTint: Color
    @State private var selectedWorkspaceID: WorkspaceID?
    @State private var query = ""
    @State private var draftTitle = ""
    @State private var draftURL = ""
    @State private var creationKind: CreationKind?
    @State private var workspacePendingDeletion: WorkspaceID?
    @State private var workspacePendingRename: Workspace?
    @State private var renameDraft = ""
    @State private var selectedRemoteDeviceID: DeviceID?
    @State private var settingsPresented = false
    @State private var sendLinkPresented = false
    @AppStorage(CompanionSyncPreferences.enabledKey) private var syncEnabled = false

    public init(
        model: CompanionAppModel,
        openURL: OpenURLAction? = nil,
        accentTint: Color = .accentColor
    ) {
        self.model = model
        self.overriddenOpenURL = openURL
        self.accentTint = accentTint
    }

    private var openURL: OpenURLAction { overriddenOpenURL ?? systemOpenURL }

    public var body: some View {
        NavigationSplitView {
            List(selection: $selectedWorkspaceID) {
                Section(L("root.workspaces", "Workspaces")) {
                    ForEach(model.snapshot.visibleWorkspaces) { workspace in
                        HStack(spacing: 8) {
                            WorkspaceIcon(workspace: workspace)
                            Text(workspace.name)
                        }
                        .tag(workspace.id)
                        .contextMenu {
                            Button(L("action.rename", "Rename")) {
                                renameDraft = workspace.name
                                workspacePendingRename = workspace
                            }
                            Button(L("workspace.delete", "Delete workspace"), role: .destructive) {
                                workspacePendingDeletion = workspace.id
                            }
                        }
                    }
                }

                if !model.snapshot.visibleRemoteTabs.isEmpty {
                    Section {
                        ForEach(filteredRemoteTabs) { tab in
                            remoteTabRow(tab)
                        }
                    } header: {
                        HStack {
                            Text(CompanionL10n.string(
                                "root.device_tabs",
                                fallback: "Device tabs"
                            ))
                            Spacer()
                            Menu {
                                Button(CompanionL10n.string(
                                    "device_filter.all",
                                    fallback: "All devices"
                                )) {
                                    selectedRemoteDeviceID = nil
                                }
                                ForEach(remoteDevices) { device in
                                    Button(device.name) {
                                        selectedRemoteDeviceID = device.id
                                    }
                                }
                            } label: {
                                Image(systemName: selectedRemoteDeviceID == nil
                                      ? "line.3.horizontal.decrease.circle"
                                      : "line.3.horizontal.decrease.circle.fill")
                                    .accessibilityLabel(CompanionL10n.string(
                                        "device_filter.accessibility",
                                        fallback: "Filter device tabs"
                                    ))
                            }
                        }
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(accentTint.opacity(0.055))
            .navigationTitle("AhoiBrowser")
            .overlay {
                if model.snapshot.visibleWorkspaces.isEmpty && model.snapshot.visibleRemoteTabs.isEmpty {
                    Text(L("root.empty", "No synced data yet"))
                        .foregroundStyle(.secondary)
                        .padding()
                }
            }
        } detail: {
            if !model.searchResults.isEmpty && !query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                SearchResultsView(results: model.searchResults, openURL: openURL)
            } else if let workspace = model.snapshot.visibleWorkspaces.first(where: { $0.id == selectedWorkspaceID }) {
                WorkspaceDetailView(
                    workspace: workspace,
                    nodes: model.snapshot.visibleTreeNodes.filter { $0.workspaceID == workspace.id },
                    tabs: model.visibleTabs(for: workspace.id),
                    moveTargets: CompanionMoveTargetBuilder.targets(
                        snapshot: model.snapshot
                    ),
                    actionableTabIDs: model.actionableRemoteTabIDs,
                    openURL: openURL,
                    remoteControlAvailable: model.isRemoteControlAvailable,
                    onRemoteOpen: { tab in
                        Task { await model.remotelyOpen(tab) }
                    },
                    onRemoteFocus: { tab in
                        Task { await model.remotelyFocus(tab) }
                    },
                    onRemoteClose: { tab in
                        Task { await model.remotelyClose(tab) }
                    },
                    onDeleteNode: { node in
                        Task { await model.deleteTreeNode(node.id) }
                    },
                    onRenameNode: { node, title in
                        Task { await model.renameTreeNode(node.id, title: title) }
                    },
                    onMoveNode: { node, target in
                        Task {
                            await model.moveTreeNode(
                                node.id,
                                workspaceID: target.workspaceID,
                                parentID: target.parentID
                            )
                        }
                    }
                )
            } else {
                ContentUnavailableView(
                    L("workspace.select", "Select a workspace"),
                    systemImage: "sidebar.left",
                    description: Text(L(
                        "workspace.select.description",
                        "Workspaces, saved pages and normal device tabs remain available locally."
                    ))
                )
            }
        }
        .tint(accentTint)
        .searchable(
            text: $query,
            placement: .sidebar,
            prompt: L("search.prompt", "Workspaces, tabs, history")
        )
        .onChange(of: query) { _, value in
            Task { await model.refreshSearch(query: value) }
        }
        .onChange(of: syncEnabled) { _, enabled in
            Task { await model.setSyncEnabled(enabled) }
        }
        .toolbar {
            ToolbarItem(placement: .automatic) {
                Menu {
                    Button(L("workspace.title", "Workspace")) { beginCreation(.workspace) }
                    Button(L("folder.title", "Folder")) { beginCreation(.folder) }
                        .disabled(selectedWorkspaceID == nil)
                    Button(L("saved_page.title", "Saved page")) { beginCreation(.savedPage) }
                        .disabled(selectedWorkspaceID == nil)
                    Divider()
                    Toggle(L("settings.sync.enabled", "CloudKit sync"), isOn: $syncEnabled)
                    if syncEnabled && !model.isSyncConfigured {
                        Text(L(
                            "settings.sync.configuration_short",
                            "Apple configuration or encryption key is missing"
                        ))
                    }
                } label: {
                    Label(L("action.manage", "Manage"), systemImage: "plus.circle")
                }
            }
            ToolbarItem(placement: .automatic) {
                Button {
                    Task { await model.sync() }
                } label: {
                    Label(L("action.sync_now", "Sync now"), systemImage: "arrow.triangle.2.circlepath")
                }
                .accessibilityHint(L(
                    "sync.action.hint",
                    "Starts a CloudKit sync when a provider is configured."
                ))
                .disabled(!model.isSyncConfigured)
            }
            ToolbarItemGroup(placement: .automatic) {
                Button {
                    sendLinkPresented = true
                } label: {
                    Label(
                        CompanionL10n.string(
                            "send_link.title",
                            fallback: "Send link"
                        ),
                        systemImage: "paperplane"
                    )
                }
                .disabled(!model.isRemoteControlAvailable || remoteDevices.isEmpty)

                Button {
                    settingsPresented = true
                } label: {
                    Label(
                        CompanionL10n.string(
                            "settings.title",
                            fallback: "Settings"
                        ),
                        systemImage: "gearshape"
                    )
                }
            }
        }
        .task {
            await model.load()
            await model.sync()
        }
        .alert(creationTitle, isPresented: creationPresented) {
            TextField(L("field.name", "Name"), text: $draftTitle)
            if creationKind == .savedPage {
                TextField("https://…", text: $draftURL)
            }
            Button(L("action.cancel", "Cancel"), role: .cancel) { resetCreation() }
            Button(L("action.create", "Create")) { commitCreation() }
        }
        .confirmationDialog(
            L("workspace.delete.confirmation", "Delete workspace and its tree?"),
            isPresented: Binding(
                get: { workspacePendingDeletion != nil },
                set: { if !$0 { workspacePendingDeletion = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button(L("action.delete", "Delete"), role: .destructive) {
                guard let id = workspacePendingDeletion else { return }
                Task { await model.deleteWorkspace(id) }
                workspacePendingDeletion = nil
            }
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                workspacePendingDeletion = nil
            }
        }
        .alert(
            L("workspace.rename", "Rename workspace"),
            isPresented: Binding(
                get: { workspacePendingRename != nil },
                set: { if !$0 { workspacePendingRename = nil } }
            )
        ) {
            TextField(L("field.name", "Name"), text: $renameDraft)
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                workspacePendingRename = nil
            }
            Button(L("action.save", "Save")) {
                guard let workspace = workspacePendingRename else { return }
                Task { await model.renameWorkspace(workspace.id, name: renameDraft) }
                workspacePendingRename = nil
            }
        }
        .sheet(isPresented: $settingsPresented) {
            CompanionSettingsView(model: model, syncEnabled: $syncEnabled)
        }
        .sheet(isPresented: $sendLinkPresented) {
            CompanionSendLinkView(model: model)
        }
    }

    private func remoteTabRow(_ tab: RemoteTab) -> some View {
        RemoteTabRow(
            tab: tab,
            openURL: openURL,
            remoteControlAvailable: model.isRemoteControlAvailable &&
                model.actionableRemoteTabIDs.contains(tab.id),
            onRemoteOpen: { Task { await model.remotelyOpen(tab) } },
            onRemoteFocus: { Task { await model.remotelyFocus(tab) } },
            onRemoteClose: { Task { await model.remotelyClose(tab) } },
            accentTint: accentTint
        )
    }

    private var filteredRemoteTabs: [RemoteTab] {
        model.snapshot.visibleRemoteTabs.filter { tab in
            selectedRemoteDeviceID.map { $0 == tab.deviceID } ?? true
        }
    }

    private var remoteDevices: [Device] {
        let remoteDeviceIDs = Set(model.snapshot.visibleRemoteTabs.map(\.deviceID))
        return model.snapshot.devices.filter {
            remoteDeviceIDs.contains($0.id) && !$0.isDeleted && !$0.isRevoked
        }.sorted {
            $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
    }

    private var creationPresented: Binding<Bool> {
        Binding(
            get: { creationKind != nil },
            set: { if !$0 { resetCreation() } }
        )
    }

    private var creationTitle: String {
        switch creationKind {
        case .workspace: L("workspace.new", "New workspace")
        case .folder: L("folder.new", "New folder")
        case .savedPage: L("saved_page.new", "New saved page")
        case nil: L("action.new", "New")
        }
    }

    private func beginCreation(_ kind: CreationKind) {
        draftTitle = ""
        draftURL = ""
        creationKind = kind
    }

    private func resetCreation() {
        creationKind = nil
        draftTitle = ""
        draftURL = ""
    }

    private func commitCreation() {
        let kind = creationKind
        let title = draftTitle
        let url = draftURL
        resetCreation()
        Task {
            switch kind {
            case .workspace:
                if let workspace = await model.createWorkspace(name: title) {
                    selectedWorkspaceID = workspace.id
                }
            case .folder:
                guard let selectedWorkspaceID else { return }
                _ = await model.createFolder(
                    workspaceID: selectedWorkspaceID,
                    title: title
                )
            case .savedPage:
                guard let selectedWorkspaceID else { return }
                _ = await model.createSavedPage(
                    workspaceID: selectedWorkspaceID,
                    title: title,
                    url: url
                )
            case nil:
                break
            }
        }
    }

    private enum CreationKind {
        case workspace
        case folder
        case savedPage
    }
}

public struct WorkspaceDetailView: View {
    public let workspace: Workspace
    public let nodes: [TreeNode]
    public let tabs: [RemoteTab]
    public let moveTargets: [CompanionTreeMoveTarget]
    public let actionableTabIDs: Set<TabID>
    public let openURL: OpenURLAction
    public let remoteControlAvailable: Bool
    public let onRemoteOpen: ((RemoteTab) -> Void)?
    public let onRemoteFocus: ((RemoteTab) -> Void)?
    public let onRemoteClose: ((RemoteTab) -> Void)?
    public let onDeleteNode: ((TreeNode) -> Void)?
    public let onRenameNode: ((TreeNode, String) -> Void)?
    public let onMoveNode: ((TreeNode, CompanionTreeMoveTarget) -> Void)?
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
        remoteControlAvailable: Bool = false,
        onRemoteOpen: ((RemoteTab) -> Void)? = nil,
        onRemoteFocus: ((RemoteTab) -> Void)? = nil,
        onRemoteClose: ((RemoteTab) -> Void)? = nil,
        onDeleteNode: ((TreeNode) -> Void)? = nil,
        onRenameNode: ((TreeNode, String) -> Void)? = nil,
        onMoveNode: ((TreeNode, CompanionTreeMoveTarget) -> Void)? = nil
    ) {
        self.workspace = workspace
        self.nodes = nodes
        self.tabs = tabs
        self.moveTargets = moveTargets
        self.actionableTabIDs = actionableTabIDs
        self.openURL = openURL
        self.remoteControlAvailable = remoteControlAvailable
        self.onRemoteOpen = onRemoteOpen
        self.onRemoteFocus = onRemoteFocus
        self.onRemoteClose = onRemoteClose
        self.onDeleteNode = onDeleteNode
        self.onRenameNode = onRenameNode
        self.onMoveNode = onMoveNode
    }

    public var body: some View {
        List {
            Section(workspace.name) {
                ForEach(orderedNodes) { item in
                    TreeNodeRow(node: item.node, depth: item.depth, openURL: openURL)
                        .contextMenu {
                            Button(L("action.rename", "Rename")) {
                                renameDraft = item.node.title
                                nodePendingRename = item.node
                            }
                            .disabled(onRenameNode == nil)
                            Menu(CompanionL10n.string(
                                "tree.move",
                                fallback: "Move to"
                            )) {
                                ForEach(moveTargets) { target in
                                    Button(target.label) {
                                        onMoveNode?(item.node, target)
                                    }
                                    .disabled(isInvalidMoveTarget(target, for: item.node))
                                }
                            }
                            .disabled(onMoveNode == nil)
                            Button(L("action.delete", "Delete"), role: .destructive) {
                                nodePendingDeletion = item.node
                            }
                            .disabled(onDeleteNode == nil)
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
        .navigationTitle(workspace.name)
        .alert(
            L("tree.rename", "Rename item"),
            isPresented: Binding(
                get: { nodePendingRename != nil },
                set: { if !$0 { nodePendingRename = nil } }
            )
        ) {
            TextField(L("field.name", "Name"), text: $renameDraft)
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                nodePendingRename = nil
            }
            Button(L("action.save", "Save")) {
                guard let node = nodePendingRename else { return }
                onRenameNode?(node, renameDraft)
                nodePendingRename = nil
            }
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
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                nodePendingDeletion = nil
            }
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
        if left.syncSortKey != right.syncSortKey {
            return left.syncSortKey < right.syncSortKey
        }
        return left.id < right.id
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

    var body: some View {
        Button {
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
    }
}

private struct WorkspaceIcon: View {
    let workspace: Workspace

    var body: some View {
        HStack(spacing: 4) {
            if workspace.icon.isEmpty {
                Image(systemName: "square.stack.3d.up")
            } else {
                Text(workspace.icon)
            }
            if let accent = workspace.accent.flatMap(Color.init(argbHex:)) {
                Circle().fill(accent).frame(width: 7, height: 7)
            }
        }
    }
}

private extension Color {
    init?(argbHex: String) {
        let raw = argbHex.trimmingCharacters(
            in: CharacterSet(charactersIn: "#")
        )
        guard let value = UInt32(raw, radix: 16), raw.count == 8 else { return nil }
        self.init(
            .sRGB,
            red: Double((value >> 16) & 0xff) / 255,
            green: Double((value >> 8) & 0xff) / 255,
            blue: Double(value & 0xff) / 255,
            opacity: Double((value >> 24) & 0xff) / 255
        )
    }
}

private struct SearchResultsView: View {
    let results: [CompanionSearchResult]
    let openURL: OpenURLAction

    var body: some View {
        List(results) { result in
            Button {
                guard let url = result.url.flatMap(URL.init(string:)) else { return }
                openURL(url)
            } label: {
                VStack(alignment: .leading, spacing: 3) {
                    Text(result.title)
                    Text(result.detail)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .buttonStyle(.plain)
            .disabled(result.url == nil)
        }
        .navigationTitle(L("search.title", "Search"))
    }
}
