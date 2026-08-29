import Foundation
import ImageIO
import SwiftUI
import UniformTypeIdentifiers
import WebKit
import AhoiCloudKitSpike

public struct MobilePendingExternalOpen: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let url: URL
    public let origin: String
    public let sourceTabID: UUID

    public init(
        id: UUID = UUID(),
        url: URL,
        origin: String,
        sourceTabID: UUID
    ) {
        self.id = id
        self.url = url
        self.origin = origin
        self.sourceTabID = sourceTabID
    }
}

@MainActor
public final class MobileBrowserController: ObservableObject {
    @Published public private(set) var tabs: [MobileTabRecord] = []
    @Published public var selectedTabID: UUID? {
        didSet {
            guard oldValue != selectedTabID else { return }
            permissionCoordinator.cancelPending(unlessTabID: selectedTabID)
            if pendingExternalOpen?.sourceTabID != selectedTabID {
                pendingExternalOpen = nil
            }
            if let oldValue {
                dialogPresenters[oldValue]?.cancelPending()
                if pendingLink?.sourceTabID == oldValue {
                    pendingLink = nil
                }
            }
        }
    }
    @Published public private(set) var lastError: String?
    @Published public private(set) var recentlyClosedTab: MobileTabRecord?
    @Published public private(set) var pendingExternalOpen: MobilePendingExternalOpen?
    @Published public private(set) var pendingLink: MobilePendingLink?
    @Published public private(set) var pageFailures: [UUID: MobilePageFailureKind] = [:]

    public let permissionCoordinator: MobilePermissionCoordinator
    public let downloadCoordinator: MobileDownloadCoordinator

    private let store: any MobileBrowserSessionStoring
    private let saveCoordinator: MobileBrowserSessionSaveCoordinator
    private let storagePreparation: (@Sendable () async throws -> Void)?
    private var pages: [UUID: WebPage] = [:]
    private var dialogPresenters: [UUID: MobileWebDialogPresenter] = [:]
    private var linkInteractionCoordinators: [UUID: MobileLinkInteractionCoordinator] = [:]
    private var websiteDataStores: [UUID: WKWebsiteDataStore] = [:]
    private var privateWebsiteDataStore: WKWebsiteDataStore?
    private var lastRecordedHistoryURL: [UUID: String] = [:]
    private var desktopSiteTabIDs: Set<UUID> = []
    private var externalOpenDeduplicator = MobileExternalOpenDeduplicator()
    private var pendingStartupURL: URL?
    private var navigationObservationTasks: [UUID: Task<Void, Never>] = [:]
    private var expectedDownloadCancellationTabIDs: Set<UUID> = []
    private var faviconFetchInFlight: [UUID: String] = [:]
    private var faviconAttemptedDocumentURLs: [UUID: String] = [:]
    private var navigationDocumentGenerations: [UUID: UInt64] = [:]
    private var didLoad = false
    private var sessionRevision: UInt64 = 0
    private static let maximumFaviconBytes = MobileTabRecord.maximumFaviconDataBytes
    private static let maximumFaviconDimension = 1_024
    private static let maximumFaviconPixels = 1_048_576
    private static let persistedFaviconDimension = 64
    private static let faviconContentWorld = WKContentWorld.world(
        name: "AhoiBrowser.Favicon"
    )
    private static let allowedFaviconMIMETypes: Set<String> = [
        "image/avif",
        "image/jpeg",
        "image/png",
        "image/vnd.microsoft.icon",
        "image/webp",
        "image/x-icon",
    ]
#if DEBUG
    private var uiTestRetryResponses: [UUID: (request: URLRequest, html: String)] = [:]
#endif

    public init(
        store: any MobileBrowserSessionStoring,
        permissionCoordinator: MobilePermissionCoordinator = MobilePermissionCoordinator(),
        downloadCoordinator: MobileDownloadCoordinator = MobileDownloadCoordinator(),
        storagePreparation: (@Sendable () async throws -> Void)? = nil,
        startupError: String? = nil
    ) {
        self.store = store
        self.saveCoordinator = MobileBrowserSessionSaveCoordinator(store: store)
        self.storagePreparation = storagePreparation
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

    public var selectedDialogPresenter: MobileWebDialogPresenter? {
        guard let selectedTabID else { return nil }
        return dialogPresenters[selectedTabID]
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
            try await storagePreparation?()
            let snapshot = try await store.load()
            let restoredSnapshot = MobileBrowserSessionSnapshot(
                schemaVersion: snapshot.schemaVersion,
                tabs: snapshot.tabs,
                selectedTabID: snapshot.selectedTabID
            )
            tabs = restoredSnapshot.tabs
            selectedTabID = restoredSnapshot.selectedTabID
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

    public func reload() {
        guard let selectedTabID, let page = selectedPage else { return }
        observeNavigations(of: page, tabID: selectedTabID)
        page.reload()
    }

    public func close(_ id: UUID) {
        guard let index = tabs.firstIndex(where: { $0.id == id }) else { return }
        let removed = tabs.remove(at: index)
        permissionCoordinator.cancelPending(forTabID: id)
        pages.removeValue(forKey: id)
        dialogPresenters.removeValue(forKey: id)?.cancelPending()
        linkInteractionCoordinators.removeValue(forKey: id)?.invalidate()
        if pendingLink?.sourceTabID == id { pendingLink = nil }
        if pendingExternalOpen?.sourceTabID == id { pendingExternalOpen = nil }
        websiteDataStores.removeValue(forKey: id)
        navigationObservationTasks.removeValue(forKey: id)?.cancel()
        pageFailures.removeValue(forKey: id)
        expectedDownloadCancellationTabIDs.remove(id)
        faviconFetchInFlight.removeValue(forKey: id)
        faviconAttemptedDocumentURLs.removeValue(forKey: id)
        navigationDocumentGenerations.removeValue(forKey: id)
        desktopSiteTabIDs.remove(id)
#if DEBUG
        uiTestRetryResponses.removeValue(forKey: id)
#endif
        // Private browsing metadata must not survive tab closure in an undo
        // buffer or appear later in an app-switcher/tab-switcher snapshot.
        recentlyClosedTab = removed.mode == .normal ? removed : nil
        if removed.mode == .privateBrowsing && privateTabs.isEmpty {
            // Closing the final private tab ends that ephemeral session. A
            // replacement private tab receives a fresh non-persistent store.
            privateWebsiteDataStore = nil
        }
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
        guard var record = recentlyClosedTab, record.mode == .normal else {
            recentlyClosedTab = nil
            return
        }
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

    public func moveTab(_ id: UUID, to workspaceID: WorkspaceID?) {
        guard let index = tabs.firstIndex(where: { $0.id == id }) else { return }
        tabs[index].workspaceID = workspaceID
        persistSoon()
    }

    public func setSelectedTabSaved(_ saved: Bool) {
        guard let selectedTabID,
              let index = tabs.firstIndex(where: { $0.id == selectedTabID }) else { return }
        tabs[index].isSaved = saved
        persistSoon()
    }

    public func setTabSaved(_ id: UUID, _ saved: Bool) {
        guard let index = tabs.firstIndex(where: { $0.id == id }),
              tabs[index].mode == .normal else { return }
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

    public func reorderTabs(
        _ orderedIDs: [UUID],
        fromOffsets: IndexSet,
        toOffset: Int
    ) {
        guard !orderedIDs.isEmpty,
              orderedIDs.allSatisfy({ id in tabs.contains { $0.id == id } }) else {
            return
        }
        var reorderedIDs = orderedIDs
        reorderedIDs.move(fromOffsets: fromOffsets, toOffset: toOffset)
        var records: [UUID: MobileTabRecord] = [:]
        for tab in tabs { records[tab.id] = tab }
        let memberIDs = Set(orderedIDs)
        let slots = tabs.indices.filter { memberIDs.contains(tabs[$0].id) }
        guard slots.count == reorderedIDs.count else { return }
        for (slot, id) in zip(slots, reorderedIDs) {
            guard let record = records[id] else { return }
            tabs[slot] = record
        }
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
        guard let tab = selectedTab,
              let value = tab.url,
              let url = URL(string: value),
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil,
              lastRecordedHistoryURL[selectedTabID] != value,
              tab.mode == .normal else { return nil }
        lastRecordedHistoryURL[selectedTabID] = value
        return MobileNavigationObservation(
            tabID: selectedTabID,
            title: tab.title,
            url: url,
            tab: tab
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

    public func refreshSelectedFavicon() async {
        guard let tabID = selectedTabID,
              let page = selectedPage,
              !page.isLoading,
              let pageURL = page.url,
              (try? MobileBrowserInputRouter.validateWebURL(pageURL)) != nil else {
            return
        }
        let documentKey = pageURL.absoluteString
        guard faviconFetchInFlight[tabID] != documentKey,
              faviconAttemptedDocumentURLs[tabID] != documentKey else {
            return
        }
        faviconFetchInFlight[tabID] = documentKey
        defer {
            if faviconFetchInFlight[tabID] == documentKey {
                faviconFetchInFlight.removeValue(forKey: tabID)
                faviconAttemptedDocumentURLs[tabID] = documentKey
            }
        }
        let script = #"""
        return await (async () => {
          const declared = document.querySelector(
            'link[rel~="icon"], link[rel="shortcut icon"], link[rel="apple-touch-icon"]'
          )?.href;
          let candidate;
          try {
            candidate = new URL(declared || '/favicon.ico', document.baseURI);
          } catch (_) {
            return null;
          }
          if (candidate.protocol !== 'https:' && candidate.protocol !== 'http:') {
            return null;
          }

          let response;
          try {
            response = await fetch(candidate.href, {
              cache: 'force-cache',
              credentials: 'include',
              redirect: 'follow',
              headers: {
                Accept: 'image/avif,image/webp,image/png,image/jpeg,image/x-icon,image/vnd.microsoft.icon'
              }
            });
          } catch (_) {
            return null;
          }
          if (!response.ok || !response.body) return null;

          let finalURL;
          try { finalURL = new URL(response.url); }
          catch (_) { return null; }
          if (finalURL.protocol !== 'https:' && finalURL.protocol !== 'http:') {
            return null;
          }

          const mime = (response.headers.get('Content-Type') || '')
            .split(';', 1)[0]
            .trim()
            .toLowerCase();
          if (!allowedMIMETypes.includes(mime)) return null;
          const declaredLength = Number(response.headers.get('Content-Length'));
          if (Number.isFinite(declaredLength) && declaredLength > maximumBytes) {
            return null;
          }

          const reader = response.body.getReader();
          const chunks = [];
          let total = 0;
          try {
            while (true) {
              const { done, value } = await reader.read();
              if (done) break;
              if (!(value instanceof Uint8Array)) return null;
              total += value.byteLength;
              if (total > maximumBytes) {
                await reader.cancel();
                return null;
              }
              chunks.push(value);
            }
          } catch (_) {
            return null;
          }
          if (total <= 0) return null;

          const bytes = new Uint8Array(total);
          let cursor = 0;
          for (const chunk of chunks) {
            bytes.set(chunk, cursor);
            cursor += chunk.byteLength;
          }
          let binary = '';
          for (let offset = 0; offset < bytes.length; offset += 0x8000) {
            binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
          }
          const base64 = btoa(binary);
          const maximumEncodedLength = Math.ceil(maximumBytes / 3) * 4;
          if (base64.length > maximumEncodedLength) return null;
          return {
            base64,
            documentURL: location.href,
            mime,
          };
        })();
        """#
        let value: Any?
        do {
            value = try await page.callJavaScript(
                script,
                arguments: [
                    "allowedMIMETypes": Array(Self.allowedFaviconMIMETypes),
                    "maximumBytes": Self.maximumFaviconBytes,
                ],
                contentWorld: Self.faviconContentWorld
            )
        } catch {
            return
        }
        guard pages[tabID] === page,
              let result = value as? [String: Any],
              let base64 = result["base64"] as? String,
              let mime = result["mime"] as? String,
              let documentURLValue = result["documentURL"] as? String,
              let documentURL = URL(string: documentURLValue),
              (try? MobileBrowserInputRouter.validateWebURL(documentURL)) != nil,
              page.url == documentURL,
              let data = Self.validatedFaviconData(
                base64: base64,
                mime: mime
              ),
              let index = tabs.firstIndex(where: { $0.id == tabID }),
              tabs[index].faviconData != data else { return }
        tabs[index].faviconData = data
        persistSoon()
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

    public func confirmPendingExternalOpen(requestID: UUID) -> URL? {
        guard pendingExternalOpen?.id == requestID else { return nil }
        defer { pendingExternalOpen = nil }
        return pendingExternalOpen?.url
    }

    public func cancelPendingExternalOpen(requestID: UUID? = nil) {
        guard requestID == nil || pendingExternalOpen?.id == requestID else { return }
        pendingExternalOpen = nil
    }

    public func dismissError() {
        lastError = nil
    }

    public func dismissPendingLink() {
        pendingLink = nil
    }

    @discardableResult
    public func openPendingLink(
        mode: MobileBrowsingMode? = nil,
        workspaceID: WorkspaceID? = nil
    ) -> UUID? {
        guard let pendingLink else { return nil }
        self.pendingLink = nil
        return createTab(
            url: pendingLink.url,
            workspaceID: workspaceID ?? pendingLink.workspaceID,
            mode: mode ?? pendingLink.sourceMode
        )
    }

    public func flushSession() async {
        sessionRevision &+= 1
        do {
            try await saveCoordinator.enqueue(
                persistentSnapshot(),
                revision: sessionRevision
            )
        } catch {
            lastError = error.localizedDescription
        }
    }

    /// Resolves page-owned continuations before UIKit snapshots or suspends the
    /// scene. A hidden permission or JavaScript dialog must never survive in a
    /// background tab and reappear without the initiating page in view.
    public func prepareForInactiveScene() {
        permissionCoordinator.cancelPending(unlessTabID: nil)
        dialogPresenters.values.forEach { $0.cancelPending() }
        pendingLink = nil
        pendingExternalOpen = nil
    }

    public func clearPrivateTabs() {
        let privateIDs = Set(privateTabs.map(\.id))
        tabs.removeAll { privateIDs.contains($0.id) }
        if recentlyClosedTab?.mode == .privateBrowsing { recentlyClosedTab = nil }
        for id in privateIDs { permissionCoordinator.cancelPending(forTabID: id) }
        for id in privateIDs { pages.removeValue(forKey: id) }
        for id in privateIDs { dialogPresenters.removeValue(forKey: id)?.cancelPending() }
        for id in privateIDs { linkInteractionCoordinators.removeValue(forKey: id)?.invalidate() }
        if let pendingLink, privateIDs.contains(pendingLink.sourceTabID) {
            self.pendingLink = nil
        }
        for id in privateIDs { websiteDataStores.removeValue(forKey: id) }
        for id in privateIDs { navigationObservationTasks.removeValue(forKey: id)?.cancel() }
        for id in privateIDs { pageFailures.removeValue(forKey: id) }
        for id in privateIDs { faviconFetchInFlight.removeValue(forKey: id) }
        for id in privateIDs { faviconAttemptedDocumentURLs.removeValue(forKey: id) }
        for id in privateIDs { navigationDocumentGenerations.removeValue(forKey: id) }
#if DEBUG
        for id in privateIDs { uiTestRetryResponses.removeValue(forKey: id) }
#endif
        privateWebsiteDataStore = nil
        if let selectedTabID, privateIDs.contains(selectedTabID) {
            self.selectedTabID = normalTabs.last?.id
            if self.selectedTabID == nil { _ = createTab() }
        }
        persistSoon()
    }

    public func discardInactivePages(keeping maximumLivePages: Int) {
        let limit = max(1, maximumLivePages)
        let rankedTabs = tabs.enumerated().sorted { lhs, rhs in
            let lhsIsSelected = lhs.element.id == selectedTabID
            let rhsIsSelected = rhs.element.id == selectedTabID
            if lhsIsSelected != rhsIsSelected { return lhsIsSelected }
            if lhs.element.lastActiveAt != rhs.element.lastActiveAt {
                return lhs.element.lastActiveAt > rhs.element.lastActiveAt
            }
            if lhs.element.createdAt != rhs.element.createdAt {
                return lhs.element.createdAt > rhs.element.createdAt
            }
            return lhs.offset < rhs.offset
        }
        let keepIDs = Set(rankedTabs
            .prefix(limit)
            .map(\.element.id))
        for id in Array(pages.keys) where !keepIDs.contains(id) {
            permissionCoordinator.cancelPending(forTabID: id)
            pages.removeValue(forKey: id)
            dialogPresenters.removeValue(forKey: id)?.cancelPending()
            linkInteractionCoordinators.removeValue(forKey: id)?.invalidate()
            if pendingLink?.sourceTabID == id { pendingLink = nil }
            websiteDataStores.removeValue(forKey: id)
            navigationObservationTasks.removeValue(forKey: id)?.cancel()
            pageFailures.removeValue(forKey: id)
            expectedDownloadCancellationTabIDs.remove(id)
            faviconFetchInFlight.removeValue(forKey: id)
            faviconAttemptedDocumentURLs.removeValue(forKey: id)
            navigationDocumentGenerations.removeValue(forKey: id)
#if DEBUG
            uiTestRetryResponses.removeValue(forKey: id)
#endif
        }
    }

    public func retrySelectedPage() {
        guard let selectedTabID, let page = selectedPage else { return }
        pageFailures.removeValue(forKey: selectedTabID)
        observeNavigations(of: page, tabID: selectedTabID)
#if DEBUG
        if let retryResponse = uiTestRetryResponses.removeValue(
            forKey: selectedTabID
        ) {
            page.load(
                simulatedRequest: retryResponse.request,
                responseHTML: retryResponse.html
            )
            updateSelectedMetadata(
                url: retryResponse.request.url,
                title: "Ahoi Retry Fixture"
            )
            return
        }
#endif
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
        uiTestRetryResponses.removeAll()
        dialogPresenters.values.forEach { $0.cancelPending() }
        dialogPresenters.removeAll()
        linkInteractionCoordinators.values.forEach { $0.invalidate() }
        linkInteractionCoordinators.removeAll()
        navigationObservationTasks.values.forEach { $0.cancel() }
        navigationObservationTasks.removeAll()
        navigationDocumentGenerations.removeAll()
        pages.removeAll()
        websiteDataStores.removeAll()
        privateWebsiteDataStore = nil
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
              .fixture-actions { display:grid; gap:10px; max-width:340px; margin:20px 0 }
              .fixture-actions,input { max-width:100%; box-sizing:border-box }
              input[type="file"] { width:100% !important }
              label { display:block; font-weight:600; margin:18px 0 8px }
            </style></head><body>
            <main><h1>Ahoi fixture page</h1>
            <p>This page is provided locally for deterministic browser UI tests.</p>
            <p id="find-target">Ahoi visible find target</p>
            <ul>
              <li><a href="https://example.com">Open HTTPS page</a></li>
              <li><a id="link-actions-fixture" href="https://example.com/ahoi-link-actions" aria-label="Open Ahoi link actions">Long-press for link actions</a></li>
              <li><a href="https://example.com/?ahoi-popup=1" target="_blank">Open target blank</a></li>
              <li><a href="https://httpbin.org/response-headers?Content-Disposition=attachment%3B%20filename%3Dahoi-fixture.txt&amp;Content-Type=text%2Fplain">Download fixture</a></li>
              <li><a href="mailto:browser-test@example.com">Open mail app</a></li>
            </ul>
            <section class="fixture-actions" aria-label="JavaScript dialog fixtures">
              <button id="js-alert" aria-label="Show JavaScript alert" onclick="alert('Ahoi alert fixture')">Show JavaScript alert</button>
              <button id="js-confirm" aria-label="Show JavaScript confirm" onclick="showFixtureConfirm()">Show JavaScript confirm</button>
              <button id="js-prompt" aria-label="Show JavaScript prompt" onclick="showFixturePrompt()">Show JavaScript prompt</button>
              <output id="dialog-result" aria-live="polite">No dialog result yet.</output>
            </section>
            <label for="upload">Choose a fixture file</label>
            <p><input id="upload" aria-label="Choose a fixture file" type="file" style="font-size:20px;padding:12px;border:1px solid #777;border-radius:10px;width:340px"></p>
            <section class="fixture-actions" aria-label="Website permission fixtures">
              <button id="camera" onclick="requestMedia('camera')">Request camera</button>
              <button id="microphone" onclick="requestMedia('microphone')">Request microphone</button>
              <button id="motion" onclick="requestMotion()">Request motion</button>
              <output id="permission-result" aria-live="polite">No permission result yet.</output>
            </section>
            </main>
            <script>
              const dialogResult = document.getElementById('dialog-result');
              function showFixtureConfirm() {
                dialogResult.textContent = confirm('Confirm the Ahoi fixture?')
                  ? 'Confirm accepted.'
                  : 'Confirm cancelled.';
              }
              function showFixturePrompt() {
                const value = prompt('Enter the Ahoi fixture value', 'Ahoi');
                dialogResult.textContent = value === null
                  ? 'Prompt cancelled.'
                  : 'Prompt value: ' + value;
              }
              const permissionResult = document.getElementById('permission-result');
              async function requestMedia(kind) {
                try {
                  const constraints = kind === 'camera' ? {video:true} : {audio:true};
                  const stream = await navigator.mediaDevices.getUserMedia(constraints);
                  permissionResult.textContent = kind + ' granted.';
                  stream.getTracks().forEach(track => track.stop());
                } catch (error) {
                  permissionResult.textContent = kind + ' denied: ' + (error?.name || 'Error') + '.';
                }
              }
              async function requestMotion() {
                try {
                  const result = typeof DeviceMotionEvent.requestPermission === 'function'
                    ? await DeviceMotionEvent.requestPermission()
                    : 'available';
                  permissionResult.textContent = 'motion ' + result + '.';
                } catch (error) {
                  permissionResult.textContent = 'motion denied: ' + (error?.name || 'Error') + '.';
                }
              }
            </script></body></html>
            """
        )
        updateSelectedMetadata(url: fixtureURL, title: "Ahoi Fixture")
    }

    public func loadUITestOfflineFailure() {
        uiTestRetryResponses.removeAll()
        dialogPresenters.values.forEach { $0.cancelPending() }
        dialogPresenters.removeAll()
        linkInteractionCoordinators.values.forEach { $0.invalidate() }
        linkInteractionCoordinators.removeAll()
        navigationObservationTasks.values.forEach { $0.cancel() }
        navigationObservationTasks.removeAll()
        navigationDocumentGenerations.removeAll()
        pages.removeAll()
        websiteDataStores.removeAll()
        privateWebsiteDataStore = nil
        tabs.removeAll()
        selectedTabID = nil
        pageFailures.removeAll()
        let fixtureURL = URL(string: "https://fixture.ahoibrowser.test/offline")!
        let tabID = createTab()
        guard page(for: tabID, createIfBlank: true) != nil else { return }
        updateSelectedMetadata(url: fixtureURL, title: "Ahoi Offline Fixture")
        uiTestRetryResponses[tabID] = (
            request: URLRequest(url: fixtureURL),
            html: """
            <!doctype html><html lang="en"><head>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <meta name="theme-color" content="#0f766e">
            <title>Ahoi Retry Fixture</title>
            <style>
              :root { color-scheme:light dark }
              body { font:17px -apple-system,sans-serif; margin:28px; line-height:1.45 }
              h1 { color:#0f766e }
            </style></head><body>
            <main aria-live="polite">
              <h1 id="retry-recovered">Ahoi is back online</h1>
              <p>The deterministic retry completed without network access.</p>
            </main></body></html>
            """
        )
        pageFailures[tabID] = .offline
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
                    case .startedProvisionalNavigation:
                        self.navigationDocumentGenerations[tabID, default: 0] &+= 1
                        self.expectedDownloadCancellationTabIDs.remove(tabID)
                        self.permissionCoordinator.cancelPending(forTabID: tabID)
                        self.dialogPresenters[tabID]?.cancelPending()
                        if self.pendingLink?.sourceTabID == tabID {
                            self.pendingLink = nil
                        }
                        if self.pendingExternalOpen?.sourceTabID == tabID {
                            self.pendingExternalOpen = nil
                        }
                        self.faviconFetchInFlight.removeValue(forKey: tabID)
                        self.faviconAttemptedDocumentURLs.removeValue(forKey: tabID)
                        self.pageFailures.removeValue(forKey: tabID)
                    case .committed:
                        self.pageFailures.removeValue(forKey: tabID)
                    case .finished:
                        self.expectedDownloadCancellationTabIDs.remove(tabID)
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
        let documentGeneration = navigationDocumentGenerations[tabID, default: 0]
        let documentURL = page.url
        let declaredValue: Any? = try? await page.callJavaScript(
            "return document.querySelector('meta[name=\"theme-color\"]')?.content || '';"
        )
        let declaredColor = (declaredValue as? String)
            .flatMap(Self.argbColor(from:))
            .flatMap { Self.isEligibleWebsiteTint($0) ? $0 : nil }
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
              tabs[currentIndex].mode == .normal,
              pages[tabID] === page,
              page.url == documentURL,
              navigationDocumentGenerations[tabID, default: 0] == documentGeneration else {
            return
        }
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

    private static func isEligibleWebsiteTint(_ argb: UInt32) -> Bool {
        let channels = [
            Double((argb >> 16) & 0xFF) / 255,
            Double((argb >> 8) & 0xFF) / 255,
            Double(argb & 0xFF) / 255,
        ]
        guard let maximum = channels.max(), let minimum = channels.min() else {
            return false
        }
        let lightness = (maximum + minimum) / 2
        let saturation = maximum == minimum
            ? 0
            : (maximum - minimum) / (1 - abs(2 * lightness - 1))
        return lightness > 0.10 && lightness < 0.90 && saturation > 0.16
    }

    private static func validatedFaviconData(
        base64: String,
        mime: String
    ) -> Data? {
        let maximumEncodedLength = ((maximumFaviconBytes + 2) / 3) * 4
        guard base64.utf8.count <= maximumEncodedLength,
              allowedFaviconMIMETypes.contains(mime.lowercased()),
              let data = Data(base64Encoded: base64),
              !data.isEmpty,
              data.count <= maximumFaviconBytes,
              let source = CGImageSourceCreateWithData(data as CFData, [
                kCGImageSourceShouldCache: false,
              ] as CFDictionary) else {
            return nil
        }
        let imageCount = CGImageSourceGetCount(source)
        guard imageCount > 0, imageCount <= 16 else { return nil }
        for index in 0..<imageCount {
            guard let properties = CGImageSourceCopyPropertiesAtIndex(
                source,
                index,
                [kCGImageSourceShouldCache: false] as CFDictionary
            ) as? [CFString: Any],
                  let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
                  let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
                  width > 0,
                  height > 0,
                  width <= maximumFaviconDimension,
                  height <= maximumFaviconDimension,
                  width * height <= maximumFaviconPixels else {
                return nil
            }
        }
        let thumbnailOptions: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true,
            kCGImageSourceThumbnailMaxPixelSize: persistedFaviconDimension,
        ]
        guard let thumbnail = CGImageSourceCreateThumbnailAtIndex(
            source,
            0,
            thumbnailOptions as CFDictionary
        ) else {
            return nil
        }
        let normalized = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(
            normalized,
            UTType.png.identifier as CFString,
            1,
            nil
        ) else {
            return nil
        }
        CGImageDestinationAddImage(destination, thumbnail, nil)
        guard CGImageDestinationFinalize(destination) else { return nil }
        let normalizedData = normalized as Data
        guard !normalizedData.isEmpty,
              normalizedData.count <= maximumFaviconBytes else {
            return nil
        }
        return normalizedData
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
        let websiteDataStore: WKWebsiteDataStore
        if mode == .privateBrowsing {
            if let privateWebsiteDataStore {
                websiteDataStore = privateWebsiteDataStore
            } else {
                let created = WKWebsiteDataStore.nonPersistent()
                privateWebsiteDataStore = created
                websiteDataStore = created
            }
        } else {
            websiteDataStore = .default()
        }
        websiteDataStores[tabID] = websiteDataStore
        configuration.websiteDataStore = websiteDataStore
        configuration.upgradeKnownHostsToHTTPS = true
        configuration.mediaPlaybackBehavior = .allowsInlinePlayback
        configuration.deviceSensorAuthorization = .init { [weak self] permission, _, origin in
            guard let self,
                  self.selectedTabID == tabID,
                  self.tabs.contains(where: { $0.id == tabID }) else {
                return .deny
            }
            return await self.permissionCoordinator.request(
                permission: permission,
                origin: origin,
                tabID: tabID
            )
        }
        let linkCoordinator = MobileLinkInteractionCoordinator(
            userContentController: configuration.userContentController
        ) { [weak self] url, sourceOrigin in
            guard let self,
                  let sourceTab = self.tabs.first(where: { $0.id == tabID }) else {
                return
            }
            self.pendingLink = MobilePendingLink(
                url: url,
                sourceTabID: tabID,
                sourceOrigin: sourceOrigin,
                workspaceID: sourceTab.workspaceID,
                sourceMode: sourceTab.mode
            )
        }
        linkInteractionCoordinators[tabID] = linkCoordinator

        let policy = MobileNavigationPolicyHandler()
        let dialogPresenter = MobileWebDialogPresenter()
        dialogPresenters[tabID] = dialogPresenter
        policy.onOpenNewTab = { [weak self] url in
            guard let self else { return }
            let workspaceID = self.tabs.first(where: { $0.id == tabID })?.workspaceID
            _ = self.createTab(
                url: url,
                workspaceID: workspaceID,
                mode: mode
            )
        }
        policy.onExternalScheme = { [weak self] url, origin in
            guard let self,
                  self.selectedTabID == tabID,
                  self.tabs.contains(where: { $0.id == tabID }) else {
                return
            }
            self.pendingExternalOpen = MobilePendingExternalOpen(
                url: url,
                origin: origin,
                sourceTabID: tabID
            )
        }
        policy.onDownload = { [weak self] request in
            self?.expectedDownloadCancellationTabIDs.insert(tabID)
            self?.pageFailures.removeValue(forKey: tabID)
            self?.downloadCoordinator.start(
                request: request,
                websiteDataStore: websiteDataStore,
                initiatingOrigin: self?.tabs.first(where: { $0.id == tabID })?.url
                    .flatMap(URL.init(string:))
                    .map(MobileBrowserOriginFormatter.label(for:)),
                isPrivate: mode == .privateBrowsing
            )
        }
        policy.onDownloadRejected = { [weak self] url, reason in
            guard let self else { return }
            let message: String
            switch reason {
            case .unsafeMethod(let method):
                message = CompanionL10n.format(
                    "browser.download.error.unsafe_method",
                    fallback: "AhoiBrowser did not repeat this %@ download request because it could submit data twice.",
                    method
                )
            case .unmatchedResponse:
                message = CompanionL10n.string(
                    "browser.download.error.unmatched_response",
                    fallback: "AhoiBrowser could not safely match this download response to its original request, so no request was repeated."
                )
            }
            self.expectedDownloadCancellationTabIDs.insert(tabID)
            self.pageFailures.removeValue(forKey: tabID)
            self.lastError = message
            guard let url else { return }
            let sourceOrigin = self.tabs.first(where: { $0.id == tabID })?.url
                .flatMap(URL.init(string:))
                .map(MobileBrowserOriginFormatter.label(for:))
            self.downloadCoordinator.recordFailure(
                sourceURL: url,
                initiatingOrigin: sourceOrigin,
                isPrivate: mode == .privateBrowsing,
                message: message
            )
        }
        return WebPage(
            configuration: configuration,
            navigationDecider: policy,
            dialogPresenter: dialogPresenter
        )
    }

    private func updateSelectedMetadata(url: URL?, title: String?) {
        guard let selectedTabID,
              let index = tabs.firstIndex(where: { $0.id == selectedTabID }) else { return }
        if let url, (try? MobileBrowserInputRouter.validateWebURL(url)) != nil {
            guard let newValue = MobileTabRecord.normalizedURLString(url.absoluteString) else {
                return
            }
            if tabs[index].url != newValue {
                tabs[index].faviconData = nil
                faviconFetchInFlight.removeValue(forKey: selectedTabID)
                faviconAttemptedDocumentURLs.removeValue(forKey: selectedTabID)
                tabs[index].url = newValue
            }
        }
        if let title, !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            tabs[index].title = MobileTabRecord.normalizedTitle(title)
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
        sessionRevision &+= 1
        let revision = sessionRevision
        let snapshot = persistentSnapshot()
        Task {
            do {
                try await saveCoordinator.enqueue(snapshot, revision: revision)
            } catch {
                await MainActor.run { self.lastError = error.localizedDescription }
            }
        }
    }

    private func persistentSnapshot() -> MobileBrowserSessionSnapshot {
        let persistentTabs = normalTabs
        let persistentSelection = persistentTabs.contains { $0.id == selectedTabID }
            ? selectedTabID
            : persistentTabs.last?.id
        return MobileBrowserSessionSnapshot(
            tabs: persistentTabs,
            selectedTabID: persistentSelection
        )
    }
}
