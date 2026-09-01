import Foundation
import SwiftUI
import UIKit
import AhoiCloudKitSpike

struct MobileTabSwitcherSheet: View {
    @ObservedObject private var companionModel: CompanionAppModel
    @ObservedObject private var browser: MobileBrowserController
    @Binding private var isPresented: Bool
    @Binding private var selectedMode: MobileBrowsingMode
    @Binding private var renameTab: MobileTabRecord?
    @Binding private var renameText: String
    @State private var tabEditMode: EditMode = .inactive

    init(
        companionModel: CompanionAppModel,
        browser: MobileBrowserController,
        isPresented: Binding<Bool>,
        selectedMode: Binding<MobileBrowsingMode>,
        renameTab: Binding<MobileTabRecord?>,
        renameText: Binding<String>
    ) {
        _companionModel = ObservedObject(wrappedValue: companionModel)
        _browser = ObservedObject(wrappedValue: browser)
        _isPresented = isPresented
        _selectedMode = selectedMode
        _renameTab = renameTab
        _renameText = renameText
    }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Picker("", selection: $selectedMode) {
                        Text(CompanionL10n.string(
                            "browser.tabs.normal",
                            fallback: "Tabs"
                        )).tag(MobileBrowsingMode.normal)
                        Text(CompanionL10n.string(
                            "browser.tabs.private",
                            fallback: "Private"
                        )).tag(MobileBrowsingMode.privateBrowsing)
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .accessibilityLabel(CompanionL10n.string(
                        "browser.tabs.mode",
                        fallback: "Browsing mode"
                    ))
                    .accessibilityIdentifier("browser.tabs.mode")
                    .frame(minHeight: 44)
                }

                if selectedMode == .normal {
                    ForEach(companionModel.snapshot.visibleWorkspaces) { workspace in
                        let workspaceTabs = browser.normalTabs.filter {
                            $0.workspaceID == workspace.id
                        }
                        tabSections(workspaceTabs, workspaceName: workspace.name)
                    }
                    tabSections(
                        unassignedNormalTabs,
                        workspaceName: CompanionL10n.string(
                            "browser.tabs.unassigned",
                            fallback: "No workspace"
                        )
                    )
                } else if !browser.privateTabs.isEmpty {
                    Section(CompanionL10n.string(
                        "browser.tabs.private",
                        fallback: "Private"
                    )) {
                        ForEach(browser.privateTabs) { tab in tabRow(tab) }
                            .onMove { source, destination in
                                browser.reorderTabs(
                                    browser.privateTabs.map(\.id),
                                    fromOffsets: source,
                                    toOffset: destination
                                )
                            }
                    }
                } else {
                    Section {
                        ContentUnavailableView(
                            CompanionL10n.string(
                                "browser.tabs.private.empty",
                                fallback: "No Private Tabs"
                            ),
                            systemImage: "hand.raised"
                        )
                    }
                }

                if let closed = browser.recentlyClosedTab,
                   closed.mode == selectedMode {
                    Section {
                        Button {
                            browser.undoClose()
                            dismissSwitcher()
                            Task {
                                await companionModel.reconcilePublishedMobileTabs(browser.normalTabs)
                            }
                        } label: {
                            Label(
                                CompanionL10n.format(
                                    "browser.undo_close",
                                    fallback: "Reopen %@",
                                    closed.displayTitle
                                ),
                                systemImage: "arrow.uturn.backward"
                            )
                        }
                        .accessibilityIdentifier("browser.tabs.undo-close")
                    }
                }
            }
            .accessibilityIdentifier(
                tabEditMode.isEditing
                    ? "browser.tabs.reorder-list"
                    : "browser.tabs.list"
            )
            .navigationDestination(isPresented: Binding(
                get: { renameTab != nil },
                set: { presented in
                    if !presented { renameTab = nil }
                }
            )) {
                if let renameTab {
                    renameTabPage(renameTab)
                }
            }
            .navigationTitle(CompanionL10n.string("browser.tabs.title", fallback: "Tabs"))
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button(CompanionL10n.string("action.done", fallback: "Done")) {
                        dismissSwitcher()
                    }
                    .frame(minHeight: 44)
                    .accessibilityIdentifier("browser.tabs.done")
                }
                ToolbarItemGroup(placement: .topBarTrailing) {
                    if selectedMode == .normal || !browser.privateTabs.isEmpty {
                        Button {
                            tabEditMode = tabEditMode.isEditing ? .inactive : .active
                        } label: {
                            Text(CompanionL10n.string(
                                tabEditMode.isEditing ? "action.done" : "action.manage",
                                fallback: tabEditMode.isEditing ? "Done" : "Manage"
                            ))
                        }
                        .frame(minHeight: 44)
                        .accessibilityIdentifier("browser.tabs.edit")
                    }
                    Menu {
                        Button {
                            _ = browser.createTab(mode: selectedMode)
                            dismissSwitcher()
                        } label: {
                            Label(
                                selectedMode == .privateBrowsing
                                    ? CompanionL10n.string(
                                        "browser.new_private_tab",
                                        fallback: "New private tab"
                                    )
                                    : CompanionL10n.string(
                                        "browser.new_tab",
                                        fallback: "New tab"
                                    ),
                                systemImage: selectedMode == .privateBrowsing
                                    ? "hand.raised"
                                    : "plus"
                            )
                        }
                    } label: {
                        Image(systemName: "plus")
                            .frame(width: 44, height: 44)
                            .contentShape(Rectangle())
                    }
                    .accessibilityIdentifier("browser.tabs.new")
                    .accessibilityLabel(CompanionL10n.string(
                        selectedMode == .privateBrowsing
                            ? "browser.new_private_tab"
                            : "browser.new_tab",
                        fallback: selectedMode == .privateBrowsing
                            ? "New private tab"
                            : "New tab"
                    ))
                }
            }
        }
        .environment(\.editMode, $tabEditMode)
        .onChange(of: selectedMode) { _, _ in
            tabEditMode = .inactive
        }
        .onChange(of: isPresented) { _, presented in
            if !presented {
                tabEditMode = .inactive
                renameTab = nil
                renameText = ""
            }
        }
        .accessibilityAction(.escape) { dismissSwitcher() }
    }

    private func renameTabPage(_ tab: MobileTabRecord) -> some View {
        Form {
            TextField(
                CompanionL10n.string("browser.tab_name", fallback: "Tab name"),
                text: $renameText
            )
            .accessibilityIdentifier("browser.tabs.rename.field")
        }
        .accessibilityIdentifier("browser.tabs.rename.sheet")
        .navigationTitle(CompanionL10n.string(
            "browser.rename_tab",
            fallback: "Rename Tab"
        ))
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button(CompanionL10n.string("action.cancel", fallback: "Cancel")) {
                    renameTab = nil
                }
                .accessibilityIdentifier("browser.tabs.rename.cancel")
            }
            ToolbarItem(placement: .confirmationAction) {
                Button(CompanionL10n.string("action.save", fallback: "Save")) {
                    browser.renameTab(tab.id, title: renameText)
                    publishTab(tab.id)
                    renameTab = nil
                }
                .accessibilityIdentifier("browser.tabs.rename.save")
            }
        }
        .accessibilityAction(.escape) { renameTab = nil }
    }

    @ViewBuilder
    private func tabSections(
        _ tabs: [MobileTabRecord],
        workspaceName: String
    ) -> some View {
        let saved = tabs.filter(\.isSaved)
        let temporary = tabs.filter { !$0.isSaved }
        if !saved.isEmpty {
            Section(tabSectionTitle(
                workspaceName: workspaceName,
                kind: CompanionL10n.string("browser.tabs.saved", fallback: "Saved")
            )) {
                ForEach(saved) { tab in tabRow(tab) }
                    .onMove { source, destination in
                        browser.reorderTabs(
                            saved.map(\.id),
                            fromOffsets: source,
                            toOffset: destination
                        )
                    }
            }
        }
        if !temporary.isEmpty {
            Section(tabSectionTitle(
                workspaceName: workspaceName,
                kind: CompanionL10n.string("browser.tabs.temporary", fallback: "Temporary")
            )) {
                ForEach(temporary) { tab in tabRow(tab) }
                    .onMove { source, destination in
                        browser.reorderTabs(
                            temporary.map(\.id),
                            fromOffsets: source,
                            toOffset: destination
                        )
                    }
            }
        }
    }

    private func tabSectionTitle(workspaceName: String, kind: String) -> String {
        CompanionL10n.format(
            "browser.tabs.section",
            fallback: "%@ · %@",
            workspaceName,
            kind
        )
    }

    private func tabRow(_ tab: MobileTabRecord) -> some View {
        let isSelected = browser.selectedTabID == tab.id
        return HStack(spacing: 12) {
            Button {
                browser.select(tab.id)
                dismissSwitcher()
            } label: {
                HStack(spacing: 10) {
                    tabIcon(tab)
                    VStack(alignment: .leading, spacing: 2) {
                        HStack(spacing: 5) {
                            Text(tab.displayTitle)
                                .fontWeight(isSelected ? .semibold : .regular)
                                .lineLimit(1)
                            if tab.isSaved {
                                Image(systemName: "bookmark.fill")
                                    .font(.caption2)
                                    .foregroundStyle(.tint)
                                    .accessibilityHidden(true)
                            }
                        }
                        Text(tab.url ?? CompanionL10n.string("browser.new_tab", fallback: "New tab"))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    Spacer()
                    if isSelected {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(
                                tab.mode == .privateBrowsing
                                    ? Color.purple
                                    : Color.accentColor
                            )
                            .accessibilityHidden(true)
                    }
                }
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .hoverEffect(.highlight)
            .accessibilityIdentifier("browser.tab-row.\(tab.id.uuidString.lowercased())")
            .accessibilityValue(Text(isSelected
                ? CompanionL10n.string("browser.tabs.selected", fallback: "Selected")
                : ""))
            Button(role: .destructive) {
                closeTab(tab.id)
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.secondary)
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .accessibilityLabel(CompanionL10n.format(
                "browser.tab.close_named",
                fallback: "Close %@",
                tab.displayTitle
            ))
            .accessibilityIdentifier("browser.tab-close.\(tab.id.uuidString.lowercased())")
        }
        .listRowBackground(isSelected ? Color.accentColor.opacity(0.13) : Color.clear)
        .contextMenu {
            Button {
                Task { @MainActor in
                    await Task.yield()
                    guard isPresented,
                          let currentTab = browser.tabs.first(where: { $0.id == tab.id }) else {
                        return
                    }
                    renameText = currentTab.displayTitle
                    renameTab = currentTab
                }
            } label: {
                Label(CompanionL10n.string("action.rename", fallback: "Rename"), systemImage: "pencil")
            }
            .accessibilityIdentifier(
                "browser.tab-rename.\(tab.id.uuidString.lowercased())"
            )
            Button {
                browser.select(tab.id)
                _ = browser.duplicateSelectedTab()
                dismissSwitcher()
            } label: {
                Label(CompanionL10n.string("browser.duplicate_tab", fallback: "Duplicate Tab"), systemImage: "plus.square.on.square")
            }
            if tab.mode == .normal {
                Button {
                    browser.setTabSaved(tab.id, !tab.isSaved)
                    publishTab(tab.id)
                } label: {
                    Label(
                        tab.isSaved
                            ? CompanionL10n.string(
                                "browser.mark_temporary",
                                fallback: "Make Temporary"
                            )
                            : CompanionL10n.string(
                                "browser.mark_saved",
                                fallback: "Save Tab"
                            ),
                        systemImage: tab.isSaved ? "bookmark.slash" : "bookmark"
                    )
                }
                Menu {
                    Button(CompanionL10n.string(
                        "browser.tabs.unassigned",
                        fallback: "No workspace"
                    )) {
                        browser.moveTab(tab.id, to: nil)
                        publishTab(tab.id)
                    }
                    ForEach(companionModel.snapshot.visibleWorkspaces) { workspace in
                        Button(workspace.name) {
                            browser.moveTab(tab.id, to: workspace.id)
                            publishTab(tab.id)
                        }
                    }
                } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.move_to_workspace",
                            fallback: "Move to Workspace"
                        ),
                        systemImage: "folder"
                    )
                }
            }
        }
    }

    @ViewBuilder
    private func tabIcon(_ tab: MobileTabRecord) -> some View {
        Group {
            if tab.mode == .privateBrowsing {
                Image(systemName: "hand.raised.fill")
                    .foregroundStyle(.purple)
                    .frame(width: 24, height: 24)
            } else if let data = tab.faviconData, let image = UIImage(data: data) {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(width: 24, height: 24)
                    .clipShape(RoundedRectangle(cornerRadius: 5, style: .continuous))
            } else {
                Image(systemName: "globe")
                    .foregroundStyle(.secondary)
                    .frame(width: 24, height: 24)
            }
        }
        .accessibilityHidden(true)
    }

    private func closeTab(_ id: UUID) {
        let shouldRemovePublication = browser.tabs.first(where: { $0.id == id })?.mode == .normal
        browser.close(id)
        if shouldRemovePublication {
            Task { await companionModel.closePublishedMobileTab(id) }
        }
    }

    private func publishTab(_ id: UUID) {
        guard let tab = browser.tabs.first(where: { $0.id == id }),
              tab.mode == .normal else { return }
        Task { await companionModel.publishMobileTab(tab) }
    }

    private func dismissSwitcher() {
        tabEditMode = .inactive
        renameTab = nil
        renameText = ""
        isPresented = false
    }

    private var unassignedNormalTabs: [MobileTabRecord] {
        let workspaceIDs = Set(companionModel.snapshot.visibleWorkspaces.map(\.id))
        return browser.normalTabs.filter { tab in
            guard let workspaceID = tab.workspaceID else { return true }
            return !workspaceIDs.contains(workspaceID)
        }
    }
}
