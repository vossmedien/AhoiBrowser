import Foundation
import SwiftUI
import AhoiCloudKitSpike

public struct CompanionRootView: View {
    @ObservedObject private var model: CompanionAppModel
    @Environment(\.openURL) private var systemOpenURL
    private let overriddenOpenURL: OpenURLAction?
    private let onOpenTreeNode: ((TreeNodeID) -> Void)?
    private let accentTint: Color
    private let onDone: (() -> Void)?
    // Read at the sheet root, outside NavigationSplitView, so it dismisses the
    // library sheet itself rather than popping a pushed detail column.
    @Environment(\.dismiss) private var dismissLibrary
    @State private var selectedWorkspaceID: WorkspaceID?
    @State private var query = ""
    @State private var draftTitle = ""
    @State private var draftURL = ""
    @State private var creationKind: CreationKind?
    @State private var workspacePendingDeletion: WorkspaceID?
    @State private var workspacePendingRename: Workspace?
    @State private var workspacePendingMerge: WorkspaceMergeRequest?
    @State private var renameDraft = ""
    @State private var selectedRemoteDeviceID: DeviceID?
    @State private var settingsPresented = false
    @State private var sendLinkPresented = false
    @State private var bookmarksPresented = false
    @Binding private var syncEnabled: Bool

    public init(
        model: CompanionAppModel,
        syncEnabled: Binding<Bool>,
        openURL: OpenURLAction? = nil,
        onOpenTreeNode: ((TreeNodeID) -> Void)? = nil,
        accentTint: Color = .accentColor,
        onDone: (() -> Void)? = nil
    ) {
        self.model = model
        self._syncEnabled = syncEnabled
        self.overriddenOpenURL = openURL
        self.onOpenTreeNode = onOpenTreeNode
        self.accentTint = accentTint
        self.onDone = onDone
    }

    private var openURL: OpenURLAction { overriddenOpenURL ?? systemOpenURL }

    public var body: some View {
        NavigationSplitView {
            if query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            List(selection: $selectedWorkspaceID) {
                Section {
                    CompanionBookmarkLibraryEntry { bookmarksPresented = true }
                }
                Section(L("root.workspaces", "Workspaces")) {
                    ForEach(model.snapshot.visibleWorkspaces) { workspace in
                        HStack(spacing: 8) {
                            WorkspaceIcon(workspace: workspace)
                            Text(workspace.name)
                        }
                        .tag(workspace.id)
                        .accessibilityIdentifier(
                            "browser.library.workspace.\(stableUUID(workspace.id.rawValue))"
                        )
                        .contextMenu {
                            Button(L("action.rename", "Rename")) {
                                renameDraft = workspace.name
                                workspacePendingRename = workspace
                            }
                            .accessibilityIdentifier(
                                "browser.library.workspace.rename.\(stableUUID(workspace.id.rawValue))"
                            )
                            mergeMenu(for: workspace)
                            Button(L("workspace.delete", "Delete workspace"), role: .destructive) {
                                workspacePendingDeletion = workspace.id
                            }
                            .accessibilityIdentifier(
                                "browser.library.workspace.delete.\(stableUUID(workspace.id.rawValue))"
                            )
                        }
                    }
                    SeparatedWorkspaceRows(
                        coordinator: model.separatedWorkspaces,
                        accentTint: .accentColor,
                        onOpen: nil
                    )
                }

                if !model.snapshot.visibleRemoteTabs.isEmpty {
                    Section {
                        ForEach(filteredRemoteTabs) { tab in
                            remoteTabRow(tab)
                                // The rows carry their own device label and
                                // spacing; the stock hairlines only add noise.
                                .listRowSeparator(.hidden)
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
            .accessibilityIdentifier("browser.library.root")
            .navigationTitle("AhoiBrowser")
            .modifier(LibraryDoneToolbar(onDone: libraryDoneAction))
            .toolbar {
                ToolbarItem(placement: .automatic) {
                    Menu {
                        Button(L("workspace.title", "Workspace")) { beginCreation(.workspace) }
                            .accessibilityIdentifier("browser.library.create.workspace")
                        Button(L("folder.title", "Folder")) { beginCreation(.folder) }
                            .disabled(selectedWorkspaceID == nil)
                            .accessibilityIdentifier("browser.library.create.folder")
                        Button(L("saved_page.title", "Saved page")) { beginCreation(.savedPage) }
                            .disabled(selectedWorkspaceID == nil)
                            .accessibilityIdentifier("browser.library.create.saved-page")
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
                    .accessibilityIdentifier("browser.library.manage")
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
            .overlay {
                if model.snapshot.visibleWorkspaces.isEmpty && model.snapshot.visibleRemoteTabs.isEmpty {
                    Text(L("root.empty", "No synced data yet"))
                        .foregroundStyle(.secondary)
                        .padding()
                }
            }
            } else {
                CompanionSearchResultsView(
                    results: model.searchResults,
                    openURL: openURL,
                    onOpenTreeNode: onOpenTreeNode
                )
            }
        } detail: {
            Group {
            if let workspace = model.snapshot.visibleWorkspaces.first(where: { $0.id == selectedWorkspaceID }) {
                WorkspaceDetailView(
                    workspace: workspace,
                    nodes: model.snapshot.visibleTreeNodes.filter { $0.workspaceID == workspace.id },
                    tabs: model.visibleTabs(for: workspace.id),
                    moveTargets: CompanionMoveTargetBuilder.targets(
                        snapshot: model.snapshot
                    ),
                    actionableTabIDs: model.actionableRemoteTabIDs,
                    openURL: openURL,
                    onOpenTreeNode: onOpenTreeNode,
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
                    },
                    onReorderNode: { node, successorID in
                        Task {
                            await model.reorderTreeNode(
                                node.id,
                                before: successorID
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
            // A pushed compact detail hides the sidebar's Done, so it carries its own.
            .modifier(LibraryDoneToolbar(onDone: libraryDoneAction, onlyWhenCompact: true))
        }
        .tint(accentTint)
        .sheet(isPresented: $bookmarksPresented) {
            BookmarkLibraryView(model: model, openURL: openURL)
        }
        .searchable(
            text: $query,
            placement: .sidebar,
            prompt: L("search.prompt", "Workspaces, tabs, history")
        )
        .accessibilityIdentifier("browser.library.search")
        .onChange(of: query) { _, value in
            Task { await model.refreshSearch(query: value) }
        }
        .task {
            await model.load()
            await model.sync()
        }
        // A small form sheet instead of a text-field alert: an alert presentation
        // could outlive the pushed Workspace detail and resurface after the
        // library closed, blocking Done and the browser underneath.
        .sheet(isPresented: creationPresented) {
            NavigationStack {
                Form {
                    TextField(L("field.name", "Name"), text: $draftTitle)
                        .accessibilityIdentifier("browser.library.create.name")
                    if creationKind == .savedPage {
                        TextField("https://…", text: $draftURL)
                            .keyboardType(.URL)
                            .textInputAutocapitalization(.never)
                            .accessibilityIdentifier("browser.library.create.url")
                    }
                }
                .navigationTitle(creationTitle)
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button(L("action.cancel", "Cancel")) { resetCreation() }
                            .accessibilityIdentifier("browser.library.create.cancel")
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button(L("action.create", "Create")) { commitCreation() }
                            .disabled(draftTitle.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                            .accessibilityIdentifier("browser.library.create.confirm")
                    }
                }
            }
            .presentationDetents([.medium])
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
            .accessibilityIdentifier("browser.library.workspace.delete.confirm")
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                workspacePendingDeletion = nil
            }
            .accessibilityIdentifier("browser.library.workspace.delete.cancel")
        }
        .confirmationDialog(
            mergeConfirmationTitle,
            isPresented: Binding(
                get: { workspacePendingMerge != nil },
                set: { if !$0 { workspacePendingMerge = nil } }
            ),
            titleVisibility: .visible,
            presenting: workspacePendingMerge
        ) { request in
            Button(L("workspace.merge.as_folder", "Merge as folder")) {
                merge(request, intoFolder: true)
            }
            .accessibilityIdentifier("browser.library.workspace.merge.folder")
            Button(L("workspace.merge.flat", "Merge without folder")) {
                merge(request, intoFolder: false)
            }
            .accessibilityIdentifier("browser.library.workspace.merge.flat")
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                workspacePendingMerge = nil
            }
        } message: { request in
            Text(mergeConfirmationMessage(request))
        }
        .alert(
            L("workspace.rename", "Rename workspace"),
            isPresented: Binding(
                get: { workspacePendingRename != nil },
                set: { if !$0 { workspacePendingRename = nil } }
            )
        ) {
            TextField(L("field.name", "Name"), text: $renameDraft)
                .accessibilityIdentifier("browser.library.workspace.rename.field")
            Button(L("action.cancel", "Cancel"), role: .cancel) {
                workspacePendingRename = nil
            }
            .accessibilityIdentifier("browser.library.workspace.rename.cancel")
            Button(L("action.save", "Save")) {
                guard let workspace = workspacePendingRename else { return }
                Task { await model.renameWorkspace(workspace.id, name: renameDraft) }
                workspacePendingRename = nil
            }
            .accessibilityIdentifier("browser.library.workspace.rename.save")
        }
        .sheet(isPresented: $settingsPresented) {
            CompanionSettingsView(model: model, syncEnabled: $syncEnabled)
        }
        .sheet(isPresented: $sendLinkPresented) {
            CompanionSendLinkView(model: model)
        }
        .safeAreaInset(edge: .top) {
            if let message = model.loadError {
                CompanionOperationErrorBanner(message: message, dismiss: model.dismissLoadError)
            }
        }
        .safeAreaInset(edge: .bottom) {
            if let receipt = model.pendingWorkspaceMergeUndo {
                WorkspaceMergeUndoBanner(
                    message: CompanionL10n.format(
                        "workspace.merge.done",
                        fallback: "“%1$@” merged into “%2$@”.",
                        receipt.previousSourceName,
                        workspaceName(receipt.targetID)
                    ),
                    undo: { Task { await model.undoWorkspaceMerge() } },
                    dismiss: model.dismissWorkspaceMergeUndo
                )
            }
        }
    }

    @ViewBuilder
    private func mergeMenu(for workspace: Workspace) -> some View {
        let targets = model.snapshot.visibleWorkspaces.filter {
            model.canMergeWorkspace(workspace.id, into: $0.id)
        }
        if !targets.isEmpty {
            Menu(L("workspace.merge", "Merge into…")) {
                ForEach(targets) { target in
                    Button(target.name) { requestMerge(workspace, into: target) }
                        .accessibilityIdentifier(
                            "browser.library.workspace.merge.\(stableUUID(workspace.id.rawValue)).\(stableUUID(target.id.rawValue))"
                        )
                }
            }
        }
    }

    /// ADR 0012: an empty Workspace merges without asking.
    private func requestMerge(_ source: Workspace, into target: Workspace) {
        let request = WorkspaceMergeRequest(source: source, target: target)
        if mergeCounts(request).pages + mergeCounts(request).folders == 0 {
            merge(request, intoFolder: true)
        } else {
            workspacePendingMerge = request
        }
    }

    private func merge(_ request: WorkspaceMergeRequest, intoFolder: Bool) {
        workspacePendingMerge = nil
        if selectedWorkspaceID == request.source.id { selectedWorkspaceID = request.target.id }
        Task {
            await model.mergeWorkspace(
                request.source.id,
                into: request.target.id,
                intoFolder: intoFolder
            )
        }
    }

    private func mergeCounts(_ request: WorkspaceMergeRequest) -> (pages: Int, folders: Int) {
        let nodes = model.snapshot.visibleTreeNodes.filter { $0.workspaceID == request.source.id }
        return (nodes.filter { $0.kind == .savedPage }.count, nodes.filter { $0.kind == .folder }.count)
    }

    private var mergeConfirmationTitle: String {
        guard let request = workspacePendingMerge else { return "" }
        return CompanionL10n.format(
            "workspace.merge.confirmation",
            fallback: "Merge “%1$@” into “%2$@”?",
            request.source.name,
            request.target.name
        )
    }

    private func mergeConfirmationMessage(_ request: WorkspaceMergeRequest) -> String {
        let counts = mergeCounts(request)
        return CompanionL10n.format(
            "workspace.merge.message",
            fallback: "%1$d pages and %2$d folders move to “%3$@”, and “%4$@” is removed. You can undo this right afterwards.",
            counts.pages,
            counts.folders,
            request.target.name,
            request.source.name
        )
    }

    private func workspaceName(_ id: WorkspaceID) -> String {
        model.snapshot.visibleWorkspaces.first { $0.id == id }?.name ?? ""
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

    /// Updates the caller's binding and dismisses the sheet directly: after a
    /// Workspace push the binding alone could leave the sheet on screen.
    private var libraryDoneAction: (() -> Void)? {
        guard let onDone else { return nil }
        return {
            onDone()
            dismissLibrary()
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
        // Called from the Manage menu: presenting while that menu is still
        // closing leaves its popover dismiss region behind, which then
        // swallows taps such as Done after the Workspace is created.
        Task { @MainActor in
            try? await Task.sleep(for: .milliseconds(350))
            creationKind = kind
        }
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
                    // Push the new detail only after the form sheet has closed.
                    try? await Task.sleep(for: .milliseconds(450))
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

private struct LibraryDoneToolbar: ViewModifier {
    let onDone: (() -> Void)?
    var onlyWhenCompact = false
    @Environment(\.horizontalSizeClass) private var sizeClass

    func body(content: Content) -> some View {
        if let onDone, !onlyWhenCompact || sizeClass == .compact {
            content.toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(CompanionL10n.string("action.done", fallback: "Done"), action: onDone)
                        .accessibilityIdentifier("browser.library.done")
                }
            }
        } else {
            content
        }
    }
}

struct WorkspaceMergeRequest: Identifiable, Equatable {
    let source: Workspace
    let target: Workspace
    var id: String { "\(source.id.rawValue).\(target.id.rawValue)" }
}

/// The one-step undo offered right after a Workspace merge (ADR 0012).
struct WorkspaceMergeUndoBanner: View {
    let message: String
    let undo: () -> Void
    let dismiss: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Text(message)
                .font(.callout)
                .frame(maxWidth: .infinity, alignment: .leading)
            Button(L("action.undo", "Undo"), action: undo)
                .buttonStyle(.borderedProminent)
                .accessibilityIdentifier("browser.library.workspace.merge.undo")
            Button(action: dismiss) {
                Image(systemName: "xmark")
            }
            .accessibilityLabel(L("action.close", "Close"))
            .accessibilityIdentifier("browser.library.workspace.merge.undo.dismiss")
        }
        .padding(12)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        .padding(.horizontal, 16)
        .padding(.bottom, 8)
    }
}
