import Foundation
import SwiftUI
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
    @Published public internal(set) var tabs: [MobileTabRecord] = []
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
    @Published public internal(set) var lastError: String?
    @Published public internal(set) var recentlyClosedTab: MobileTabRecord?
    @Published public internal(set) var pendingExternalOpen: MobilePendingExternalOpen?
    @Published public internal(set) var pendingLink: MobilePendingLink?
    @Published public internal(set) var pageFailures: [UUID: MobilePageFailureKind] = [:]

    public let permissionCoordinator: MobilePermissionCoordinator
    public let downloadCoordinator: MobileDownloadCoordinator

    private let store: any MobileBrowserSessionStoring
    private let saveCoordinator: MobileBrowserSessionSaveCoordinator
    private let storagePreparation: (@Sendable () async throws -> Void)?
    var pages: [UUID: WebPage] = [:]
    var dialogPresenters: [UUID: MobileWebDialogPresenter] = [:]
    var linkInteractionCoordinators: [UUID: MobileLinkInteractionCoordinator] = [:]
    var websiteDataStores: [UUID: WKWebsiteDataStore] = [:]
    var privateWebsiteDataStore: WKWebsiteDataStore?
    private var lastRecordedHistoryURL: [UUID: String] = [:]
    private var desktopSiteTabIDs: Set<UUID> = []
    private var externalOpenDeduplicator = MobileExternalOpenDeduplicator()
    private var pendingStartupURL: URL?
    var navigationObservationTasks: [UUID: Task<Void, Never>] = [:]
    var expectedDownloadCancellationTabIDs: Set<UUID> = []
    var faviconFetchInFlight: [UUID: String] = [:]
    var faviconAttemptedDocumentURLs: [UUID: String] = [:]
    var navigationDocumentGenerations: [UUID: UInt64] = [:]
    private var didLoad = false
    private var sessionRevision: UInt64 = 0
    static let maximumFaviconBytes = MobileTabRecord.maximumFaviconDataBytes
    static let maximumFaviconDimension = 1_024
    static let maximumFaviconPixels = 1_048_576
    static let persistedFaviconDimension = 64
    static let faviconContentWorld = WKContentWorld.world(
        name: "AhoiBrowser.Favicon"
    )
    static let allowedFaviconMIMETypes: Set<String> = [
        "image/avif",
        "image/jpeg",
        "image/png",
        "image/vnd.microsoft.icon",
        "image/webp",
        "image/x-icon",
    ]
#if DEBUG
    var uiTestRetryResponses: [UUID: (request: URLRequest, html: String)] = [:]
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

    var selectedLinkInteractionCoordinator: MobileLinkInteractionCoordinator? {
        guard let selectedTabID else { return nil }
        return linkInteractionCoordinators[selectedTabID]
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
        guard !normalized.isEmpty, selectedTab?.mode == .normal else { return [] }
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

    public func handleExternalURL(_ url: URL) {
        do {
            let safeURL = try MobileBrowserInputRouter.validateWebURL(url)
            guard didLoad else {
                pendingStartupURL = safeURL
                return
            }
            guard externalOpenDeduplicator.accepts(safeURL) else { return }
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


    func updateSelectedMetadata(url: URL?, title: String?) {
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
        guard externalOpenDeduplicator.accepts(pendingStartupURL) else { return }
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

    func persistSoon() {
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
