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
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.colorScheme) private var colorScheme
    @State private var addressPresented = false
    @State private var tabsPresented = false
    @State private var libraryPresented = false
    @State private var historyPresented = false
    @State private var settingsPresented = false
    @State private var downloadsPresented = false
    @State private var browserActionsPresented = false
    @State private var findNavigatorPresented = false
    @State private var clearWebsiteDataRequested = false
    @State private var clearPrivateTabsRequested = false
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
        ZStack {
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
                reduceMotion ? nil : .easeInOut(duration: 0.34),
                value: browser.selectedTab?.websiteTintARGB
            )

            if privatePrivacyCoverPresented {
                privatePrivacyCover
                    .zIndex(10_000)
            }
        }
        .transaction { transaction in
            if privatePrivacyCoverPresented {
                transaction.animation = nil
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
            if phase != .active {
                browser.prepareForInactiveScene()
            }
            if phase == .background {
                browser.discardInactivePages(keeping: 2)
                flushSessionDuringBackgroundTransition()
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
                set: { _ in }
            ),
            presenting: permissions.pendingRequest
        ) { request in
            Button(CompanionL10n.string("action.deny", fallback: "Don't Allow"), role: .cancel) {
                permissions.deny(requestID: request.id)
            }
            Button(CompanionL10n.string("action.allow", fallback: "Allow")) {
                permissions.allow(requestID: request.id)
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
                get: { browser.pendingExternalOpen != nil },
                set: { _ in }
            ),
            presenting: browser.pendingExternalOpen
        ) { request in
            Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                browser.cancelPendingExternalOpen(requestID: request.id)
            }
            Button(CompanionL10n.string("browser.external.open", fallback: "Open App")) {
                if let url = browser.confirmPendingExternalOpen(requestID: request.id) {
                    Task { _ = await UIApplication.shared.open(url) }
                }
            }
        } message: { request in
            Text(CompanionL10n.format(
                "browser.external.message",
                fallback: "%@ wants to open %@.",
                request.origin,
                request.url.scheme ?? request.url.absoluteString
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
            if isPrivateBrowsing {
                Color(red: 0.055, green: 0.060, blue: 0.085)
                    .ignoresSafeArea()
            }
            LinearGradient(
                colors: [
                    chromeTintColor.opacity(isPrivateBrowsing ? 0.24 : 0.11),
                    .clear,
                    chromeTintColor.opacity(isPrivateBrowsing ? 0.10 : 0.045),
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            VStack(spacing: 18) {
                Image(systemName: browser.selectedTab?.mode == .privateBrowsing
                      ? "hand.raised.fill"
                      : "sailboat.fill")
                    .font(.system(size: 42, weight: .semibold))
                    .foregroundStyle(chromeTintColor)
                    .accessibilityHidden(true)
                Text(browser.selectedTab?.mode == .privateBrowsing
                     ? CompanionL10n.string("browser.private", fallback: "Private")
                     : "AhoiBrowser")
                    .font(.title.bold())
                    .accessibilityIdentifier(
                        browser.selectedTab?.mode == .privateBrowsing
                            ? "browser.private-indicator"
                            : "browser.brand"
                    )
                    .foregroundStyle(isPrivateBrowsing ? Color.white : Color.primary)
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
                .foregroundStyle(isPrivateBrowsing ? Color.white : Color.primary)
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
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .disabled(browser.selectedPage?.backForwardList.backList.isEmpty != false)
            .keyboardShortcut("[", modifiers: .command)
            .accessibilityIdentifier("browser.back")
            .accessibilityLabel(CompanionL10n.string("browser.back", fallback: "Back"))

            Button(action: browser.goForward) {
                Image(systemName: "chevron.forward")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .disabled(browser.selectedPage?.backForwardList.forwardList.isEmpty != false)
            .keyboardShortcut("]", modifiers: .command)
            .accessibilityIdentifier("browser.forward")
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
                .frame(minHeight: 44)
                .padding(.horizontal, 12)
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
                browser.selectedTab?.mode == .privateBrowsing
                    ? "browser.private.address.accessibility"
                    : "browser.address.accessibility",
                fallback: browser.selectedTab?.mode == .privateBrowsing
                    ? "Private address and search"
                    : "Address and search"
            ))
            .accessibilityValue(Text(addressAccessibilityValue))

            Button(action: browser.reloadOrStop) {
                Image(systemName: browser.selectedPage?.isLoading == true ? "xmark" : "arrow.clockwise")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .keyboardShortcut("r", modifiers: .command)
            .accessibilityIdentifier("browser.reload-stop")
            .accessibilityLabel(CompanionL10n.string("browser.reload", fallback: "Reload"))

            Button(action: presentTabs) {
                ZStack {
                    RoundedRectangle(cornerRadius: 5)
                        .stroke(lineWidth: 1.5)
                        .frame(width: 24, height: 24)
                    Text("\(visibleTabCount)")
                        .font(.caption2.monospacedDigit().weight(.bold))
                }
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
            }
            .accessibilityIdentifier("browser.tabs")
            .accessibilityLabel(CompanionL10n.format(
                "browser.tabs.count",
                fallback: "%d tabs",
                visibleTabCount
            ))

            Button {
                browserActionsPresented = true
            } label: {
                Image(systemName: "ellipsis.circle")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .accessibilityIdentifier("browser.more")
            .accessibilityLabel(CompanionL10n.string(
                "browser.more.accessibility",
                fallback: "More browser actions"
            ))
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
        .background {
            if isPrivateBrowsing {
                Color(red: 0.055, green: 0.060, blue: 0.085)
            } else {
                Color(uiColor: .systemBackground)
            }
        }
        .environment(\.colorScheme, isPrivateBrowsing ? .dark : colorScheme)
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
            onSelectWorkspace: selectWorkspace,
            onSelectTab: browser.select,
            onOpenPage: openSidebarPage,
            onCreateTab: createSidebarTab
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
        browser.selectedPage?.url ?? browser.selectedTab?.url.flatMap(URL.init(string:))
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
        ZStack {
            Color(red: 0.055, green: 0.060, blue: 0.085)
                .ignoresSafeArea()
            VStack(spacing: 12) {
                Image(systemName: "hand.raised.fill")
                    .font(.system(size: 34, weight: .semibold))
                    .foregroundStyle(chromeTintColor)
                Text(CompanionL10n.string(
                    "browser.private.cover.title",
                    fallback: "Private browsing protected"
                ))
                .font(.headline)
                .foregroundStyle(.white)
                Text(CompanionL10n.string(
                    "browser.private.cover.message",
                    fallback: "Return to AhoiBrowser to view this private tab."
                ))
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.72))
                .multilineTextAlignment(.center)
            }
            .padding(28)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("browser.private-privacy-cover")
        .background {
            MobilePrivateSceneShield(
                title: CompanionL10n.string(
                    "browser.private.cover.title",
                    fallback: "Private browsing protected"
                ),
                message: CompanionL10n.string(
                    "browser.private.cover.message",
                    fallback: "Return to AhoiBrowser to view this private tab."
                )
            )
        }
    }

    private var securitySymbol: String {
        selectedAddressURL?.scheme?.lowercased() == "https" ? "lock.fill" : "globe"
    }

    private var chromeTintColor: Color {
        if isPrivateBrowsing {
            return Color(red: 0.53, green: 0.48, blue: 0.88)
        }
        guard let argb = browser.selectedTab?.websiteTintARGB else {
            return Color(red: 0.10, green: 0.43, blue: 0.84)
        }
        let channels = contrastAdjustedTint(
            red: Double((argb >> 16) & 0xFF) / 255,
            green: Double((argb >> 8) & 0xFF) / 255,
            blue: Double(argb & 0xFF) / 255
        )
        return Color(red: channels.red, green: channels.green, blue: channels.blue)
    }

    private var isPrivateBrowsing: Bool {
        browser.selectedTab?.mode == .privateBrowsing
    }

    /// Website colors remain recognizable, but functional controls never use
    /// a tint below the WCAG 3:1 non-text contrast floor against the active
    /// system background.
    private func contrastAdjustedTint(
        red: Double,
        green: Double,
        blue: Double
    ) -> (red: Double, green: Double, blue: Double) {
        let backgroundLuminance = colorScheme == .dark ? 0.0 : 1.0
        var candidate = (red, green, blue)
        for _ in 0..<32 {
            let luminance = relativeLuminance(
                red: candidate.0,
                green: candidate.1,
                blue: candidate.2
            )
            let contrast = (max(luminance, backgroundLuminance) + 0.05) /
                (min(luminance, backgroundLuminance) + 0.05)
            if contrast >= 3 { break }
            if colorScheme == .dark {
                candidate = (
                    candidate.0 + (1 - candidate.0) * 0.08,
                    candidate.1 + (1 - candidate.1) * 0.08,
                    candidate.2 + (1 - candidate.2) * 0.08
                )
            } else {
                candidate = (
                    candidate.0 * 0.90,
                    candidate.1 * 0.90,
                    candidate.2 * 0.90
                )
            }
        }
        return candidate
    }

    private func relativeLuminance(red: Double, green: Double, blue: Double) -> Double {
        func linear(_ channel: Double) -> Double {
            channel <= 0.04045
                ? channel / 12.92
                : pow((channel + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue)
    }

    private func presentAddress() {
        addressText = selectedAddressURL?.absoluteString ?? ""
        selectAllAddressText()
        addressPresented = true
    }

    private func presentTabs() {
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

    private func createSidebarTab(_ workspaceID: WorkspaceID?) {
        _ = browser.createTab(workspaceID: workspaceID)
        reconcileSidebarTabs()
    }

    private func reconcileSidebarTabs() {
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
            await browser.flushSession()
        }
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
        let normalizedTitle = MobileTabRecord.normalizedTitle(tab.title)
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

@MainActor
private final class MobileBackgroundTaskLease {
    private let name: String
    private var identifier: UIBackgroundTaskIdentifier = .invalid

    init(name: String) {
        self.name = name
    }

    func begin() {
        guard identifier == .invalid else { return }
        identifier = UIApplication.shared.beginBackgroundTask(withName: name) { [weak self] in
            Task { @MainActor [weak self] in
                self?.end()
            }
        }
    }

    func end() {
        guard identifier != .invalid else { return }
        let activeIdentifier = identifier
        identifier = .invalid
        UIApplication.shared.endBackgroundTask(activeIdentifier)
    }
}

/// SwiftUI presentations live above their presenting view, so a root overlay
/// alone does not protect an already-open sheet or alert in the app-switcher
/// snapshot. This marker installs a matching opaque shield at the owning
/// window level and removes it with the conditional SwiftUI cover.
@MainActor
private struct MobilePrivateSceneShield: UIViewRepresentable {
    let title: String
    let message: String

    func makeCoordinator() -> Coordinator {
        Coordinator(title: title, message: message)
    }

    func makeUIView(context: Context) -> MobilePrivateSceneMarkerView {
        let marker = MobilePrivateSceneMarkerView()
        marker.backgroundColor = .clear
        marker.isUserInteractionEnabled = false
        marker.onWindowChange = { [weak coordinator = context.coordinator] window in
            coordinator?.install(in: window)
        }
        return marker
    }

    func updateUIView(_ uiView: MobilePrivateSceneMarkerView, context: Context) {
        context.coordinator.update(title: title, message: message)
        context.coordinator.install(in: uiView.window)
    }

    static func dismantleUIView(
        _ uiView: MobilePrivateSceneMarkerView,
        coordinator: Coordinator
    ) {
        uiView.onWindowChange = nil
        coordinator.remove()
    }

    @MainActor
    final class Coordinator {
        private let shield = UIView()
        private let titleLabel = UILabel()
        private let messageLabel = UILabel()

        init(title: String, message: String) {
            shield.backgroundColor = .systemBackground
            shield.isOpaque = true
            shield.isUserInteractionEnabled = true
            shield.isAccessibilityElement = true
            shield.accessibilityViewIsModal = true
            shield.accessibilityIdentifier = "browser.private-window-shield"
            shield.layer.zPosition = 10_000

            let icon = UIImageView(image: UIImage(
                systemName: "hand.raised.fill",
                withConfiguration: UIImage.SymbolConfiguration(
                    pointSize: 34,
                    weight: .semibold
                )
            ))
            icon.tintColor = .systemPurple
            icon.contentMode = .scaleAspectFit
            icon.isAccessibilityElement = false

            titleLabel.font = .preferredFont(forTextStyle: .headline)
            titleLabel.textAlignment = .center
            titleLabel.adjustsFontForContentSizeCategory = true

            messageLabel.font = .preferredFont(forTextStyle: .subheadline)
            messageLabel.textColor = .secondaryLabel
            messageLabel.textAlignment = .center
            messageLabel.numberOfLines = 0
            messageLabel.adjustsFontForContentSizeCategory = true

            let stack = UIStackView(arrangedSubviews: [icon, titleLabel, messageLabel])
            stack.axis = .vertical
            stack.alignment = .center
            stack.spacing = 12
            stack.translatesAutoresizingMaskIntoConstraints = false
            shield.addSubview(stack)
            NSLayoutConstraint.activate([
                icon.widthAnchor.constraint(equalToConstant: 48),
                icon.heightAnchor.constraint(equalToConstant: 48),
                stack.centerXAnchor.constraint(equalTo: shield.centerXAnchor),
                stack.centerYAnchor.constraint(equalTo: shield.centerYAnchor),
                stack.leadingAnchor.constraint(
                    greaterThanOrEqualTo: shield.leadingAnchor,
                    constant: 28
                ),
                stack.trailingAnchor.constraint(
                    lessThanOrEqualTo: shield.trailingAnchor,
                    constant: -28
                )
            ])
            update(title: title, message: message)
        }

        func update(title: String, message: String) {
            titleLabel.text = title
            messageLabel.text = message
            shield.accessibilityLabel = "\(title). \(message)"
        }

        func install(in window: UIWindow?) {
            guard let window else {
                remove()
                return
            }
            window.endEditing(true)
            if shield.superview !== window {
                shield.removeFromSuperview()
                shield.frame = window.bounds
                shield.autoresizingMask = [.flexibleWidth, .flexibleHeight]
                window.addSubview(shield)
            } else {
                shield.frame = window.bounds
                window.bringSubviewToFront(shield)
            }
        }

        func remove() {
            shield.removeFromSuperview()
        }
    }
}

@MainActor
private final class MobilePrivateSceneMarkerView: UIView {
    var onWindowChange: (@MainActor (UIWindow?) -> Void)?

    override func didMoveToWindow() {
        super.didMoveToWindow()
        onWindowChange?(window)
    }
}
