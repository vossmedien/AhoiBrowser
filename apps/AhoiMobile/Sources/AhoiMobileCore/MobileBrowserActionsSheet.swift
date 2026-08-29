import SwiftUI
import UIKit
import AhoiCloudKitSpike

struct MobileBrowserActionsSheet: View {
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
        self.onCloseSelectedTab = onCloseSelectedTab
        self.onClearPrivateTabs = onClearPrivateTabs
        self.onClearWebsiteData = onClearWebsiteData
    }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    LazyVGrid(
                        columns: [GridItem(.adaptive(minimum: 78, maximum: 110), spacing: 10)],
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
                        Button { onSwitchWorkspace(1) } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.workspace.next",
                                    fallback: "Next workspace"
                                ),
                                systemImage: "arrow.right"
                            )
                        }
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

                    if browser.selectedTab?.mode == .normal,
                       !companionModel.snapshot.visibleWorkspaces.isEmpty {
                        Menu {
                            ForEach(companionModel.snapshot.visibleWorkspaces) { workspace in
                                Button(workspace.name) { onSaveToWorkspace(workspace) }
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
                    }

                    Button(action: onPresentHistory) {
                        Label(
                            CompanionL10n.string("browser.history.title", fallback: "History"),
                            systemImage: "clock.arrow.circlepath"
                        )
                    }

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
                }
            }
        }
        .presentationDetents([.medium, .large])
        .presentationDragIndicator(.visible)
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
    }

    private func actionTileLabel(title: String, systemImage: String) -> some View {
        VStack(spacing: 8) {
            Image(systemName: systemImage)
                .font(.title3.weight(.semibold))
                .frame(height: 24)
            Text(title)
                .font(.caption.weight(.semibold))
                .multilineTextAlignment(.center)
                .lineLimit(2)
        }
        .frame(maxWidth: .infinity, minHeight: 78)
        .padding(.horizontal, 6)
        .background(Color(uiColor: .secondarySystemGroupedBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .contentShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
    }
}
