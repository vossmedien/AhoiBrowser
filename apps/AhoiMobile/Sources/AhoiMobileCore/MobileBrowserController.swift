import Foundation
import SwiftUI
import WebKit
import AhoiCloudKitSpike

@MainActor
public final class MobileBrowserController: ObservableObject {
    @Published public private(set) var tabs: [MobileTabRecord] = []
    @Published public var selectedTabID: UUID?
    @Published public private(set) var lastError: String?
    @Published public private(set) var recentlyClosedTab: MobileTabRecord?
    @Published public private(set) var pendingExternalURL: URL?
    @Published public private(set) var pageFailures: [UUID: MobilePageFailureKind] = [:]

    public let permissionCoordinator: MobilePermissionCoordinator
    public let downloadCoordinator: MobileDownloadCoordinator

    private let store: any MobileBrowserSessionStoring
    private var pages: [UUID: WebPage] = [:]
    private var websiteDataStores: [UUID: WKWebsiteDataStore] = [:]
    private var lastRecordedHistoryURL: [UUID: String] = [:]
    private var desktopSiteTabIDs: Set<UUID> = []
    private var externalOpenDeduplicator = MobileExternalOpenDeduplicator()
    private var pendingStartupURL: URL?
    private var navigationObservationTasks: [UUID: Task<Void, Never>] = [:]
    private var expectedDownloadCancellationTabIDs: Set<UUID> = []
    private var didLoad = false

    public init(
        store: any MobileBrowserSessionStoring,
        permissionCoordinator: MobilePermissionCoordinator = MobilePermissionCoordinator(),
        downloadCoordinator: MobileDownloadCoordinator = MobileDownloadCoordinator(),
        startupError: String? = nil
    ) {
        self.store = store
        self.permissionCoordinator = permissionCoordinator
        self.downloadCoordinator = downloadCoordinator
        self.lastError = startupError
    }

    public convenience init() {
        self.init(store: InMemoryMobileBrowserSessionStore())
    }

    public var selectedTab: MobileTabRecord? {
        tabs.first { $0.id == selectedTabID }
    }

    public var selectedPage: WebPage? {
        guard let selectedTabID else { return nil }
        return page(for: selectedTabID)
    }

    public var selectedPageFailure: MobilePageFailureKind? {
        selectedTabID.flatMap { pageFailures[$0] }
    }

    public var normalTabs: [MobileTabRecord] {
        tabs.filter { $0.mode == .normal }
    }

    public var privateTabs: [MobileTabRecord] {
        tabs.filter { $0.mode == .privateBrowsing }
    }

    public func load() async {
        guard !didLoad else { return }
        do {
            let snapshot = try await store.load()
            tabs = snapshot.tabs
            selectedTabID = snapshot.selectedTabID
            didLoad = true
            if tabs.isEmpty {
                _ = createTab()
            } else if let selectedTabID {
                _ = page(for: selectedTabID)
            }
            lastError = nil
            drainPendingStartupURL()
        } catch {
            tabs = []
            selectedTabID = nil
            didLoad = true
            lastError = error.localizedDescription
            _ = createTab()
            drainPendingStartupURL()
        }
    }

    @discardableResult
    public func createTab(
        url: URL? = nil,
        workspaceID: WorkspaceID? = nil,
        mode: MobileBrowsingMode = .normal,
        select: Bool = true
    ) -> UUID {
        let record = MobileTabRecord(
            workspaceID: workspaceID,
            url: url?.absoluteString,
            mode: mode
        )
        tabs.append(record)
        if select { selectedTabID = record.id }
        if let url {
            let page = makePage(tabID: record.id, mode: mode)
            pages[record.id] = page
            observeNavigations(of: page, tabID: record.id)
            page.load(url)
        }
        discardInactivePages(keeping: 5)
        persistSoon()
        return record.id
    }

    public func select(_ id: UUID) {
        guard tabs.contains(where: { $0.id == id }) else { return }
        selectedTabID = id
        if let index = tabs.firstIndex(where: { $0.id == id }) {
            tabs[index].lastActiveAt = Date()
        }
        _ = page(for: id)
        discardInactivePages(keeping: 5)
        persistSoon()
    }

    public func navigate(
        _ input: String,
        searchTemplate: String = MobileBrowserInputRouter.defaultSearchTemplate
    ) {
        do {
            let url = try MobileBrowserInputRouter.resolve(
                input,
                searchTemplate: searchTemplate
            )
            guard let selectedTabID,
                  let page = page(for: selectedTabID, createIfBlank: true) else { return }
            observeNavigations(of: page, tabID: selectedTabID)
            page.load(url)
            updateSelectedMetadata(url: url, title: nil)
            lastError = nil
        } catch {
            lastError = CompanionL10n.string(
                "browser.error.invalid_address",
                fallback: "Enter a valid address or search term."
            )
        }
    }

    public func goBack() {
        guard let selectedTabID, let page = selectedPage,
              let item = page.backForwardList.backList.last else { return }
        observeNavigations(of: page, tabID: selectedTabID)
        page.load(item)
    }

    public func goForward() {
        guard let selectedTabID, let page = selectedPage,
              let item = page.backForwardList.forwardList.first else { return }
        observeNavigations(of: page, tabID: selectedTabID)
        page.load(item)
    }

    public func reloadOrStop() {
        guard let selectedTabID, let page = selectedPage else { return }
        observeNavigations(of: page, tabID: selectedTabID)
        if page.isLoading {
            page.stopLoading()
        } else {
            page.reload()
        }
    }

    public func close(_ id: UUID) {
        guard let index = tabs.firstIndex(where: { $0.id == id }) else { return }
        let removed = tabs.remove(at: index)
        pages.removeValue(forKey: id)
        websiteDataStores.removeValue(forKey: id)
        navigationObservationTasks.removeValue(forKey: id)?.cancel()
        pageFailures.removeValue(forKey: id)
        expectedDownloadCancellationTabIDs.remove(id)
        desktopSiteTabIDs.remove(id)
        recentlyClosedTab = removed
        if selectedTabID == id {
            if tabs.isEmpty {
                selectedTabID = nil
                _ = createTab(mode: removed.mode)
            } else {
                selectedTabID = tabs[min(index, tabs.count - 1)].id
            }
        }
        persistSoon()
    }

    public func undoClose() {
        guard var record = recentlyClosedTab else { return }
        record.lastActiveAt = Date()
        tabs.append(record)
        selectedTabID = record.id
        recentlyClosedTab = nil
        if let value = record.url, let url = URL(string: value) {
            let restoredPage = makePage(tabID: record.id, mode: record.mode)
            pages[record.id] = restoredPage
            restoredPage.load(url)
        }
        persistSoon()
    }

    public func moveSelectedTab(to workspaceID: WorkspaceID?) {
        guard let selectedTabID,
              let index = tabs.firstIndex(where: { $0.id == selectedTabID }) else { return }
        tabs[index].workspaceID = workspaceID
        persistSoon()
    }

    public func setSelectedTabSaved(_ saved: Bool) {
        guard let selectedTabID,
              let index = tabs.firstIndex(where: { $0.id == selectedTabID }) else { return }
        tabs[index].isSaved = saved
        persistSoon()
    }

    @discardableResult
    public func duplicateSelectedTab() -> UUID? {
        guard let tab = selectedTab else { return nil }
        return createTab(
            url: tab.url.flatMap(URL.init(string:)),
            workspaceID: tab.workspaceID,
            mode: tab.mode
        )
    }

    public func renameTab(_ id: UUID, title: String) {
        guard let index = tabs.firstIndex(where: { $0.id == id }) else { return }
        let value = title.trimmingCharacters(in: .whitespacesAndNewlines)
        tabs[index].title = String(value.prefix(160))
        persistSoon()
    }

    public func closeSelectedTab() {
        guard let selectedTabID else { return }
        close(selectedTabID)
    }

    public func toggleDesktopSite() {
        guard let selectedTabID, let page = selectedPage else { return }
        if desktopSiteTabIDs.contains(selectedTabID) {
            desktopSiteTabIDs.remove(selectedTabID)
            page.customUserAgent = nil
        } else {
            desktopSiteTabIDs.insert(selectedTabID)
            page.customUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                + "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15"
        }
        observeNavigations(of: page, tabID: selectedTabID)
        page.reload()
    }

    public var selectedTabUsesDesktopSite: Bool {
        selectedTabID.map(desktopSiteTabIDs.contains) ?? false
    }

    public func adjustTextScale(by delta: Int) {
        guard let page = selectedPage else { return }
        Task {
            let current = (try? await page.callJavaScript(
                "parseInt(document.documentElement.style.webkitTextSizeAdjust) || 100"
            ) as? Int) ?? 100
            let next = min(200, max(50, current + delta))
            _ = try? await page.callJavaScript(
                "document.documentElement.style.webkitTextSizeAdjust = value + '%'",
                arguments: ["value": next]
            )
        }
    }

    public func searchOpenTabs(_ query: String) -> [MobileTabRecord] {
        let normalized = query.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.diacriticInsensitive, .caseInsensitive], locale: .current)
        guard !normalized.isEmpty else { return [] }
        return tabs.filter { tab in
            guard tab.mode == .normal else { return false }
            let haystack = "\(tab.title) \(tab.url ?? "")"
                .folding(options: [.diacriticInsensitive, .caseInsensitive], locale: .current)
            return haystack.contains(normalized)
        }.sorted { $0.lastActiveAt > $1.lastActiveAt }
    }

    public func synchronizeSelectedPageMetadata() -> MobileNavigationObservation? {
        guard let selectedTabID, let page = selectedPage else { return nil }
        updateSelectedMetadata(url: page.url, title: page.title)
        guard let url = page.url,
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil,
              lastRecordedHistoryURL[selectedTabID] != url.absoluteString,
              selectedTab?.mode == .normal else { return nil }
        lastRecordedHistoryURL[selectedTabID] = url.absoluteString
        return MobileNavigationObservation(
            tabID: selectedTabID,
            title: page.title,
            url: url,
            tab: selectedTab ?? MobileTabRecord(id: selectedTabID)
        )
    }

    public func refreshSelectedWebsiteTint() async {
        guard let selectedTabID, let page = selectedPage else { return }
        for delay in [Duration.zero, .milliseconds(180), .milliseconds(420)] {
            if delay != .zero { try? await Task.sleep(for: delay) }
            await sampleWebsiteTint(from: page, tabID: selectedTabID)
            if tabs.first(where: { $0.id == selectedTabID })?.websiteTintARGB != nil {
                return
            }
        }
    }

    public func handleExternalURL(_ url: URL) {
        do {
            let safeURL = try MobileBrowserInputRouter.validateWebURL(url)
            guard externalOpenDeduplicator.accepts(safeURL) else { return }
            guard didLoad else {
                pendingStartupURL = safeURL
                return
            }
            openValidatedExternalURL(safeURL)
            lastError = nil
        } catch {
            lastError = CompanionL10n.string(
                "browser.error.blocked_scheme",
                fallback: "AhoiBrowser only opens HTTP and HTTPS links here."
            )
        }
    }

    public func confirmPendingExternalURL() -> URL? {
        defer { pendingExternalURL = nil }
        return pendingExternalURL
    }

    public func cancelPendingExternalURL() {
        pendingExternalURL = nil
    }

    public func dismissError() {
        lastError = nil
    }

    public func clearPrivateTabs() {
        let privateIDs = Set(privateTabs.map(\.id))
        tabs.removeAll { privateIDs.contains($0.id) }
        for id in privateIDs { pages.removeValue(forKey: id) }
        for id in privateIDs { websiteDataStores.removeValue(forKey: id) }
        for id in privateIDs { navigationObservationTasks.removeValue(forKey: id)?.cancel() }
        for id in privateIDs { pageFailures.removeValue(forKey: id) }
        if let selectedTabID, privateIDs.contains(selectedTabID) {
            self.selectedTabID = normalTabs.last?.id
            if self.selectedTabID == nil { _ = createTab() }
        }
        persistSoon()
    }

    public func discardInactivePages(keeping maximumLivePages: Int) {
        let limit = max(1, maximumLivePages)
        let keepIDs = Set(tabs
            .sorted { lhs, rhs in
                if lhs.id == selectedTabID { return true }
                if rhs.id == selectedTabID { return false }
                return lhs.lastActiveAt > rhs.lastActiveAt
            }
            .prefix(limit)
            .map(\.id))
        for id in Array(pages.keys) where !keepIDs.contains(id) {
            pages.removeValue(forKey: id)
            websiteDataStores.removeValue(forKey: id)
            navigationObservationTasks.removeValue(forKey: id)?.cancel()
            pageFailures.removeValue(forKey: id)
            expectedDownloadCancellationTabIDs.remove(id)
        }
    }

    public func retrySelectedPage() {
        guard let selectedTabID, let page = selectedPage else { return }
        pageFailures.removeValue(forKey: selectedTabID)
        observeNavigations(of: page, tabID: selectedTabID)
        if page.url != nil {
            page.reload()
        } else if let value = selectedTab?.url, let url = URL(string: value) {
            page.load(url)
        }
    }

    public static func classifyNavigationFailure(_ error: Error) -> MobilePageFailureKind {
        if let navigationError = error as? WebPage.NavigationError {
            switch navigationError {
            case .failedProvisionalNavigation(let underlying):
                return classifyNavigationFailure(underlying)
            case .webContentProcessTerminated:
                return .webContentTerminated
            case .invalidURL:
                return .invalidURL
            case .pageClosed:
                return .failed
            @unknown default:
                return .failed
            }
        }

        let failure = error as NSError
        switch failure.code {
        case NSURLErrorNotConnectedToInternet, NSURLErrorNetworkConnectionLost:
            return .offline
        case NSURLErrorTimedOut:
            return .timedOut
        case NSURLErrorBadURL, NSURLErrorUnsupportedURL:
            return .invalidURL
        default:
            if let underlying = failure.userInfo[NSUnderlyingErrorKey] as? Error {
                return classifyNavigationFailure(underlying)
            }
            return .failed
        }
    }

    public func clearWebsiteData() async {
        let types = WKWebsiteDataStore.allWebsiteDataTypes()
        await withCheckedContinuation { continuation in
            WKWebsiteDataStore.default().removeData(
                ofTypes: types,
                modifiedSince: .distantPast
            ) {
                continuation.resume()
            }
        }
    }

#if DEBUG
    public func loadUITestFixture() {
        pages.removeAll()
        websiteDataStores.removeAll()
        tabs.removeAll()
        selectedTabID = nil
        recentlyClosedTab = nil
        let fixtureURL = URL(string: "https://fixture.ahoibrowser.test/start")!
        let tabID = createTab()
        guard let page = page(for: tabID, createIfBlank: true) else { return }
        page.load(
            simulatedRequest: URLRequest(url: fixtureURL),
            responseHTML: """
            <!doctype html><html lang="en"><head>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <meta name="theme-color" content="#f97316">
            <title>Ahoi Fixture</title>
            <style>
              :root { accent-color:#f97316; color-scheme:light dark }
              body { font:17px -apple-system,sans-serif; margin:28px; line-height:1.45 }
              h1 { color:#c2410c }
              button { background:#f97316; color:white; border:0; border-radius:10px; padding:12px }
            </style></head><body>
            <main><h1>Ahoi fixture page</h1>
            <p>This page is provided locally for deterministic browser UI tests.</p>
            <p id="find-target">Ahoi visible find target</p>
            <ul>
              <li><a href="https://example.com">Open HTTPS page</a></li>
              <li><a href="https://example.com/?ahoi-popup=1" target="_blank">Open target blank</a></li>
              <li><a href="https://httpbin.org/response-headers?Content-Disposition=attachment%3B%20filename%3Dahoi-fixture.txt&amp;Content-Type=text%2Fplain">Download fixture</a></li>
              <li><a href="mailto:browser-test@example.com">Open mail app</a></li>
            </ul>
            <p><input id="upload" type="file" style="font-size:20px;padding:12px;border:1px solid #777;border-radius:10px;width:340px"></p>
            <p><button id="camera" onclick="navigator.mediaDevices.getUserMedia({video:true}).catch(() => {})">Request camera</button></p>
            </main></body></html>
            """
        )
        updateSelectedMetadata(url: fixtureURL, title: "Ahoi Fixture")
    }

    public func loadUITestOfflineFailure() {
        pages.removeAll()
        websiteDataStores.removeAll()
        tabs.removeAll()
        selectedTabID = nil
        pageFailures.removeAll()
        _ = createTab()
        guard let selectedTabID else { return }
        pageFailures[selectedTabID] = .offline
    }
#endif

    private func page(for tabID: UUID, createIfBlank: Bool = false) -> WebPage? {
        if let page = pages[tabID] { return page }
        guard let record = tabs.first(where: { $0.id == tabID }) else { return nil }
        guard createIfBlank || record.url != nil else { return nil }
        let page = makePage(tabID: tabID, mode: record.mode)
        pages[tabID] = page
        observeNavigations(of: page, tabID: tabID)
        if let value = record.url, let url = URL(string: value) { page.load(url) }
        return page
    }

    private func observeNavigations(of page: WebPage, tabID: UUID) {
        navigationObservationTasks.removeValue(forKey: tabID)?.cancel()
        navigationObservationTasks[tabID] = Task { @MainActor [weak self, weak page] in
            guard let self, let page else { return }
            do {
                for try await event in page.navigations {
                    guard !Task.isCancelled else { return }
                    switch event {
                    case .startedProvisionalNavigation, .committed:
                        self.pageFailures.removeValue(forKey: tabID)
                    case .finished:
                        self.pageFailures.removeValue(forKey: tabID)
                        await self.sampleWebsiteTint(from: page, tabID: tabID)
                    case .receivedServerRedirect:
                        break
                    @unknown default:
                        break
                    }
                }
            } catch {
                guard !Task.isCancelled else { return }
                if self.expectedDownloadCancellationTabIDs.remove(tabID) != nil {
                    self.pageFailures.removeValue(forKey: tabID)
                    return
                }
                guard !Self.isNavigationCancellation(error) else { return }
                self.pageFailures[tabID] = Self.classifyNavigationFailure(error)
            }
        }
    }

    private func sampleWebsiteTint(from page: WebPage, tabID: UUID) async {
        guard let index = tabs.firstIndex(where: { $0.id == tabID }),
              tabs[index].mode == .normal else { return }
        let declaredValue: Any? = try? await page.callJavaScript(
            "return document.querySelector('meta[name=\"theme-color\"]')?.content || '';"
        )
        let declaredColor = (declaredValue as? String).flatMap(Self.argbColor(from:))
        let script = #"""
        return (() => {
          const parse = (value) => {
            if (!value || value === 'transparent' || value === 'auto') return null;
            const direct = value.trim().match(/^#([0-9a-f]{6})$/i);
            if (direct) {
              return [0, 2, 4].map(offset => parseInt(direct[1].slice(offset, offset + 2), 16));
            }
            const probe = document.createElement('span');
            probe.style.cssText = 'position:fixed;left:-10000px;visibility:hidden';
            probe.style.color = value;
            if (!probe.style.color) return null;
            document.documentElement.appendChild(probe);
            const rendered = getComputedStyle(probe).color;
            probe.remove();
            const match = rendered.match(/rgba?\(\s*([\d.]+)[, ]+\s*([\d.]+)[, ]+\s*([\d.]+)(?:\s*[,/]\s*([\d.]+))?/i);
            if (!match || (match[4] !== undefined && Number(match[4]) < 0.35)) return null;
            return [Number(match[1]), Number(match[2]), Number(match[3])];
          };
          const eligible = (rgb) => {
            const [r, g, b] = rgb.map(v => v / 255);
            const max = Math.max(r, g, b), min = Math.min(r, g, b);
            const lightness = (max + min) / 2;
            const saturation = max === min ? 0 : (max - min) / (1 - Math.abs(2 * lightness - 1));
            return lightness > 0.10 && lightness < 0.90 && saturation > 0.16;
          };
          const banner = document.querySelector('header, nav, [role="banner"]');
          const link = document.querySelector('a');
          const candidates = [
            document.querySelector('meta[name="theme-color"]')?.content,
            getComputedStyle(document.documentElement).accentColor,
            banner && getComputedStyle(banner).backgroundColor,
            getComputedStyle(document.body).backgroundColor,
            getComputedStyle(document.documentElement).backgroundColor,
            link && getComputedStyle(link).color
          ];
          for (const candidate of candidates) {
            const rgb = parse(candidate);
            if (!rgb || !eligible(rgb)) continue;
            return '#' + rgb.map(v => Math.round(v).toString(16).padStart(2, '0')).join('');
          }
          return null;
        })()
        """#
        let sampledValue: Any? = try? await page.callJavaScript(script)
        let sampled = sampledValue as? String
        guard let currentIndex = tabs.firstIndex(where: { $0.id == tabID }),
              tabs[currentIndex].mode == .normal else { return }
        let color = declaredColor ?? sampled.flatMap(Self.argbColor(from:))
        guard tabs[currentIndex].websiteTintARGB != color else { return }
        tabs[currentIndex].websiteTintARGB = color
        persistSoon()
    }

    private static func argbColor(from hexadecimal: String) -> UInt32? {
        let value = hexadecimal.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        guard value.count == 6, let rgb = UInt32(value, radix: 16) else { return nil }
        return 0xFF00_0000 | rgb
    }

    private static func isNavigationCancellation(_ error: Error) -> Bool {
        if let navigationError = error as? WebPage.NavigationError,
           case .failedProvisionalNavigation(let underlying) = navigationError {
            return isNavigationCancellation(underlying)
        }
        let failure = error as NSError
        if failure.domain == NSURLErrorDomain, failure.code == NSURLErrorCancelled {
            return true
        }
        if let underlying = failure.userInfo[NSUnderlyingErrorKey] as? Error {
            return isNavigationCancellation(underlying)
        }
        return false
    }

    private func makePage(tabID: UUID, mode: MobileBrowsingMode) -> WebPage {
        var configuration = WebPage.Configuration()
        let websiteDataStore: WKWebsiteDataStore = mode == .privateBrowsing
            ? .nonPersistent()
            : .default()
        websiteDataStores[tabID] = websiteDataStore
        configuration.websiteDataStore = websiteDataStore
        configuration.upgradeKnownHostsToHTTPS = true
        configuration.mediaPlaybackBehavior = .allowsInlinePlayback
        configuration.deviceSensorAuthorization = .init { [weak permissionCoordinator] permission, _, origin in
            guard let permissionCoordinator else { return .deny }
            return await permissionCoordinator.request(permission: permission, origin: origin)
        }

        let policy = MobileNavigationPolicyHandler()
        policy.onOpenNewTab = { [weak self] url in
            _ = self?.createTab(url: url)
        }
        policy.onExternalScheme = { [weak self] url in
            self?.pendingExternalURL = url
        }
        policy.onDownload = { [weak self] request in
            self?.expectedDownloadCancellationTabIDs.insert(tabID)
            self?.pageFailures.removeValue(forKey: tabID)
            self?.downloadCoordinator.start(
                request: request,
                websiteDataStore: websiteDataStore,
                isPrivate: mode == .privateBrowsing
            )
        }
        return WebPage(configuration: configuration, navigationDecider: policy)
    }

    private func updateSelectedMetadata(url: URL?, title: String?) {
        guard let selectedTabID,
              let index = tabs.firstIndex(where: { $0.id == selectedTabID }) else { return }
        if let url, (try? MobileBrowserInputRouter.validateWebURL(url)) != nil {
            tabs[index].url = url.absoluteString
        }
        if let title, !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            tabs[index].title = title
        }
        tabs[index].lastActiveAt = Date()
        persistSoon()
    }

    private func drainPendingStartupURL() {
        guard let pendingStartupURL else { return }
        self.pendingStartupURL = nil
        openValidatedExternalURL(pendingStartupURL)
    }

    private func openValidatedExternalURL(_ safeURL: URL) {
        if selectedTab?.url == nil, selectedTab?.mode == .normal {
            guard let selectedTabID,
                  let page = page(for: selectedTabID, createIfBlank: true) else { return }
            observeNavigations(of: page, tabID: selectedTabID)
            page.load(safeURL)
            updateSelectedMetadata(url: safeURL, title: nil)
        } else {
            _ = createTab(url: safeURL)
        }
    }

    private func persistSoon() {
        let persistentTabs = normalTabs
        let persistentSelection = persistentTabs.contains { $0.id == selectedTabID }
            ? selectedTabID
            : persistentTabs.last?.id
        let snapshot = MobileBrowserSessionSnapshot(
            tabs: persistentTabs,
            selectedTabID: persistentSelection
        )
        Task {
            do {
                try await store.save(snapshot)
            } catch {
                await MainActor.run { self.lastError = error.localizedDescription }
            }
        }
    }
}
