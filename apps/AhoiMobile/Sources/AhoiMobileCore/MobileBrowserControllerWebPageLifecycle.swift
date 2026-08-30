import Foundation
import WebKit

extension MobileBrowserController {
    func page(for tabID: UUID, createIfBlank: Bool = false) -> WebPage? {
        if let page = pages[tabID] { return page }
        guard let record = tabs.first(where: { $0.id == tabID }) else { return nil }
        guard createIfBlank || record.url != nil else { return nil }
        let page = makePage(tabID: tabID, mode: record.mode)
        pages[tabID] = page
        observeNavigations(of: page, tabID: tabID)
        if let value = record.url, let url = URL(string: value) { page.load(url) }
        return page
    }

    func observeNavigations(of page: WebPage, tabID: UUID) {
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

    func makePage(tabID: UUID, mode: MobileBrowsingMode) -> WebPage {
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

}
