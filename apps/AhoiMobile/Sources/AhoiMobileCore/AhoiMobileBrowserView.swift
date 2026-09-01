import Foundation
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
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.colorScheme) private var colorScheme
    @Environment(\.mobileBrowserCommandRouter) private var commandRouter
    @State private var addressPresented = false
    @State private var tabsPresented = false
    @State private var libraryPresented = false
    @State private var historyPresented = false
    @State private var settingsPresented = false
    @State private var downloadsPresented = false
    @State private var browserActionsPresented = false
    @State private var findNavigatorPresented = false
    @State private var harborDeckCollapsed = false
    @State private var harborDeckResetGeneration: UInt64 = 0
    @State private var clearWebsiteDataRequested = false
    @State private var clearPrivateTabsRequested = false
    @State private var downloadPreviewURL: URL?
    @State private var renameTab: MobileTabRecord?
    @State private var renameText = ""
    @State private var addressText = ""
    @State private var addressSelection: TextSelection?
    @State private var tabSwitcherMode: MobileBrowsingMode = .normal
    @State private var columnVisibility: NavigationSplitViewVisibility = .all
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
        MobileE2EEvidenceOverlay(content: finalPresentationLayer)
            .mobileBrowserCommandRegistration(browserCommandActions, router: commandRouter)
            .background(MobileBrowserKeyboardFocusAnchor(router: commandRouter).frame(width: 1, height: 1))
    }
    private var privacyLayer: some View {
        ZStack {
            adaptiveBrowserLayout

            if privatePrivacyCoverPresented {
                privatePrivacyCover
                    .zIndex(10_000)
            }
        }
        .environment(
            \.mobileBrowserReduceMotionOverride,
            resolvedPerformanceReduceMotionOverride
        )
        .transaction { transaction in
            if privatePrivacyCoverPresented {
                transaction.animation = nil
            }
        }
    }
    private var lifecycleLayer: some View {
        privacyLayer
        .task {
            let launchArguments = ProcessInfo.processInfo.arguments
            switch MobilePerformanceLaunchRequest.validate(arguments: launchArguments) {
            case let .valid(request):
#if DEBUG
                browser.loadPerformanceFixture(request)
                await browser.runPerformanceWorkload(request)
#endif
                return
            case .invalid:
                return
            case .notRequested:
                break
            }
            await browser.load()
#if DEBUG
            if launchArguments.contains("-AhoiUITestFixture") {
                browser.loadUITestFixture()
            }
            if launchArguments.contains("-AhoiUITestOffline") {
                browser.loadUITestOfflineFailure()
            }
#endif
            await companionModel.load()
            await companionModel.setSyncEnabled(syncEnabled)
            await companionModel.reconcilePublishedMobileTabs(browser.normalTabs)
#if DEBUG
            await companionModel.loadSyncVisibleUITestConflictIfRequested()
#endif
            await companionModel.sync()
        }
        .onOpenURL { browser.handleExternalURL($0) }
        .onChange(of: syncEnabled) { _, enabled in
            guard !isPerformanceRuntime else { return }
            Task { await companionModel.setSyncEnabled(enabled) }
        }
        .onChange(of: scenePhase) { _, phase in
            if phase != .active {
                browser.prepareForInactiveScene()
            }
            if phase == .background {
                browser.discardInactivePages(keeping: 2)
                flushSessionDuringBackgroundTransition()
            } else if phase == .active {
                expandHarborDeck()
                guard !isPerformanceRuntime else { return }
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
    }
    private var sheetLayer: some View {
        lifecycleLayer
        .sheet(isPresented: $addressPresented) { addressSheet }
        .sheet(isPresented: $tabsPresented) { tabSwitcher }
        .sheet(isPresented: $libraryPresented) { librarySheet }
        .sheet(isPresented: $historyPresented) {
            MobileBrowserHistoryView(model: companionModel) { url in
                browser.handleExternalURL(url)
            }
        }
        .sheet(isPresented: $downloadsPresented) { downloadsSheet }
        .sheet(isPresented: $browserActionsPresented) { browserActionsSheet }
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
    }
    private var confirmationLayer: some View {
        sheetLayer
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
        .confirmationDialog(
            CompanionL10n.string(
                "browser.private.close_all.confirmation",
                fallback: "Close all private tabs?"
            ),
            isPresented: $clearPrivateTabsRequested,
            titleVisibility: .visible
        ) {
            Button(
                CompanionL10n.string("browser.private.close_all", fallback: "Close Private Tabs"),
                role: .destructive
            ) {
                browser.clearPrivateTabs()
            }
            Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {}
        } message: {
            Text(CompanionL10n.string(
                "browser.private.close_all.message",
                fallback: "Open private tabs and their in-memory browsing session will be discarded."
            ))
        }
    }
    private var finalPresentationLayer: some View {
        confirmationLayer
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
            .accessibilityIdentifier("browser.error.dismiss")
        } message: {
            Text(browser.lastError ?? "")
                .accessibilityIdentifier("browser.error.message")
        }
        .mobileBrowserSystemAlerts(
            browser: browser,
            permissions: permissions
        )
        .overlay {
            if let presenter = browser.selectedDialogPresenter {
                MobileWebDialogHost(
                    presenter: presenter,
                    onPresentationRequested: expandHarborDeck
                )
            }
        }
        .onChange(of: chromeResetContext) { previous, current in
            if current.requiresExpansion(comparedTo: previous) { expandHarborDeck() }
        }
    }

    @ViewBuilder
    private var adaptiveBrowserLayout: some View {
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
        .accessibilityHidden(privatePrivacyCoverPresented)
        .animation(
            effectiveReduceMotion ? nil : .easeInOut(
                duration: MobileBrowserChromeTheme.motionDuration
            ),
            value: browser.selectedTab?.websiteTintARGB
        )
    }

    private var browserSurface: some View {
        ZStack(alignment: .bottom) {
            ZStack {
                if browser.selectedPageIsRetrying {
                    MobilePageRetryingView()
                } else if let failure = browser.selectedPageFailure {
                    MobilePageFailureView(
                        failure: failure,
                        onRetry: browser.retrySelectedPage
                    )
                } else if browser.selectedPage?.url == nil {
                    focusVoyage
                } else if let page = browser.selectedPage,
                          let scrollCoordinator = browser.selectedLinkInteractionCoordinator {
                    MobileWebPageView(
                        page: page,
                        scrollCoordinator: scrollCoordinator,
                        findNavigatorPresented: $findNavigatorPresented,
                        chromeCollapsed: $harborDeckCollapsed,
                        chromeResetGeneration: harborDeckResetGeneration,
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
                    .id(browser.selectedTabID)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(uiColor: .systemBackground))
            .overlay(alignment: .top) {
                if browser.selectedPageFailure == nil,
                   let page = browser.selectedPage, page.isLoading {
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
            .padding(.bottom, MobileBrowserChromeTheme.compactHarborDeckHeight)
            harborDeck
        }
        .background(Color(uiColor: .systemBackground))
        .tint(chromeTintColor)
        .animation(
            effectiveReduceMotion ? nil : .easeInOut(
                duration: MobileBrowserChromeTheme.motionDuration
            ),
            value: browser.selectedTab?.websiteTintARGB
        )
    }
    private var focusVoyage: some View {
        MobileFocusVoyageView(
            mode: selectedMode,
            workspaceName: selectedWorkspace?.name,
            workspaceSystemImage: selectedWorkspaceSystemImage,
            content: MobileFocusVoyageContent.make(
                mode: selectedMode,
                tabs: browser.normalTabs,
                snapshot: companionModel.snapshot,
                workspaceID: selectedWorkspace?.id
            ),
            accentTint: chromeTintColor,
            onSearch: presentAddress,
            onOpen: openFocusVoyageItem
        )
        .environment(\.colorScheme, isPrivateBrowsing ? .dark : colorScheme)
    }
    private var harborDeck: some View {
        MobileHarborDeckView(
            mode: selectedMode,
            isCollapsed: harborDeckCollapsed,
            workspaceName: selectedWorkspace?.name,
            workspaceSystemImage: selectedWorkspaceSystemImage,
            accentTint: chromeTintColor,
            addressLabel: addressLabel,
            addressAccessibilityValue: addressAccessibilityValue,
            securitySystemImage: securitySymbol,
            visibleTabCount: visibleTabCount,
            canGoBack: browser.selectedPage?.backForwardList.backList.isEmpty == false,
            canGoForward: browser.selectedPage?.backForwardList.forwardList.isEmpty == false,
            isLoading: browser.selectedPage?.isLoading == true,
            canSwitchWorkspace: selectedMode == .normal &&
                companionModel.snapshot.visibleWorkspaces.count > 1,
            onGoBack: browser.goBack,
            onGoForward: browser.goForward,
            onPresentAddress: presentAddress,
            onReloadOrStop: browser.reloadOrStop,
            onPresentTabs: presentTabs,
            onPresentMore: {
                expandHarborDeck()
                browserActionsPresented = true
            },
            onSwitchWorkspace: switchWorkspace
        )
        .accessibilitySortPriority(10)
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
            previewURL: $downloadPreviewURL,
            mode: browser.selectedTab?.mode ?? .normal
        )
    }
    private var browserActionsSheet: some View {
        MobileBrowserActionsSheet(
            companionModel: companionModel,
            browser: browser,
            isPresented: $browserActionsPresented,
            isRegularWidth: horizontalSizeClass == .regular,
            visibleDownloadCount: visibleDownloadCount,
            onFindOnPage: { presentAfterBrowserActions { findNavigatorPresented = true } },
            onPresentLibrary: { presentAfterBrowserActions { libraryPresented = true } },
            onPresentHistory: { presentAfterBrowserActions { historyPresented = true } },
            onPresentDownloads: { presentAfterBrowserActions { downloadsPresented = true } },
            onPresentSettings: { presentAfterBrowserActions { settingsPresented = true } },
            onToggleSidebar: toggleSidebar,
            onSwitchWorkspace: switchWorkspace,
            onSaveToWorkspace: saveSelectedPage,
            onCloseSelectedTab: {
                guard let selectedTabID = browser.selectedTabID else { return }
                closeTab(selectedTabID)
            },
            onClearPrivateTabs: {
                presentAfterBrowserActions { clearPrivateTabsRequested = true }
            },
            onClearWebsiteData: {
                presentAfterBrowserActions { clearWebsiteDataRequested = true }
            }
        )
    }
    private var librarySidebar: some View {
        MobileBrowserSidebar(
            model: companionModel,
            browser: browser,
            accentTint: chromeTintColor,
            onPresentCommand: presentAddress,
            onSelectWorkspace: selectWorkspace,
            onSelectTab: browser.select,
            onOpenPage: openSidebarPage,
            onCreateTab: createSidebarTab
        )
    }
    private var librarySheet: some View {
        CompanionRootView(
            model: companionModel,
            syncEnabled: $syncEnabled,
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
            if let host = selectedOriginHost {
                return CompanionL10n.format(
                    "browser.private.address",
                    fallback: "Private · %@",
                    host
                )
            }
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        if let url = selectedAddressURL {
            return selectedOriginHost ?? url.absoluteString
        }
        return CompanionL10n.string("browser.search_or_address", fallback: "Search or address")
    }
    private var addressAccessibilityValue: String {
        if browser.selectedTab?.mode == .privateBrowsing {
            if let host = selectedOriginHost {
                return CompanionL10n.format(
                    "browser.private.address.value",
                    fallback: "Private browsing, %@",
                    host
                )
            }
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        return selectedAddressURL?.absoluteString
            ?? CompanionL10n.string("browser.search_or_address", fallback: "Search or address")
    }
    private var selectedOriginHost: String? {
        guard let url = selectedAddressURL,
              let host = url.host(), !host.isEmpty else { return nil }
        guard let port = url.port else { return host }
        return "\(host):\(port)"
    }
    private var selectedAddressURL: URL? {
        if browser.selectedPageFailure != nil,
           let value = browser.selectedTab?.url {
            return URL(string: value)
        }
        return browser.selectedPage?.url ?? browser.selectedTab?.url.flatMap(URL.init(string:))
    }
    private var privatePrivacyCoverPresented: Bool {
        scenePhase != .active && isPrivateContentVisible
    }
    private var isPrivateContentVisible: Bool {
        browser.selectedTab?.mode == .privateBrowsing ||
            (tabsPresented && tabSwitcherMode == .privateBrowsing)
    }
    private var visibleTabCount: Int {
        browser.selectedTab?.mode == .privateBrowsing
            ? browser.privateTabs.count
            : browser.normalTabs.count
    }
    private var visibleDownloadCount: Int {
        let isPrivate = browser.selectedTab?.mode == .privateBrowsing
        return downloads.downloads.lazy.filter { $0.isPrivate == isPrivate }.count
    }
    private var privatePrivacyCover: some View {
        MobilePrivatePrivacyCoverView(accentTint: chromeTintColor)
    }
    private var securitySymbol: String {
        selectedAddressURL?.scheme?.lowercased() == "https" ? "lock.fill" : "globe"
    }
    private var chromeTintColor: Color {
        MobileBrowserChromeTheme.chromeTint(
            websiteTintARGB: browser.selectedTab?.websiteTintARGB,
            mode: selectedMode,
            colorScheme: colorScheme
        )
    }
    private var isPrivateBrowsing: Bool {
        selectedMode == .privateBrowsing
    }
    private var selectedMode: MobileBrowsingMode {
        browser.selectedTab?.mode ?? .normal
    }
    private var commandTabs: [MobileTabRecord] {
        selectedMode == .privateBrowsing ? browser.privateTabs : browser.normalTabs
    }
    private var browserCommandActions: MobileBrowserCommandActions {
        .init(
            tabCount: commandTabs.count,
            canReopenClosedTab: browser.recentlyClosedTab != nil,
            canSwitchWorkspace: selectedMode == .normal &&
                companionModel.snapshot.visibleWorkspaces.count > 1,
            canToggleSidebar: horizontalSizeClass == .regular,
            newTab: { createSidebarTab(nil, .normal) },
            newPrivateTab: { createSidebarTab(nil, .privateBrowsing) },
            reopenClosedTab: { browser.undoClose(); reconcileSidebarTabs() },
            closeSelectedTab: {
                if let id = browser.selectedTabID { closeTab(id) }
            },
            presentAddress: presentAddress,
            presentTabs: presentTabs,
            toggleSidebar: toggleSidebar,
            switchWorkspace: switchWorkspace,
            switchTab: browser.switchSelectedTab,
            selectNumberedTab: selectNumberedTab
        )
    }
    private var chromeResetContext: MobileChromeResetContext {
        MobileChromeResetContext(
            selectedTabID: browser.selectedTabID, pageIsLoading: browser.selectedPage?.isLoading == true,
            hasPageFailure: browser.selectedPageFailure != nil, browserErrorMessage: browser.lastError,
            permissionRequestID: permissions.pendingRequest?.id,
            externalRequestID: browser.pendingExternalOpen?.id, pendingLinkID: browser.pendingLink?.id,
            findPresented: findNavigatorPresented, addressPresented: addressPresented,
            regularWidth: horizontalSizeClass == .regular
        )
    }
    private var selectedWorkspace: Workspace? {
        guard selectedMode == .normal,
              let workspaceID = browser.selectedTab?.workspaceID else { return nil }
        return companionModel.snapshot.visibleWorkspaces.first { $0.id == workspaceID }
    }
    private var selectedWorkspaceSystemImage: String {
        guard let selectedWorkspace else {
            return MobileWorkspaceIconPolicy.fallbackSystemName
        }
        return MobileWorkspaceIconPolicy.systemName(for: selectedWorkspace.icon)
    }
    private func presentAddress() {
        expandHarborDeck()
        addressText = selectedAddressURL?.absoluteString ?? ""
        selectAllAddressText()
        addressPresented = true
    }
    private func presentTabs() {
        expandHarborDeck()
        tabSwitcherMode = browser.selectedTab?.mode ?? .normal
        tabsPresented = true
    }
    private func presentAfterBrowserActions(_ action: @escaping @MainActor () -> Void) {
        browserActionsPresented = false
        Task { @MainActor in
            await Task.yield()
            action()
        }
    }
    private func expandHarborDeck() {
        harborDeckResetGeneration &+= 1
        withAnimation(MobileBrowserChromeTheme.chromeAnimation(
            toCollapsed: false,
            reduceMotion: effectiveReduceMotion
        )) {
            harborDeckCollapsed = false
        }
    }
    private var effectiveReduceMotion: Bool {
        reduceMotion || performanceReduceMotionOverride == true
    }
    private var resolvedPerformanceReduceMotionOverride: Bool? {
        performanceReduceMotionOverride.map { reduceMotion || $0 }
    }
    private var performanceReduceMotionOverride: Bool? {
        if case let .valid(request) = MobilePerformanceLaunchRequest.validate(arguments: ProcessInfo.processInfo.arguments) {
            return request.reduceMotion
        }
        return nil
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
    private func selectNumberedTab(_ number: Int) {
        guard let index = MobileBrowserCommandTabPolicy.targetIndex(
            number: number,
            tabCount: commandTabs.count
        ) else { return }
        browser.select(commandTabs[index].id)
        expandHarborDeck()
    }
    private func selectWorkspace(_ workspaceID: WorkspaceID) {
        if let tab = browser.normalTabs
            .filter({ $0.workspaceID == workspaceID })
            .max(by: { $0.lastActiveAt < $1.lastActiveAt }) {
            browser.select(tab.id)
        } else {
            _ = browser.createTab(workspaceID: workspaceID)
        }
        reconcileSidebarTabs()
    }
    private func openSidebarPage(_ url: URL, _ workspaceID: WorkspaceID?) {
        guard (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else { return }
        _ = browser.createTab(url: url, workspaceID: workspaceID)
        reconcileSidebarTabs()
    }
    private func openFocusVoyageItem(_ item: MobileFocusVoyageItem) {
        if let tabID = item.existingTabID,
           browser.normalTabs.contains(where: { $0.id == tabID }) {
            browser.select(tabID)
            return
        }
        guard (try? MobileBrowserInputRouter.validateWebURL(item.url)) != nil else { return }
        if selectedMode == .normal, browser.selectedPage?.url == nil {
            if let workspaceID = item.workspaceID {
                browser.moveSelectedTab(to: workspaceID)
            }
            browser.navigate(item.url.absoluteString)
        } else {
            _ = browser.createTab(url: item.url, workspaceID: item.workspaceID)
        }
        reconcileSidebarTabs()
    }
    private func createSidebarTab(
        _ workspaceID: WorkspaceID?,
        _ mode: MobileBrowsingMode
    ) {
        _ = browser.createTab(workspaceID: workspaceID, mode: mode)
        reconcileSidebarTabs()
    }
    private func reconcileSidebarTabs() {
        guard !isPerformanceRuntime else { return }
        Task {
            await companionModel.reconcilePublishedMobileTabs(browser.normalTabs)
        }
    }
    private func flushSessionDuringBackgroundTransition() {
        let backgroundTask = MobileBackgroundTaskLease(
            name: "AhoiBrowser browser-session flush"
        )
        backgroundTask.begin()
        Task { @MainActor in
            defer { backgroundTask.end() }
            async let sessionFlush: Void = browser.flushSession()
            async let downloadFlush: Void = downloads.flushRecoveryState()
            _ = await (sessionFlush, downloadFlush)
        }
    }
    private var isPerformanceRuntime: Bool {
        if case .valid = MobilePerformanceLaunchRequest.validate(
            arguments: ProcessInfo.processInfo.arguments
        ) {
            return true
        }
        return false
    }
    private func toggleSidebar() {
        columnVisibility = columnVisibility == .detailOnly ? .all : .detailOnly
    }
    private func selectAllAddressText() {
        addressSelection = TextSelection(range: addressText.startIndex..<addressText.endIndex)
    }
    private func saveSelectedPage(to workspace: Workspace) {
        guard let tab = browser.selectedTab,
              let url = MobileTabRecord.normalizedURLString(tab.url) else { return }
        let normalizedTitle = MobileTabRecord.normalizedTitle(tab.effectiveTitle)
        let title = normalizedTitle.isEmpty ? url : normalizedTitle
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
}
