import SwiftUI
import WebKit
import UIKit
import Combine
import AhoiCloudKitSpike

public struct AhoiMobileBrowserView: View {
    @ObservedObject private var companionModel: CompanionAppModel
    @ObservedObject private var browser: MobileBrowserController
    @ObservedObject private var permissions: MobilePermissionCoordinator
    @ObservedObject private var downloads: MobileDownloadCoordinator
    @Environment(\.horizontalSizeClass) private var horizontalSizeClass
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var addressPresented = false
    @State private var tabsPresented = false
    @State private var libraryPresented = false
    @State private var historyPresented = false
    @State private var settingsPresented = false
    @State private var downloadsPresented = false
    @State private var findNavigatorPresented = false
    @State private var clearWebsiteDataRequested = false
    @State private var downloadPreviewURL: URL?
    @State private var renameTab: MobileTabRecord?
    @State private var renameText = ""
    @State private var addressText = ""
    @State private var addressSelection: TextSelection?
    @State private var tabSwitcherMode: MobileBrowsingMode = .normal
    @State private var columnVisibility: NavigationSplitViewVisibility = .automatic
    @AppStorage(CompanionSyncPreferences.enabledKey) private var syncEnabled = false
    @AppStorage(MobileBrowserPreferences.searchEngineKey)
    private var searchEngineRawValue = MobileSearchEngine.duckDuckGo.rawValue

    public init(
        companionModel: CompanionAppModel,
        browser: MobileBrowserController
    ) {
        self.companionModel = companionModel
        self.browser = browser
        _permissions = ObservedObject(wrappedValue: browser.permissionCoordinator)
        _downloads = ObservedObject(wrappedValue: browser.downloadCoordinator)
    }

    public var body: some View {
        Group {
            if horizontalSizeClass == .regular {
                NavigationSplitView(columnVisibility: $columnVisibility) {
                    librarySidebar
                } detail: {
                    browserSurface
                }
            } else {
                browserSurface
            }
        }
        .task {
            await browser.load()
#if DEBUG
            let launchArguments = ProcessInfo.processInfo.arguments
            if launchArguments.contains("-AhoiUITestFixture") {
                browser.loadUITestFixture()
            }
            if launchArguments.contains("-AhoiUITestOffline") {
                browser.loadUITestOfflineFailure()
            }
#endif
            await companionModel.load()
            await companionModel.reconcilePublishedMobileTabs(browser.normalTabs)
            await companionModel.sync()
        }
        .onOpenURL { browser.handleExternalURL($0) }
        .onChange(of: syncEnabled) { _, enabled in
            Task { await companionModel.setSyncEnabled(enabled) }
        }
        .onChange(of: scenePhase) { _, phase in
            if phase == .background {
                browser.discardInactivePages(keeping: 2)
                Task { await browser.flushSession() }
            } else if phase == .active {
                Task {
                    await companionModel.reconcilePublishedMobileTabs(browser.normalTabs)
                    await companionModel.sync()
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(
            for: UIApplication.didReceiveMemoryWarningNotification
        )) { _ in
            browser.discardInactivePages(keeping: 1)
        }
        .sheet(isPresented: $addressPresented) { addressSheet }
        .sheet(isPresented: $tabsPresented) { tabSwitcher }
        .sheet(isPresented: $libraryPresented) { librarySheet }
        .sheet(isPresented: $historyPresented) {
            MobileBrowserHistoryView(model: companionModel) { url in
                browser.handleExternalURL(url)
            }
        }
        .sheet(isPresented: $downloadsPresented) { downloadsSheet }
        .sheet(item: $renameTab) { tab in renameTabSheet(tab) }
        .sheet(isPresented: $settingsPresented) {
            CompanionSettingsView(
                model: companionModel,
                syncEnabled: $syncEnabled
            )
        }
        .sheet(item: Binding<MobilePendingLink?>(
            get: { browser.pendingLink },
            set: { if $0 == nil { browser.dismissPendingLink() } }
        )) { link in
            MobileLinkActionSheet(
                link: link,
                companionModel: companionModel,
                browser: browser
            )
        }
        .confirmationDialog(
            CompanionL10n.string(
                "browser.clear_website_data.title",
                fallback: "Clear Website Data?"
            ),
            isPresented: $clearWebsiteDataRequested,
            titleVisibility: .visible
        ) {
            Button(
                CompanionL10n.string("action.clear", fallback: "Clear"),
                role: .destructive
            ) {
                Task {
                    await browser.clearWebsiteData()
                    browser.clearPrivateTabs()
                }
            }
            Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {}
        } message: {
            Text(CompanionL10n.string(
                "browser.clear_website_data.message",
                fallback: "Cookies, caches and other website storage on this device will be removed. Downloads and Ahoi workspaces stay intact."
            ))
        }
        .alert(
            CompanionL10n.string("browser.error.title", fallback: "AhoiBrowser"),
            isPresented: Binding(
                get: { browser.lastError != nil && !addressPresented },
                // Error state is cleared by the explicit button action. A
                // presentation binding can receive `false` while another
                // sheet is covering the root, which must not consume it.
                set: { _ in }
            )
        ) {
            Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                browser.dismissError()
            }
        } message: {
            Text(browser.lastError ?? "")
                .accessibilityIdentifier("browser.error.message")
        }
        .alert(
            CompanionL10n.string("browser.permission.title", fallback: "Website permission"),
            isPresented: Binding(
                get: { permissions.pendingRequest != nil },
                set: { if !$0 { permissions.deny() } }
            ),
            presenting: permissions.pendingRequest
        ) { _ in
            Button(CompanionL10n.string("action.deny", fallback: "Don't Allow"), role: .cancel) {
                permissions.deny()
            }
            Button(CompanionL10n.string("action.allow", fallback: "Allow")) {
                permissions.allow()
            }
        } message: { request in
            Text(CompanionL10n.format(
                "browser.permission.message",
                fallback: "%@ wants access to %@.",
                request.origin,
                permissionLabel(request.kind)
            ))
        }
        .alert(
            CompanionL10n.string("browser.external.title", fallback: "Open another app?"),
            isPresented: Binding(
                get: { browser.pendingExternalURL != nil },
                set: { if !$0 { browser.cancelPendingExternalURL() } }
            ),
            presenting: browser.pendingExternalURL
        ) { _ in
            Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                browser.cancelPendingExternalURL()
            }
            Button(CompanionL10n.string("browser.external.open", fallback: "Open App")) {
                if let url = browser.confirmPendingExternalURL() {
                    Task { _ = await UIApplication.shared.open(url) }
                }
            }
        } message: { url in
            Text(CompanionL10n.format(
                "browser.external.message",
                fallback: "%@ wants to open %@.",
                browser.pendingExternalOrigin ?? CompanionL10n.string(
                    "browser.external.unknown_origin",
                    fallback: "This website"
                ),
                url.scheme ?? url.absoluteString
            ))
        }
        .overlay {
            if let presenter = browser.selectedDialogPresenter {
                MobileWebDialogHost(presenter: presenter)
            }
        }
    }

    private var browserSurface: some View {
        VStack(spacing: 0) {
            ZStack {
                if browser.selectedPage?.url == nil {
                    newTabLanding
                } else if let page = browser.selectedPage {
                    MobileWebPageView(
                        page: page,
                        findNavigatorPresented: $findNavigatorPresented,
                        onRefresh: browser.reload
                    ) {
                        Task {
                            async let tint: Void = browser.refreshSelectedWebsiteTint()
                            async let favicon: Void = browser.refreshSelectedFavicon()
                            _ = await (tint, favicon)
                        }
                        if let navigation = browser.synchronizeSelectedPageMetadata() {
                            Task {
                                await companionModel.recordMobileNavigation(
                                    title: navigation.title,
                                    url: navigation.url.absoluteString
                                )
                                await companionModel.publishMobileTab(navigation.tab)
                            }
                        }
                    }
                }
                if let failure = browser.selectedPageFailure {
                    pageFailureView(failure)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(uiColor: .systemBackground))
            .overlay(alignment: .top) {
                if let page = browser.selectedPage, page.isLoading {
                    ProgressView(value: page.estimatedProgress)
                        .progressViewStyle(.linear)
                        .tint(chromeTintColor)
                        .accessibilityLabel(CompanionL10n.string(
                            "browser.loading",
                            fallback: "Loading page"
                        ))
                        .accessibilityValue(Text(CompanionL10n.format(
                            "browser.loading.progress",
                            fallback: "%d percent",
                            Int(page.estimatedProgress * 100)
                        )))
                }
            }
            bottomBar
        }
        .background(Color(uiColor: .systemBackground))
        .tint(chromeTintColor)
        .animation(
            reduceMotion ? nil : .easeInOut(duration: 0.34),
            value: browser.selectedTab?.websiteTintARGB
        )
    }

    private var newTabLanding: some View {
        ZStack {
            LinearGradient(
                colors: [chromeTintColor.opacity(0.11), .clear, chromeTintColor.opacity(0.045)],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            VStack(spacing: 18) {
                Image(systemName: browser.selectedTab?.mode == .privateBrowsing
                      ? "hand.raised.fill"
                      : "sailboat.fill")
                    .font(.system(size: 42, weight: .semibold))
                    .foregroundStyle(chromeTintColor)
                Text(browser.selectedTab?.mode == .privateBrowsing
                     ? CompanionL10n.string("browser.private", fallback: "Private")
                     : "AhoiBrowser")
                    .font(.title.bold())
                    .accessibilityIdentifier(
                        browser.selectedTab?.mode == .privateBrowsing
                            ? "browser.private-indicator"
                            : "browser.brand"
                    )
                Button {
                    presentAddress()
                } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.search_or_address",
                            fallback: "Search or enter address"
                        ),
                        systemImage: "magnifyingglass"
                    )
                    .frame(maxWidth: 420, alignment: .leading)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 13)
                    .background {
                        if reduceTransparency {
                            Capsule().fill(Color(uiColor: .secondarySystemBackground))
                        } else {
                            Capsule().fill(.thinMaterial)
                        }
                    }
                    .overlay {
                        Capsule().stroke(chromeTintColor.opacity(0.22), lineWidth: 1)
                            .allowsHitTesting(false)
                    }
                    .shadow(color: chromeTintColor.opacity(0.10), radius: 14, y: 6)
                }
                .buttonStyle(.plain)
            }
            .padding(24)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityIdentifier("browser.new-tab-landing")
    }

    private var bottomBar: some View {
        HStack(spacing: 10) {
            Button(action: browser.goBack) {
                Image(systemName: "chevron.backward")
            }
            .disabled(browser.selectedPage?.backForwardList.backList.isEmpty != false)
            .keyboardShortcut("[", modifiers: .command)
            .accessibilityLabel(CompanionL10n.string("browser.back", fallback: "Back"))

            Button(action: browser.goForward) {
                Image(systemName: "chevron.forward")
            }
            .disabled(browser.selectedPage?.backForwardList.forwardList.isEmpty != false)
            .keyboardShortcut("]", modifiers: .command)
            .accessibilityLabel(CompanionL10n.string("browser.forward", fallback: "Forward"))

            Button(action: presentAddress) {
                HStack(spacing: 7) {
                    if browser.selectedTab?.mode == .normal {
                        Image(systemName: "sailboat.fill")
                            .font(.caption2.weight(.bold))
                            .foregroundStyle(chromeTintColor)
                    }
                    Image(systemName: securitySymbol)
                        .font(.caption)
                    Text(addressLabel)
                        .lineLimit(1)
                        .font(.subheadline.weight(.medium))
                }
                .frame(maxWidth: .infinity)
                .padding(.horizontal, 12)
                .padding(.vertical, 10)
                .background {
                    if reduceTransparency {
                        Capsule().fill(Color(uiColor: .secondarySystemBackground))
                    } else {
                        Capsule().fill(.ultraThinMaterial)
                    }
                }
                .overlay {
                    ZStack {
                        Capsule()
                            .fill(chromeTintColor.opacity(reduceTransparency ? 0.12 : 0.075))
                        Capsule().stroke(chromeTintColor.opacity(0.18), lineWidth: 1)
                    }
                    .allowsHitTesting(false)
                }
            }
            .buttonStyle(.plain)
            .keyboardShortcut("l", modifiers: .command)
            .accessibilityIdentifier(
                browser.selectedTab?.mode == .privateBrowsing
                    ? "browser.address.private"
                    : "browser.address"
            )
            .accessibilityLabel(CompanionL10n.string(
                "browser.address.accessibility",
                fallback: "Address and search"
            ))

            Button(action: browser.reloadOrStop) {
                Image(systemName: browser.selectedPage?.isLoading == true ? "xmark" : "arrow.clockwise")
            }
            .keyboardShortcut("r", modifiers: .command)
            .accessibilityLabel(CompanionL10n.string("browser.reload", fallback: "Reload"))

            Button(action: presentTabs) {
                ZStack {
                    RoundedRectangle(cornerRadius: 5)
                        .stroke(lineWidth: 1.5)
                        .frame(width: 24, height: 24)
                    Text("\(browser.tabs.count)")
                        .font(.caption2.monospacedDigit().weight(.bold))
                }
            }
            .accessibilityIdentifier("browser.tabs")
            .accessibilityLabel(CompanionL10n.format(
                "browser.tabs.count",
                fallback: "%d tabs",
                browser.tabs.count
            ))

            Menu {
                Button {
                    _ = browser.createTab()
                } label: {
                    Label(CompanionL10n.string("browser.new_tab", fallback: "New tab"), systemImage: "plus")
                }
                .keyboardShortcut("t", modifiers: .command)
                Button {
                    _ = browser.createTab(mode: .privateBrowsing)
                } label: {
                    Label(CompanionL10n.string("browser.new_private_tab", fallback: "New private tab"), systemImage: "hand.raised.fill")
                }
                .accessibilityIdentifier("browser.new-private-tab")
                .keyboardShortcut("n", modifiers: [.command, .shift])
                if let page = browser.selectedPage, page.url != nil {
                    ShareLink(
                        item: page,
                        preview: SharePreview(page.title.isEmpty ? "AhoiBrowser" : page.title)
                    ) {
                        Label(CompanionL10n.string("browser.share", fallback: "Share Page"), systemImage: "square.and.arrow.up")
                    }
                    Button {
                        findNavigatorPresented = true
                    } label: {
                        Label(CompanionL10n.string("browser.find", fallback: "Find on Page"), systemImage: "text.magnifyingglass")
                    }
                    .keyboardShortcut("f", modifiers: .command)
                    Button {
                        UIPasteboard.general.url = page.url
                    } label: {
                        Label(CompanionL10n.string("browser.copy_address", fallback: "Copy Address"), systemImage: "doc.on.doc")
                    }
                    Menu {
                        Button {
                            browser.toggleDesktopSite()
                        } label: {
                            Label(
                                browser.selectedTabUsesDesktopSite
                                    ? CompanionL10n.string("browser.mobile_site", fallback: "Request Mobile Site")
                                    : CompanionL10n.string("browser.desktop_site", fallback: "Request Desktop Site"),
                                systemImage: "desktopcomputer"
                            )
                        }
                        Button { browser.adjustTextScale(by: 10) } label: {
                            Label(CompanionL10n.string("browser.text_larger", fallback: "Larger Text"), systemImage: "plus.magnifyingglass")
                        }
                        Button { browser.adjustTextScale(by: -10) } label: {
                            Label(CompanionL10n.string("browser.text_smaller", fallback: "Smaller Text"), systemImage: "minus.magnifyingglass")
                        }
                    } label: {
                        Label(CompanionL10n.string("browser.page_settings", fallback: "Page Settings"), systemImage: "textformat.size")
                    }
                    Button { _ = browser.duplicateSelectedTab() } label: {
                        Label(CompanionL10n.string("browser.duplicate_tab", fallback: "Duplicate Tab"), systemImage: "plus.square.on.square")
                    }
                    Button(role: .destructive) {
                        if let selectedTabID = browser.selectedTabID {
                            closeTab(selectedTabID)
                        }
                    } label: {
                        Label(CompanionL10n.string("browser.close_tab", fallback: "Close Tab"), systemImage: "xmark")
                    }
                    .keyboardShortcut("w", modifiers: .command)
                }
                Button { libraryPresented = true } label: {
                    Label(CompanionL10n.string("root.workspaces", fallback: "Workspaces"), systemImage: "sidebar.left")
                }
                Button { switchWorkspace(direction: -1) } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.workspace.previous",
                            fallback: "Previous workspace"
                        ),
                        systemImage: "arrow.left"
                    )
                }
                .disabled(companionModel.snapshot.visibleWorkspaces.isEmpty)
                .keyboardShortcut(.leftArrow, modifiers: [.command, .option])
                Button { switchWorkspace(direction: 1) } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.workspace.next",
                            fallback: "Next workspace"
                        ),
                        systemImage: "arrow.right"
                    )
                }
                .disabled(companionModel.snapshot.visibleWorkspaces.isEmpty)
                .keyboardShortcut(.rightArrow, modifiers: [.command, .option])
                if horizontalSizeClass == .regular {
                    Button(action: toggleSidebar) {
                        Label(
                            CompanionL10n.string(
                                "browser.sidebar.toggle",
                                fallback: "Toggle sidebar"
                            ),
                            systemImage: "sidebar.left"
                        )
                    }
                    .keyboardShortcut("s", modifiers: [.command, .shift])
                }
                Button { historyPresented = true } label: {
                    Label(
                        CompanionL10n.string("browser.history.title", fallback: "History"),
                        systemImage: "clock.arrow.circlepath"
                    )
                }
                if browser.selectedPage?.url != nil,
                   browser.selectedTab?.mode == .normal,
                   !companionModel.snapshot.visibleWorkspaces.isEmpty {
                    Menu {
                        ForEach(companionModel.snapshot.visibleWorkspaces) { workspace in
                            Button(workspace.name) { saveSelectedPage(to: workspace) }
                        }
                    } label: {
                        Label(
                            CompanionL10n.string("browser.save_to_workspace", fallback: "Save to workspace"),
                            systemImage: "bookmark"
                        )
                    }
                }
                Button { downloadsPresented = true } label: {
                    Label(
                        CompanionL10n.format(
                            "browser.downloads.count",
                            fallback: "Downloads (%d)",
                            downloads.downloads.count
                        ),
                        systemImage: "arrow.down.circle"
                    )
                }
                if !browser.privateTabs.isEmpty {
                    Button(role: .destructive) { browser.clearPrivateTabs() } label: {
                        Label(CompanionL10n.string("browser.private.close_all", fallback: "Close Private Tabs"), systemImage: "hand.raised.slash")
                    }
                }
                Button(role: .destructive) {
                    clearWebsiteDataRequested = true
                } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.clear_website_data",
                            fallback: "Clear Website Data"
                        ),
                        systemImage: "trash"
                    )
                }
                .accessibilityIdentifier("browser.clear-website-data")
                Button { settingsPresented = true } label: {
                    Label(CompanionL10n.string("settings.title", fallback: "Settings"), systemImage: "gearshape")
                }
            } label: {
                Image(systemName: "ellipsis.circle")
            }
            .accessibilityIdentifier("browser.more")
        }
        .font(.body.weight(.semibold))
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .background {
            if reduceTransparency {
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .fill(Color(uiColor: .secondarySystemBackground))
            } else {
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .fill(.regularMaterial)
            }
        }
        .overlay {
            ZStack {
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .fill(chromeTintColor.opacity(reduceTransparency ? 0.13 : 0.085))
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .stroke(chromeTintColor.opacity(0.20), lineWidth: 1)
            }
            .allowsHitTesting(false)
        }
        .shadow(color: Color.black.opacity(0.13), radius: 16, y: 7)
        .padding(.horizontal, horizontalSizeClass == .regular ? 16 : 8)
        .padding(.vertical, 7)
        .background(Color(uiColor: .systemBackground))
        .simultaneousGesture(
            DragGesture(minimumDistance: 28).onEnded { value in
                let horizontal = value.translation.width
                let vertical = abs(value.translation.height)
                guard abs(horizontal) >= 72, abs(horizontal) > vertical * 1.35 else {
                    return
                }
                switchWorkspace(direction: horizontal < 0 ? 1 : -1)
            }
        )
        .accessibilityAction(named: Text(CompanionL10n.string(
            "browser.workspace.next",
            fallback: "Next workspace"
        ))) {
            switchWorkspace(direction: 1)
        }
        .accessibilityAction(named: Text(CompanionL10n.string(
            "browser.workspace.previous",
            fallback: "Previous workspace"
        ))) {
            switchWorkspace(direction: -1)
        }
    }

    private func pageFailureView(_ failure: MobilePageFailureKind) -> some View {
        ContentUnavailableView {
            Label(pageFailureTitle(failure), systemImage: failure == .offline
                  ? "wifi.slash"
                  : "exclamationmark.icloud")
        } description: {
            Text(pageFailureDescription(failure))
        } actions: {
            Button(action: browser.retrySelectedPage) {
                Label(
                    CompanionL10n.string("browser.retry", fallback: "Try Again"),
                    systemImage: "arrow.clockwise"
                )
                .accessibilityIdentifier("browser.retry")
            }
            .buttonStyle(.borderedProminent)
            .accessibilityIdentifier("browser.retry")
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(uiColor: .systemBackground))
        .accessibilityIdentifier("browser.page-failure")
    }

    private func pageFailureTitle(_ failure: MobilePageFailureKind) -> String {
        switch failure {
        case .offline:
            CompanionL10n.string("browser.failure.offline.title", fallback: "You're Offline")
        case .timedOut:
            CompanionL10n.string("browser.failure.timeout.title", fallback: "The Page Took Too Long")
        case .webContentTerminated:
            CompanionL10n.string("browser.failure.process.title", fallback: "Page Reload Required")
        case .invalidURL:
            CompanionL10n.string("browser.failure.invalid.title", fallback: "This Address Can't Be Opened")
        case .failed:
            CompanionL10n.string("browser.failure.generic.title", fallback: "Page Couldn't Load")
        }
    }

    private func pageFailureDescription(_ failure: MobilePageFailureKind) -> String {
        switch failure {
        case .offline:
            CompanionL10n.string("browser.failure.offline.message", fallback: "Check your connection and try again.")
        case .timedOut:
            CompanionL10n.string("browser.failure.timeout.message", fallback: "The website did not respond in time.")
        case .webContentTerminated:
            CompanionL10n.string("browser.failure.process.message", fallback: "iOS released the page process. Reload it to continue.")
        case .invalidURL:
            CompanionL10n.string("browser.failure.invalid.message", fallback: "Check the address and try again.")
        case .failed:
            CompanionL10n.string("browser.failure.generic.message", fallback: "The website could not be reached.")
        }
    }

    private var addressSheet: some View {
        MobileAddressCommandSheet(
            companionModel: companionModel,
            browser: browser,
            isPresented: $addressPresented,
            addressText: $addressText,
            addressSelection: $addressSelection,
            searchEngine: MobileSearchEngine.resolved(from: searchEngineRawValue)
        )
    }

    private var tabSwitcher: some View {
        MobileTabSwitcherSheet(
            companionModel: companionModel,
            browser: browser,
            isPresented: $tabsPresented,
            selectedMode: $tabSwitcherMode,
            renameTab: $renameTab,
            renameText: $renameText
        )
    }

    private var downloadsSheet: some View {
        MobileDownloadsSheet(
            downloads: downloads,
            isPresented: $downloadsPresented,
            previewURL: $downloadPreviewURL
        )
    }

    private func renameTabSheet(_ tab: MobileTabRecord) -> some View {
        NavigationStack {
            Form {
                TextField(
                    CompanionL10n.string("browser.tab_name", fallback: "Tab name"),
                    text: $renameText
                )
            }
            .navigationTitle(CompanionL10n.string("browser.rename_tab", fallback: "Rename Tab"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(CompanionL10n.string("action.cancel", fallback: "Cancel")) {
                        renameTab = nil
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(CompanionL10n.string("action.save", fallback: "Save")) {
                        browser.renameTab(tab.id, title: renameText)
                        renameTab = nil
                    }
                }
            }
        }
    }

    private var librarySidebar: some View {
        CompanionRootView(
            model: companionModel,
            openURL: browserOpenURLAction,
            accentTint: chromeTintColor
        )
    }

    private var librarySheet: some View {
        CompanionRootView(
            model: companionModel,
            openURL: browserOpenURLAction,
            accentTint: chromeTintColor
        )
        .overlay(alignment: .topTrailing) {
            Button(CompanionL10n.string("action.done", fallback: "Done")) {
                libraryPresented = false
            }
            .buttonStyle(.borderedProminent)
            .padding(.top, 16)
            .padding(.trailing, 18)
            .accessibilityIdentifier("browser.library.done")
        }
    }

    private var browserOpenURLAction: OpenURLAction {
        OpenURLAction { url in
            browser.handleExternalURL(url)
            libraryPresented = false
            return .handled
        }
    }

    private var addressLabel: String {
        if browser.selectedTab?.mode == .privateBrowsing {
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        if let url = browser.selectedPage?.url {
            return url.host() ?? url.absoluteString
        }
        return CompanionL10n.string("browser.search_or_address", fallback: "Search or address")
    }

    private var securitySymbol: String {
        browser.selectedPage?.url?.scheme?.lowercased() == "https" ? "lock.fill" : "globe"
    }

    private var chromeTintColor: Color {
        if browser.selectedTab?.mode == .privateBrowsing { return .purple }
        guard let argb = browser.selectedTab?.websiteTintARGB else {
            return Color(red: 0.10, green: 0.43, blue: 0.84)
        }
        return Color(
            red: Double((argb >> 16) & 0xFF) / 255,
            green: Double((argb >> 8) & 0xFF) / 255,
            blue: Double(argb & 0xFF) / 255
        )
    }

    private func presentAddress() {
        addressText = browser.selectedPage?.url?.absoluteString ?? ""
        selectAllAddressText()
        addressPresented = true
    }

    private func presentTabs() {
        tabSwitcherMode = browser.selectedTab?.mode ?? .normal
        tabsPresented = true
    }

    private func switchWorkspace(direction: Int) {
        let workspaces = companionModel.snapshot.visibleWorkspaces
        guard !workspaces.isEmpty else { return }
        let currentID = browser.selectedTab?.workspaceID
        let targetIndex: Int
        if let currentIndex = workspaces.firstIndex(where: { $0.id == currentID }) {
            targetIndex = (currentIndex + direction + workspaces.count) % workspaces.count
        } else {
            targetIndex = direction < 0 ? workspaces.count - 1 : 0
        }
        let target = workspaces[targetIndex]
        if let tab = browser.normalTabs
            .filter({ $0.workspaceID == target.id })
            .max(by: { $0.lastActiveAt < $1.lastActiveAt }) {
            browser.select(tab.id)
        } else {
            _ = browser.createTab(workspaceID: target.id)
        }
        Task {
            await companionModel.reconcilePublishedMobileTabs(browser.normalTabs)
        }
    }

    private func toggleSidebar() {
        columnVisibility = columnVisibility == .detailOnly ? .all : .detailOnly
    }

    private func selectAllAddressText() {
        addressSelection = TextSelection(range: addressText.startIndex..<addressText.endIndex)
    }

    private func saveSelectedPage(to workspace: Workspace) {
        guard let url = browser.selectedPage?.url?.absoluteString else { return }
        let title = browser.selectedPage?.title.isEmpty == false
            ? browser.selectedPage?.title ?? url
            : url
        Task {
            let node = await companionModel.createSavedPage(
                workspaceID: workspace.id,
                title: title,
                url: url
            )
            guard node != nil else { return }
            browser.moveSelectedTab(to: workspace.id)
            browser.setSelectedTabSaved(true)
            if let tab = browser.selectedTab {
                await companionModel.publishMobileTab(tab)
            }
        }
    }

    private func closeTab(_ id: UUID) {
        let shouldRemovePublication = browser.tabs.first(where: { $0.id == id })?.mode == .normal
        browser.close(id)
        if shouldRemovePublication {
            Task { await companionModel.closePublishedMobileTab(id) }
        }
    }

    private func permissionLabel(_ kind: MobilePermissionRequest.Kind) -> String {
        switch kind {
        case .camera:
            return CompanionL10n.string("browser.permission.camera", fallback: "the camera")
        case .microphone:
            return CompanionL10n.string("browser.permission.microphone", fallback: "the microphone")
        case .cameraAndMicrophone:
            return CompanionL10n.string("browser.permission.camera_microphone", fallback: "the camera and microphone")
        case .motion:
            return CompanionL10n.string("browser.permission.motion", fallback: "motion sensors")
        }
    }
}
