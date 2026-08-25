import Foundation
import SwiftUI
import AhoiCloudKitSpike

public struct CompanionRootView: View {
    @ObservedObject private var model: CompanionAppModel
    @Environment(\.openURL) private var openURL
    @State private var selectedWorkspaceID: WorkspaceID?
    @State private var query = ""
    @State private var draftTitle = ""
    @State private var draftURL = ""
    @State private var creationKind: CreationKind?
    @State private var workspacePendingDeletion: WorkspaceID?
    @State private var workspacePendingRename: Workspace?
    @State private var renameDraft = ""
    @AppStorage(CompanionSyncPreferences.enabledKey) private var syncEnabled = false

    public init(model: CompanionAppModel) {
        self.model = model
    }

    public var body: some View {
        NavigationSplitView {
            List(selection: $selectedWorkspaceID) {
                Section("Workspaces") {
                    ForEach(model.snapshot.visibleWorkspaces) { workspace in
                        HStack(spacing: 8) {
                            WorkspaceIcon(workspace: workspace)
                            Text(workspace.name)
                        }
                        .tag(workspace.id)
                        .contextMenu {
                            Button("Umbenennen") {
                                renameDraft = workspace.name
                                workspacePendingRename = workspace
                            }
                            Button("Workspace löschen", role: .destructive) {
                                workspacePendingDeletion = workspace.id
                            }
                        }
                    }
                }

                if !model.snapshot.visibleRemoteTabs.isEmpty {
                    Section("Geräte-Tabs") {
                        ForEach(model.snapshot.visibleRemoteTabs.prefix(8)) { tab in
                            remoteTabRow(tab)
                        }
                    }
                }
            }
            .navigationTitle("Ahoi Companion")
            .overlay {
                if model.snapshot.visibleWorkspaces.isEmpty && model.snapshot.visibleRemoteTabs.isEmpty {
                    Text("Noch keine synchronisierten Daten")
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
                    moveTargets: model.snapshot.visibleWorkspaces,
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
                                workspaceID: target,
                                parentID: nil
                            )
                        }
                    }
                )
            } else {
                ContentUnavailableView(
                    "Workspace auswählen",
                    systemImage: "sidebar.left",
                    description: Text("Workspaces, gespeicherte Seiten und normale Geräte-Tabs bleiben lokal verfügbar.")
                )
            }
        }
        .searchable(text: $query, placement: .sidebar, prompt: "Workspaces, Tabs, Verlauf")
        .onChange(of: query) { _, value in
            Task { await model.refreshSearch(query: value) }
        }
        .onChange(of: syncEnabled) { _, enabled in
            Task { await model.setSyncEnabled(enabled) }
        }
        .toolbar {
            ToolbarItem(placement: .automatic) {
                Menu {
                    Button("Workspace") { beginCreation(.workspace) }
                    Button("Ordner") { beginCreation(.folder) }
                        .disabled(selectedWorkspaceID == nil)
                    Button("Gespeicherte Seite") { beginCreation(.savedPage) }
                        .disabled(selectedWorkspaceID == nil)
                    Divider()
                    Toggle("CloudKit-Sync", isOn: $syncEnabled)
                    if syncEnabled && !model.isSyncConfigured {
                        Text("Apple-Konfiguration oder Schlüssel fehlt")
                    }
                } label: {
                    Label("Verwalten", systemImage: "plus.circle")
                }
            }
            ToolbarItem(placement: .automatic) {
                Button {
                    Task { await model.sync() }
                } label: {
                    Label("Synchronisieren", systemImage: "arrow.triangle.2.circlepath")
                }
                .accessibilityHint("Startet eine CloudKit-Synchronisierung, falls ein Provider konfiguriert ist.")
                .disabled(!model.isSyncConfigured)
            }
        }
        .safeAreaInset(edge: .bottom) {
            RemoteControlProvisioningView(
                identity: model.remoteControlIdentity,
                status: model.remoteCommandStatus
            )
        }
        .task {
            await model.load()
            await model.sync()
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(300))
                guard !Task.isCancelled else { break }
                await model.sync()
            }
        }
        .alert(creationTitle, isPresented: creationPresented) {
            TextField("Name", text: $draftTitle)
            if creationKind == .savedPage {
                TextField("https://…", text: $draftURL)
            }
            Button("Abbrechen", role: .cancel) { resetCreation() }
            Button("Anlegen") { commitCreation() }
        }
        .confirmationDialog(
            "Workspace samt Baum löschen?",
            isPresented: Binding(
                get: { workspacePendingDeletion != nil },
                set: { if !$0 { workspacePendingDeletion = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button("Löschen", role: .destructive) {
                guard let id = workspacePendingDeletion else { return }
                Task { await model.deleteWorkspace(id) }
                workspacePendingDeletion = nil
            }
            Button("Abbrechen", role: .cancel) { workspacePendingDeletion = nil }
        }
        .alert(
            "Workspace umbenennen",
            isPresented: Binding(
                get: { workspacePendingRename != nil },
                set: { if !$0 { workspacePendingRename = nil } }
            )
        ) {
            TextField("Name", text: $renameDraft)
            Button("Abbrechen", role: .cancel) { workspacePendingRename = nil }
            Button("Sichern") {
                guard let workspace = workspacePendingRename else { return }
                Task { await model.renameWorkspace(workspace.id, name: renameDraft) }
                workspacePendingRename = nil
            }
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
            onRemoteClose: { Task { await model.remotelyClose(tab) } }
        )
    }

    private var creationPresented: Binding<Bool> {
        Binding(
            get: { creationKind != nil },
            set: { if !$0 { resetCreation() } }
        )
    }

    private var creationTitle: String {
        switch creationKind {
        case .workspace: "Neuer Workspace"
        case .folder: "Neuer Ordner"
        case .savedPage: "Neue gespeicherte Seite"
        case nil: "Neu"
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
    public let moveTargets: [Workspace]
    public let actionableTabIDs: Set<TabID>
    public let openURL: OpenURLAction
    public let remoteControlAvailable: Bool
    public let onRemoteOpen: ((RemoteTab) -> Void)?
    public let onRemoteFocus: ((RemoteTab) -> Void)?
    public let onRemoteClose: ((RemoteTab) -> Void)?
    public let onDeleteNode: ((TreeNode) -> Void)?
    public let onRenameNode: ((TreeNode, String) -> Void)?
    public let onMoveNode: ((TreeNode, WorkspaceID) -> Void)?
    @State private var nodePendingRename: TreeNode?
    @State private var renameDraft = ""

    public init(
        workspace: Workspace,
        nodes: [TreeNode],
        tabs: [RemoteTab],
        moveTargets: [Workspace] = [],
        actionableTabIDs: Set<TabID> = [],
        openURL: OpenURLAction,
        remoteControlAvailable: Bool = false,
        onRemoteOpen: ((RemoteTab) -> Void)? = nil,
        onRemoteFocus: ((RemoteTab) -> Void)? = nil,
        onRemoteClose: ((RemoteTab) -> Void)? = nil,
        onDeleteNode: ((TreeNode) -> Void)? = nil,
        onRenameNode: ((TreeNode, String) -> Void)? = nil,
        onMoveNode: ((TreeNode, WorkspaceID) -> Void)? = nil
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
                            Button("Umbenennen") {
                                renameDraft = item.node.title
                                nodePendingRename = item.node
                            }
                            .disabled(onRenameNode == nil)
                            Menu("In Workspace verschieben") {
                                ForEach(moveTargets) { target in
                                    Button(target.name) {
                                        onMoveNode?(item.node, target.id)
                                    }
                                    .disabled(target.id == item.node.workspaceID)
                                }
                            }
                            .disabled(onMoveNode == nil)
                            Button("Löschen", role: .destructive) {
                                onDeleteNode?(item.node)
                            }
                            .disabled(onDeleteNode == nil)
                        }
                }
            }
            if !tabs.isEmpty {
                Section("Normale Tabs auf Geräten") {
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
            "Eintrag umbenennen",
            isPresented: Binding(
                get: { nodePendingRename != nil },
                set: { if !$0 { nodePendingRename = nil } }
            )
        ) {
            TextField("Name", text: $renameDraft)
            Button("Abbrechen", role: .cancel) { nodePendingRename = nil }
            Button("Sichern") {
                guard let node = nodePendingRename else { return }
                onRenameNode?(node, renameDraft)
                nodePendingRename = nil
            }
        }
    }

    private var orderedNodes: [IndentedTreeNode] {
        let liveIDs = Set(nodes.map(\.id))
        let children = Dictionary(grouping: nodes) { node in
            node.parentID.flatMap { liveIDs.contains($0) ? $0 : nil }
        }
        var result: [IndentedTreeNode] = []
        var visited = Set<TreeNodeID>()
        func append(_ node: TreeNode, depth: Int) {
            guard visited.insert(node.id).inserted else { return }
            result.append(.init(node: node, depth: depth))
            for child in (children[node.id] ?? []).sorted(by: nodeOrder) {
                append(child, depth: depth + 1)
            }
        }
        for root in (children[nil] ?? []).sorted(by: nodeOrder) {
            append(root, depth: 0)
        }
        for orphan in nodes.sorted(by: nodeOrder) where !visited.contains(orphan.id) {
            append(orphan, depth: 0)
        }
        return result
    }

    private func nodeOrder(_ left: TreeNode, _ right: TreeNode) -> Bool {
        if left.syncSortKey != right.syncSortKey {
            return left.syncSortKey < right.syncSortKey
        }
        return left.id < right.id
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

public struct RemoteTabRow: View {
    public let tab: RemoteTab
    public let openURL: OpenURLAction
    public let remoteControlAvailable: Bool
    public let onRemoteOpen: (() -> Void)?
    public let onRemoteFocus: (() -> Void)?
    public let onRemoteClose: (() -> Void)?

    public init(
        tab: RemoteTab,
        openURL: OpenURLAction,
        remoteControlAvailable: Bool = false,
        onRemoteOpen: (() -> Void)? = nil,
        onRemoteFocus: (() -> Void)? = nil,
        onRemoteClose: (() -> Void)? = nil
    ) {
        self.tab = tab
        self.openURL = openURL
        self.remoteControlAvailable = remoteControlAvailable
        self.onRemoteOpen = onRemoteOpen
        self.onRemoteFocus = onRemoteFocus
        self.onRemoteClose = onRemoteClose
    }

    public var body: some View {
        HStack(spacing: 8) {
            Button {
                guard let url = URL(string: tab.url) else { return }
                openURL(url)
            } label: {
                HStack(spacing: 10) {
                Image(systemName: deviceSymbol)
                    .foregroundStyle(.secondary)
                    .accessibilityLabel(tab.deviceKind.rawValue)
                VStack(alignment: .leading, spacing: 2) {
                    Text(tab.title.isEmpty ? tab.url : tab.title)
                        .lineLimit(1)
                    Text("\(tab.deviceName) · \(tab.workspaceName ?? "Ohne Workspace")")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                    Spacer(minLength: 8)
                Text(tab.lastActiveAt.physicalMilliseconds.formatted())
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.tertiary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .buttonStyle(.plain)
            if tab.deviceKind == .mac {
                Menu {
                    Button("Auf dem Mac öffnen", action: onRemoteOpen ?? {})
                        .disabled(!remoteControlAvailable || onRemoteOpen == nil)
                    Button("Mac-Tab fokussieren", action: onRemoteFocus ?? {})
                        .disabled(!remoteControlAvailable || onRemoteFocus == nil)
                    Button("Mac-Tab schließen", role: .destructive, action: onRemoteClose ?? {})
                        .disabled(!remoteControlAvailable || onRemoteClose == nil)
                    if !remoteControlAvailable {
                        Text("Signaturschlüssel fehlt")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                        .accessibilityLabel("Sichere Mac-Steuerung")
                }
            }
        }
        .accessibilityLabel("\(tab.title), \(tab.deviceName), normaler Tab")
        .accessibilityHint("Öffnet die URL im Standardbrowser")
    }

    private var deviceSymbol: String {
        switch tab.deviceKind {
        case .mac: "desktopcomputer"
        case .iPhone: "iphone"
        case .iPad: "ipad"
        }
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

private struct RemoteControlProvisioningView: View {
    let identity: RemoteControlProvisioningIdentity?
    let status: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            if let identity {
                Text("Mac-Steuerung: Schlüssel bereit")
                    .font(.caption.weight(.semibold))
                Text("Gerät \(identity.sourceDeviceID.rawValue.uuidString.lowercased())")
                Text("Fingerprint \(identity.fingerprint)")
                Text(identity.publicKeyBase64)
                    .lineLimit(1)
                    .textSelection(.enabled)
                Text("Der Mac muss Gerät und Public Key ausdrücklich freigeben.")
            } else {
                Text("Mac-Steuerung aus: kein vorprovisionierter Ed25519-Keychain-Schlüssel")
            }
            if let status { Text(status) }
        }
        .font(.caption2)
        .foregroundStyle(.secondary)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal)
        .padding(.vertical, 8)
        .background(.bar)
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
        .navigationTitle("Suche")
    }
}
