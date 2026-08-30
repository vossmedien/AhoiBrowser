import Foundation
import SwiftUI
import UIKit
import AhoiCloudKitSpike

/// The regular-width browser sidebar. Unlike `CompanionRootView`, this view is
/// intentionally only a list column: the surrounding browser owns the one and
/// only `NavigationSplitView` and every navigation action is routed back to it.
struct MobileBrowserSidebar: View {
    @Environment(\.colorScheme) private var colorScheme
    @ObservedObject private var model: CompanionAppModel
    @ObservedObject private var browser: MobileBrowserController

    private let accentTint: Color
    private let onPresentCommand: () -> Void
    private let onSelectWorkspace: (WorkspaceID) -> Void
    private let onSelectTab: (UUID) -> Void
    private let onOpenPage: (URL, WorkspaceID?) -> Void
    private let onCreateTab: (WorkspaceID?, MobileBrowsingMode) -> Void

    init(
        model: CompanionAppModel,
        browser: MobileBrowserController,
        accentTint: Color,
        onPresentCommand: @escaping () -> Void,
        onSelectWorkspace: @escaping (WorkspaceID) -> Void,
        onSelectTab: @escaping (UUID) -> Void,
        onOpenPage: @escaping (URL, WorkspaceID?) -> Void,
        onCreateTab: @escaping (WorkspaceID?, MobileBrowsingMode) -> Void
    ) {
        self.model = model
        self.browser = browser
        self.accentTint = accentTint
        self.onPresentCommand = onPresentCommand
        self.onSelectWorkspace = onSelectWorkspace
        self.onSelectTab = onSelectTab
        self.onOpenPage = onOpenPage
        self.onCreateTab = onCreateTab
    }

    var body: some View {
        List {
            commandSection
            if isPrivateBrowsing {
                localTabsSection
            } else {
                workspaceSection
                savedHierarchySection
                localTabsSection
                remoteTabsSection
            }
        }
        .listStyle(.sidebar)
        .scrollContentBackground(.hidden)
        .background(isPrivateBrowsing
                    ? MobileBrowserChromeTheme.privateBackground
                    : accentTint.opacity(0.055))
        .navigationTitle(isPrivateBrowsing
                         ? CompanionL10n.string("browser.private", fallback: "Private")
                         : "AhoiBrowser")
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    onCreateTab(contentWorkspaceID, selectedMode)
                } label: {
                    Image(systemName: "plus")
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .accessibilityIdentifier("browser.sidebar.new-tab")
                .accessibilityLabel(CompanionL10n.string(
                    "browser.new_tab",
                    fallback: "New tab"
                ))
            }
        }
        .tint(accentTint)
        .environment(\.colorScheme, isPrivateBrowsing ? .dark : colorScheme)
    }

    private var commandSection: some View {
        Section {
            Button(action: onPresentCommand) {
                HStack(spacing: 10) {
                    Image(systemName: "magnifyingglass")
                        .foregroundStyle(accentTint)
                        .accessibilityHidden(true)
                    Text(CompanionL10n.string(
                        "browser.focus.search",
                        fallback: "Search, address or command"
                    ))
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    Spacer(minLength: 4)
                    Text("⌘L")
                        .font(.caption.monospaced())
                        .foregroundStyle(.tertiary)
                        .accessibilityHidden(true)
                }
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("browser.sidebar.command")
            .accessibilityLabel(CompanionL10n.string(
                "browser.focus.search",
                fallback: "Search, address or command"
            ))
        }
    }

    @ViewBuilder
    private var workspaceSection: some View {
        Section(CompanionL10n.string("root.workspaces", fallback: "Workspaces")) {
            if model.snapshot.visibleWorkspaces.isEmpty {
                sidebarEmptyRow(
                    CompanionL10n.string(
                        "browser.sidebar.workspaces.empty",
                        fallback: "No workspaces yet"
                    ),
                    symbol: "square.stack.3d.up.slash"
                )
            } else {
                ForEach(model.snapshot.visibleWorkspaces) { workspace in
                    let isSelected = selectedWorkspaceID == workspace.id
                    Button {
                        onSelectWorkspace(workspace.id)
                    } label: {
                        HStack(spacing: 10) {
                            workspaceIcon(workspace)
                            Text(workspace.name)
                                .fontWeight(isSelected ? .semibold : .regular)
                                .lineLimit(1)
                            Spacer(minLength: 8)
                            if isSelected {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(accentTint)
                                    .accessibilityHidden(true)
                            }
                        }
                        .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .listRowBackground(isSelected ? accentTint.opacity(0.13) : Color.clear)
                    .accessibilityIdentifier(
                        "browser.sidebar.workspace.\(identifier(workspace.id.rawValue))"
                    )
                    .accessibilityValue(Text(isSelected ? selectedAccessibilityValue : ""))
                }
            }
        }
    }

    @ViewBuilder
    private var savedHierarchySection: some View {
        Section(CompanionL10n.string(
            "browser.sidebar.saved",
            fallback: "Saved"
        )) {
            if let contentWorkspaceID {
                let items = savedItems(in: contentWorkspaceID)
                if items.isEmpty {
                    sidebarEmptyRow(
                        CompanionL10n.string(
                            "browser.sidebar.saved.empty",
                            fallback: "No saved pages"
                        ),
                        symbol: "bookmark.slash"
                    )
                } else {
                    ForEach(items) { item in
                        savedNodeRow(item)
                    }
                }
            } else {
                sidebarEmptyRow(
                    CompanionL10n.string(
                        "browser.sidebar.saved.no_workspace",
                        fallback: "Choose a workspace to view saved pages"
                    ),
                    symbol: "bookmark"
                )
            }
        }
    }

    @ViewBuilder
    private var localTabsSection: some View {
        Section(CompanionL10n.string(
            "browser.sidebar.local_tabs",
            fallback: "Tabs on This Device"
        )) {
            if visibleLocalTabs.isEmpty {
                sidebarEmptyRow(
                    CompanionL10n.string(
                        "browser.sidebar.local_tabs.empty",
                        fallback: "No open tabs"
                    ),
                    symbol: "rectangle.on.rectangle.slash"
                )
            } else {
                ForEach(visibleLocalTabs) { tab in
                    localTabRow(tab)
                }
            }
        }
    }

    @ViewBuilder
    private var remoteTabsSection: some View {
        Section(CompanionL10n.string(
            "root.device_tabs",
            fallback: "Device tabs"
        )) {
            if remoteDeviceGroups.isEmpty {
                sidebarEmptyRow(
                    CompanionL10n.string(
                        "browser.sidebar.device_tabs.empty",
                        fallback: "No synced device tabs"
                    ),
                    symbol: "laptopcomputer.and.iphone"
                )
            } else {
                ForEach(remoteDeviceGroups) { group in
                    Label(group.name, systemImage: group.symbol)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                        .frame(minHeight: 32)
                        .accessibilityIdentifier(
                            "browser.sidebar.device.\(identifier(group.id.rawValue))"
                        )
                    ForEach(group.tabs) { tab in
                        remoteTabRow(tab)
                    }
                }
            }
        }
    }

    private func workspaceIcon(_ workspace: Workspace) -> some View {
        Image(systemName: MobileWorkspaceIconPolicy.systemName(for: workspace.icon))
        .font(.body.weight(.semibold))
        .foregroundStyle(accentTint)
        .frame(width: 32, height: 32)
        .background(accentTint.opacity(0.11), in: RoundedRectangle(cornerRadius: 9))
        .accessibilityHidden(true)
    }

    @ViewBuilder
    private func savedNodeRow(_ item: SidebarSavedItem) -> some View {
        let node = item.node
        if node.kind == .folder {
            Label(node.title, systemImage: "folder.fill")
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.secondary)
                .padding(.leading, CGFloat(item.depth) * 16)
                .frame(maxWidth: .infinity, minHeight: 36, alignment: .leading)
                .accessibilityIdentifier(
                    "browser.sidebar.folder.\(identifier(node.id.rawValue))"
                )
                .accessibilityLabel(CompanionL10n.format(
                    "browser.sidebar.folder.level",
                    fallback: "%@, folder, level %d",
                    node.title,
                    item.depth + 1
                ))
        } else if let value = node.url, let url = URL(string: value) {
            Button {
                onOpenPage(url, node.workspaceID)
            } label: {
                HStack(spacing: 10) {
                    Image(systemName: "bookmark.fill")
                        .foregroundStyle(accentTint)
                        .frame(width: 24, height: 24)
                        .accessibilityHidden(true)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(node.title.isEmpty ? value : node.title)
                            .lineLimit(1)
                        Text(URL(string: value)?.host() ?? value)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    Spacer(minLength: 4)
                }
                .padding(.leading, CGFloat(item.depth) * 16)
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier(
                "browser.sidebar.saved-page.\(identifier(node.id.rawValue))"
            )
            .accessibilityHint(CompanionL10n.string(
                "browser.sidebar.open_hint",
                fallback: "Opens in a new tab"
            ))
            .accessibilityValue(Text(CompanionL10n.format(
                "browser.sidebar.item.level",
                fallback: "Level %d",
                item.depth + 1
            )))
        }
    }

    private func localTabRow(_ tab: MobileTabRecord) -> some View {
        let isSelected = browser.selectedTabID == tab.id
        return Button {
            onSelectTab(tab.id)
        } label: {
            HStack(spacing: 10) {
                localTabIcon(tab)
                VStack(alignment: .leading, spacing: 2) {
                    Text(tab.displayTitle)
                        .fontWeight(isSelected ? .semibold : .regular)
                        .lineLimit(1)
                    Text(localTabSubtitle(tab))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                Spacer(minLength: 4)
                if isSelected {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(tab.mode == .privateBrowsing ? .purple : accentTint)
                        .accessibilityHidden(true)
                }
            }
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .listRowBackground(isSelected ? accentTint.opacity(0.13) : Color.clear)
        .accessibilityIdentifier("browser.sidebar.local-tab.\(identifier(tab.id))")
        .accessibilityValue(Text(isSelected ? selectedAccessibilityValue : ""))
    }

    @ViewBuilder
    private func localTabIcon(_ tab: MobileTabRecord) -> some View {
        Group {
            if tab.mode == .privateBrowsing {
                Image(systemName: "hand.raised.fill")
                    .foregroundStyle(.purple)
                    .frame(width: 28, height: 28)
            } else if let data = tab.faviconData, let image = UIImage(data: data) {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(width: 28, height: 28)
                    .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
            } else {
                Image(systemName: "globe")
                    .foregroundStyle(.secondary)
                    .frame(width: 28, height: 28)
            }
        }
        .accessibilityHidden(true)
    }

    private func remoteTabRow(_ tab: RemoteTab) -> some View {
        Button {
            guard let url = URL(string: tab.url) else { return }
            onOpenPage(url, tab.workspaceID)
        } label: {
            HStack(spacing: 10) {
                Image(systemName: deviceSymbol(tab.deviceKind))
                    .foregroundStyle(accentTint)
                    .frame(width: 28, height: 28)
                    .background(accentTint.opacity(0.10), in: RoundedRectangle(cornerRadius: 8))
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 2) {
                    Text(tab.title.isEmpty ? tab.url : tab.title)
                        .lineLimit(1)
                    Text(tab.workspaceName ?? CompanionL10n.string(
                        "workspace.none",
                        fallback: "No workspace"
                    ))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                }
                Spacer(minLength: 4)
            }
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(
            "browser.sidebar.remote-tab.\(identifier(tab.id.rawValue))"
        )
        .accessibilityLabel(CompanionL10n.format(
            "remote_tab.accessibility",
            fallback: "%@, %@, normal tab",
            tab.title.isEmpty ? tab.url : tab.title,
            tab.deviceName
        ))
        .accessibilityHint(CompanionL10n.string(
            "browser.sidebar.open_hint",
            fallback: "Opens in a new tab"
        ))
    }

    private func sidebarEmptyRow(_ title: String, symbol: String) -> some View {
        Label(title, systemImage: symbol)
            .font(.caption)
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
    }

    private var selectedWorkspaceID: WorkspaceID? {
        guard browser.selectedTab?.mode == .normal,
              let workspaceID = browser.selectedTab?.workspaceID,
              model.snapshot.visibleWorkspaces.contains(where: { $0.id == workspaceID }) else {
            return nil
        }
        return workspaceID
    }

    private var contentWorkspaceID: WorkspaceID? {
        selectedWorkspaceID
    }

    private var visibleLocalTabs: [MobileTabRecord] {
        isPrivateBrowsing
            ? browser.privateTabs
            : browser.normalTabs
    }

    private var selectedMode: MobileBrowsingMode {
        browser.selectedTab?.mode ?? .normal
    }

    private var isPrivateBrowsing: Bool {
        selectedMode == .privateBrowsing
    }

    private var selectedAccessibilityValue: String {
        CompanionL10n.string("browser.tabs.selected", fallback: "Selected")
    }

    private func localTabSubtitle(_ tab: MobileTabRecord) -> String {
        let workspaceName = tab.workspaceID.flatMap { id in
            model.snapshot.visibleWorkspaces.first(where: { $0.id == id })?.name
        }
        let origin = tab.url.flatMap(URL.init(string:))?.host()
            ?? CompanionL10n.string("browser.new_tab", fallback: "New tab")
        if tab.mode == .privateBrowsing {
            return CompanionL10n.format(
                "browser.private.address",
                fallback: "Private · %@",
                origin
            )
        }
        guard let workspaceName else { return origin }
        return "\(workspaceName) · \(origin)"
    }

    private func savedItems(in workspaceID: WorkspaceID) -> [SidebarSavedItem] {
        let nodes = model.snapshot.visibleTreeNodes.filter { $0.workspaceID == workspaceID }
        let validFolderIDs = Set(nodes.lazy.filter { $0.kind == .folder }.map(\.id))
        let grouped = Dictionary(grouping: nodes) { node in
            node.parentID.flatMap { validFolderIDs.contains($0) ? $0 : nil }
        }
        var result: [SidebarSavedItem] = []
        var visited = Set<TreeNodeID>()

        func appendForest(_ roots: [TreeNode], depth: Int) {
            var pending = roots.reversed().map { ($0, depth) }
            while let (node, rawDepth) = pending.popLast() {
                guard visited.insert(node.id).inserted else { continue }
                let displayDepth = min(rawDepth, CompanionHierarchyPolicy.maximumDepth)
                result.append(SidebarSavedItem(node: node, depth: displayDepth))
                if node.kind == .folder {
                    let children = grouped[node.id] ?? []
                    pending.append(contentsOf: children.reversed().map {
                        ($0, rawDepth + 1)
                    })
                }
            }
        }

        appendForest(grouped[nil] ?? [], depth: 0)
        for node in nodes {
            guard !visited.contains(node.id) else { continue }
            appendForest([node], depth: 0)
        }
        return result
    }

    private var remoteDeviceGroups: [SidebarRemoteDeviceGroup] {
        Dictionary(grouping: model.snapshot.visibleRemoteTabs, by: \.deviceID)
            .map { deviceID, tabs in
                let name = tabs.first?.deviceName
                    ?? CompanionL10n.string("device.unknown", fallback: "Device")
                let kind = tabs.first?.deviceKind ?? .iPhone
                return SidebarRemoteDeviceGroup(
                    id: deviceID,
                    name: name,
                    symbol: deviceSymbol(kind),
                    tabs: tabs
                )
            }
            .sorted {
                let comparison = $0.name.localizedCaseInsensitiveCompare($1.name)
                if comparison != .orderedSame { return comparison == .orderedAscending }
                return $0.id < $1.id
            }
    }

    private func deviceSymbol(_ kind: DeviceKind) -> String {
        switch kind {
        case .mac: "desktopcomputer"
        case .iPhone: "iphone"
        case .iPad: "ipad"
        }
    }

    private func identifier(_ id: UUID) -> String {
        id.uuidString.lowercased()
    }
}

private struct SidebarSavedItem: Identifiable {
    let node: TreeNode
    let depth: Int

    var id: TreeNodeID { node.id }
}

private struct SidebarRemoteDeviceGroup: Identifiable {
    let id: DeviceID
    let name: String
    let symbol: String
    let tabs: [RemoteTab]
}
