import SwiftUI
import UIKit
import AhoiCloudKitSpike

struct MobileBrowserActionsSheet: View {
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @ObservedObject private var companionModel: CompanionAppModel
    @ObservedObject private var browser: MobileBrowserController
    @Binding private var isPresented: Bool

    private let isRegularWidth: Bool
    private let visibleDownloadCount: Int
    private let onFindOnPage: @MainActor () -> Void
    private let onPresentLibrary: @MainActor () -> Void
    private let onPresentHistory: @MainActor () -> Void
    private let onPresentDownloads: @MainActor () -> Void
    private let onPresentSettings: @MainActor () -> Void
    private let onToggleSidebar: @MainActor () -> Void
    private let onSwitchWorkspace: @MainActor (Int) -> Void
    private let onSaveToWorkspace: @MainActor (Workspace) -> Void
    private let onSaveBookmark: @MainActor () -> Void
    private let onCloseSelectedTab: @MainActor () -> Void
    private let onClearPrivateTabs: @MainActor () -> Void
    private let onClearWebsiteData: @MainActor () -> Void

    init(
        companionModel: CompanionAppModel,
        browser: MobileBrowserController,
        isPresented: Binding<Bool>,
        isRegularWidth: Bool,
        visibleDownloadCount: Int,
        onFindOnPage: @escaping @MainActor () -> Void,
        onPresentLibrary: @escaping @MainActor () -> Void,
        onPresentHistory: @escaping @MainActor () -> Void,
        onPresentDownloads: @escaping @MainActor () -> Void,
        onPresentSettings: @escaping @MainActor () -> Void,
        onToggleSidebar: @escaping @MainActor () -> Void,
        onSwitchWorkspace: @escaping @MainActor (Int) -> Void,
        onSaveToWorkspace: @escaping @MainActor (Workspace) -> Void,
        onSaveBookmark: @escaping @MainActor () -> Void,
        onCloseSelectedTab: @escaping @MainActor () -> Void,
        onClearPrivateTabs: @escaping @MainActor () -> Void,
        onClearWebsiteData: @escaping @MainActor () -> Void
    ) {
        _companionModel = ObservedObject(wrappedValue: companionModel)
        _browser = ObservedObject(wrappedValue: browser)
        _isPresented = isPresented
        self.isRegularWidth = isRegularWidth
        self.visibleDownloadCount = visibleDownloadCount
        self.onFindOnPage = onFindOnPage
        self.onPresentLibrary = onPresentLibrary
        self.onPresentHistory = onPresentHistory
        self.onPresentDownloads = onPresentDownloads
        self.onPresentSettings = onPresentSettings
        self.onToggleSidebar = onToggleSidebar
        self.onSwitchWorkspace = onSwitchWorkspace
        self.onSaveToWorkspace = onSaveToWorkspace
        self.onSaveBookmark = onSaveBookmark
        self.onCloseSelectedTab = onCloseSelectedTab
        self.onClearPrivateTabs = onClearPrivateTabs
        self.onClearWebsiteData = onClearWebsiteData
    }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    LazyVGrid(
                        columns: [GridItem(
                            .adaptive(
                                minimum: dynamicTypeSize.isAccessibilitySize ? 140 : 78,
                                maximum: dynamicTypeSize.isAccessibilitySize ? 220 : 110
                            ),
                            spacing: 10
                        )],
                        spacing: 10
                    ) {
                        actionTile(
                            title: CompanionL10n.string("browser.new_tab", fallback: "New tab"),
                            systemImage: "plus"
                        ) {
                            _ = browser.createTab()
                            isPresented = false
                        }
                        .accessibilityIdentifier("browser.actions.new-tab")

                        actionTile(
                            title: CompanionL10n.string(
                                "browser.new_private_tab",
                                fallback: "Private tab"
                            ),
                            systemImage: "hand.raised.fill"
                        ) {
                            _ = browser.createTab(mode: .privateBrowsing)
                            isPresented = false
                        }
                        .accessibilityIdentifier("browser.new-private-tab")

                        actionTile(
                            title: CompanionL10n.string("root.workspaces", fallback: "Workspaces"),
                            systemImage: "square.grid.2x2"
                        ) {
                            onPresentLibrary()
                        }
                        .accessibilityIdentifier("browser.actions.workspaces")

                        if let page = browser.selectedPage, page.url != nil {
                            ShareLink(
                                item: page,
                                preview: SharePreview(page.title.isEmpty ? "AhoiBrowser" : page.title)
                            ) {
                                actionTileLabel(
                                    title: CompanionL10n.string(
                                        "browser.share",
                                        fallback: "Share page"
                                    ),
                                    systemImage: "square.and.arrow.up"
                                )
                            }
                            .buttonStyle(.plain)
                            .hoverEffect(.highlight)
                            .accessibilityIdentifier("browser.actions.share")
                        }
                    }
                    .padding(.vertical, 4)
                }
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets())

                if let page = browser.selectedPage, page.url != nil {
                    Section(CompanionL10n.string("browser.actions.page", fallback: "Page")) {
                        Button(action: onFindOnPage) {
                            Label(
                                CompanionL10n.string("browser.find", fallback: "Find on Page"),
                                systemImage: "text.magnifyingglass"
                            )
                        }
                        .keyboardShortcut("f", modifiers: .command)

                        Button {
                            UIPasteboard.general.url = page.url
                        } label: {
                            Label(
                                CompanionL10n.string("browser.copy_address", fallback: "Copy Address"),
                                systemImage: "doc.on.doc"
                            )
                        }

                        Menu {
                            Button {
                                browser.toggleDesktopSite()
                            } label: {
                                Label(
                                    browser.selectedTabUsesDesktopSite
                                        ? CompanionL10n.string(
                                            "browser.mobile_site",
                                            fallback: "Request Mobile Site"
                                        )
                                        : CompanionL10n.string(
                                            "browser.desktop_site",
                                            fallback: "Request Desktop Site"
                                        ),
                                    systemImage: "desktopcomputer"
                                )
                            }
                            Button { browser.adjustTextScale(by: 10) } label: {
                                Label(
                                    CompanionL10n.string(
                                        "browser.text_larger",
                                        fallback: "Larger Text"
                                    ),
                                    systemImage: "plus.magnifyingglass"
                                )
                            }
                            Button { browser.adjustTextScale(by: -10) } label: {
                                Label(
                                    CompanionL10n.string(
                                        "browser.text_smaller",
                                        fallback: "Smaller Text"
                                    ),
                                    systemImage: "minus.magnifyingglass"
                                )
                            }
                        } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.page_settings",
                                    fallback: "Page Settings"
                                ),
                                systemImage: "textformat.size"
                            )
                        }

                        Button { _ = browser.duplicateSelectedTab() } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.duplicate_tab",
                                    fallback: "Duplicate Tab"
                                ),
                                systemImage: "plus.square.on.square"
                            )
                        }

                        Button(role: .destructive) {
                            onCloseSelectedTab()
                            isPresented = false
                        } label: {
                            Label(
                                CompanionL10n.string("browser.close_tab", fallback: "Close Tab"),
                                systemImage: "xmark"
                            )
                        }
                        .keyboardShortcut("w", modifiers: .command)
                    }
                }

                Section("Ahoi") {
                    Button(action: onPresentLibrary) {
                        Label(
                            CompanionL10n.string("root.workspaces", fallback: "Workspaces"),
                            systemImage: "sidebar.left"
                        )
                    }
                    .accessibilityIdentifier("browser.actions.library")

                    Menu {
                        Button { onSwitchWorkspace(-1) } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.workspace.previous",
                                    fallback: "Previous workspace"
                                ),
                                systemImage: "arrow.left"
                            )
                        }
                        .accessibilityIdentifier("browser.actions.workspace-previous")
                        Button { onSwitchWorkspace(1) } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.workspace.next",
                                    fallback: "Next workspace"
                                ),
                                systemImage: "arrow.right"
                            )
                        }
                        .accessibilityIdentifier("browser.actions.workspace-next")
                    } label: {
                        Label(
                            CompanionL10n.string(
                                "browser.workspace.switch",
                                fallback: "Switch Workspace"
                            ),
                            systemImage: "arrow.left.arrow.right"
                        )
                    }
                    .disabled(companionModel.snapshot.visibleWorkspaces.isEmpty)
                    .accessibilityIdentifier("browser.actions.workspace-switch")

                    if browser.selectedTab?.mode == .normal, browser.selectedTab?.url != nil {
                        Button(action: onSaveBookmark) {
                            Label(CompanionL10n.string("bookmark.add", fallback: "Add Bookmark"),
                                  systemImage: "bookmark")
                        }
                        .accessibilityIdentifier("browser.actions.add-bookmark")
                    }
                    if browser.selectedTab?.mode == .normal,
                       !companionModel.snapshot.visibleWorkspaces.isEmpty {
                        Menu {
                            ForEach(companionModel.snapshot.visibleWorkspaces) { workspace in
                                Button(workspace.name) { onSaveToWorkspace(workspace) }
                                    .accessibilityIdentifier(
                                        "browser.actions.save-to-workspace.\(workspace.id.rawValue.uuidString.lowercased())"
                                    )
                            }
                        } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.save_to_workspace",
                                    fallback: "Save to Workspace"
                                ),
                                systemImage: "bookmark"
                            )
                        }
                        .accessibilityIdentifier("browser.actions.save-to-workspace")
                    }

                    Button(action: onPresentHistory) {
                        Label(
                            CompanionL10n.string("browser.history.title", fallback: "History"),
                            systemImage: "clock.arrow.circlepath"
                        )
                    }
                    .accessibilityIdentifier("browser.actions.history")

                    Button(action: onPresentDownloads) {
                        Label(
                            CompanionL10n.format(
                                "browser.downloads.count",
                                fallback: "Downloads (%d)",
                                visibleDownloadCount
                            ),
                            systemImage: "arrow.down.circle"
                        )
                    }
                    .accessibilityIdentifier("browser.actions.downloads")

                    if isRegularWidth {
                        Button(action: onToggleSidebar) {
                            Label(
                                CompanionL10n.string(
                                    "browser.sidebar.toggle",
                                    fallback: "Toggle Sidebar"
                                ),
                                systemImage: "sidebar.left"
                            )
                        }
                        .keyboardShortcut("s", modifiers: [.command, .shift])
                    }
                }

                Section(CompanionL10n.string("browser.actions.browser", fallback: "Browser")) {
                    Button(action: onPresentSettings) {
                        Label(
                            CompanionL10n.string("settings.title", fallback: "Settings"),
                            systemImage: "gearshape"
                        )
                    }
                    .accessibilityIdentifier("browser.actions.settings")

#if DEBUG
                    if exposesMemoryPressureE2EControl {
                        Button {
                            NotificationCenter.default.post(
                                name: UIApplication.didReceiveMemoryWarningNotification,
                                object: nil
                            )
                            isPresented = false
                        } label: {
                            Label("Simulate Memory Warning", systemImage: "memorychip")
                        }
                        .accessibilityIdentifier("browser.e2e.memory-warning")
                    }
#endif
                }

                Section(CompanionL10n.string("browser.actions.data", fallback: "Privacy & Data")) {
                    if !browser.privateTabs.isEmpty {
                        Button(role: .destructive, action: onClearPrivateTabs) {
                            Label(
                                CompanionL10n.string(
                                    "browser.private.close_all",
                                    fallback: "Close Private Tabs"
                                ),
                                systemImage: "hand.raised.slash"
                            )
                        }
                    }
                    Button(role: .destructive, action: onClearWebsiteData) {
                        Label(
                            CompanionL10n.string(
                                "browser.clear_website_data",
                                fallback: "Clear Website Data"
                            ),
                            systemImage: "trash"
                        )
                    }
                    .accessibilityIdentifier("browser.clear-website-data")
                }
            }
            .listStyle(.insetGrouped)
            .accessibilityIdentifier("browser.actions.list")
            .navigationTitle(CompanionL10n.string(
                "browser.actions.title",
                fallback: "Browser Actions"
            ))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(CompanionL10n.string("action.done", fallback: "Done")) {
                        isPresented = false
                    }
                    .accessibilityIdentifier("browser.actions.done")
                }
            }
        }
        .presentationDetents([.medium, .large])
        .presentationDragIndicator(.visible)
        .accessibilityAction(.escape) { isPresented = false }
    }

    private func actionTile(
        title: String,
        systemImage: String,
        action: @escaping @MainActor () -> Void
    ) -> some View {
        Button(action: action) {
            actionTileLabel(title: title, systemImage: systemImage)
        }
        .buttonStyle(.plain)
        .hoverEffect(.highlight)
    }

#if DEBUG
    private var exposesMemoryPressureE2EControl: Bool {
        ProcessInfo.processInfo.arguments.filter {
            $0 == "-AhoiUITestMemoryPressureControl"
        }.count == 1
    }
#endif

    private func actionTileLabel(title: String, systemImage: String) -> some View {
        VStack(spacing: 8) {
            Image(systemName: systemImage)
                .font(.title3.weight(.semibold))
                .frame(height: 24)
            Text(title)
                .font(.caption.weight(.semibold))
                .multilineTextAlignment(.center)
                .lineLimit(dynamicTypeSize.isAccessibilitySize ? nil : 2)
        }
        .frame(maxWidth: .infinity, minHeight: 78)
        .padding(.horizontal, 6)
        .background(Color(uiColor: .secondarySystemGroupedBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .contentShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
    }
}
