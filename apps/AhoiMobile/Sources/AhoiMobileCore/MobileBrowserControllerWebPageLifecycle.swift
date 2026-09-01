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
        if record.url != nil {
            guard let url = Self.validatedRecoveryURL(for: record) else {
                pageFailures[tabID] = .invalidURL
                return page
            }
            page.load(url)
        }
        return page
    }

    func observeNavigations(of page: WebPage, tabID: UUID) {
        guard pages[tabID] === page,
              tabs.contains(where: { $0.id == tabID }) else {
            return
        }
        navigationObservationTasks.removeValue(forKey: tabID)?.cancel()
        navigationDocumentGenerations[tabID, default: 0] &+= 1
        let initialGeneration = navigationDocumentGenerations[tabID, default: 0]
        // Apple documents that a navigation sequence starts tracking when it
        // is created. Capture it synchronously before callers invoke load(); a
        // MainActor Task may otherwise start after an immediate DNS/URL error.
        let initialNavigations = page.navigations
        navigationObservationTasks[tabID] = Task { @MainActor [weak self, weak page] in
            guard let self, let page else { return }
            var callbackGeneration = initialGeneration
            var recoveryGate = MobileWebContentRecoveryGate()
            var pendingRecoveryURL: URL?
            var navigations = initialNavigations

            while !Task.isCancelled {
                do {
                    if let recoveryURL = pendingRecoveryURL {
                        guard self.isCurrentNavigationCallback(
                            page: page,
                            tabID: tabID,
                            generation: callbackGeneration
                        ) else { return }
                        pendingRecoveryURL = nil
                        navigations = page.navigations
                        // A fresh URL load is an idempotent GET. It restores a
                        // terminated content process without resubmitting a
                        // possibly state-changing form request.
                        page.load(recoveryURL)
                    }

                    for try await event in navigations {
                        guard !Task.isCancelled,
                              self.isCurrentNavigationCallback(
                                  page: page,
                                  tabID: tabID,
                                  generation: callbackGeneration
                              ) else {
                            return
                        }
                        switch event {
                        case .startedProvisionalNavigation:
                            self.navigationDocumentGenerations[tabID, default: 0] &+= 1
                            callbackGeneration = self.navigationDocumentGenerations[
                                tabID,
                                default: 0
                            ]
                            self.resetTransientNavigationState(for: tabID)
                            self.pageFailures.removeValue(forKey: tabID)
                        case .committed:
                            self.pageFailures.removeValue(forKey: tabID)
                        case .finished:
                            recoveryGate.resetAfterFinishedNavigation()
                            self.expectedPolicyCancellationTabIDs.remove(tabID)
                            self.pageFailures.removeValue(forKey: tabID)
                            await self.sampleWebsiteTint(from: page, tabID: tabID)
                            guard self.isCurrentNavigationCallback(
                                page: page,
                                tabID: tabID,
                                generation: callbackGeneration
                            ) else { return }
                        case .receivedServerRedirect:
                            break
                        @unknown default:
                            break
                        }
                    }
                    return
                } catch {
                    guard !Task.isCancelled,
                          self.isCurrentNavigationCallback(
                              page: page,
                              tabID: tabID,
                              generation: callbackGeneration
                          ) else {
                        return
                    }
                    let expectedPolicyCancellation =
                        self.expectedPolicyCancellationTabIDs.remove(tabID) != nil
                    switch MobileNavigationObservationFailurePolicy.action(
                        expectedPolicyCancellation: expectedPolicyCancellation,
                        isNavigationCancellation: Self.isNavigationCancellation(error)
                    ) {
                    case .resubscribeAfterCancellation:
                        if !expectedPolicyCancellation || self.pageFailures[tabID] == nil {
                            self.pageFailures.removeValue(forKey: tabID)
                        }
                        // Downloads, stopLoading(), and superseded in-page
                        // loads can all cancel a WebKit navigation sequence.
                        // Rebind after yielding so later page-owned navigation
                        // remains observable without spinning on a completed
                        // AsyncSequence.
                        let resubscribeGeneration = callbackGeneration
                        Task { @MainActor [weak self, weak page] in
                            await Task.yield()
                            guard let self, let page,
                                  self.isCurrentNavigationCallback(
                                      page: page,
                                      tabID: tabID,
                                      generation: resubscribeGeneration
                                  ) else { return }
                            self.observeNavigations(of: page, tabID: tabID)
                        }
                        return
                    case .classifyFailure:
                        break
                    }
                    let classification = Self.navigationFailureClassification(error)
                    self.retainNavigationFailureDestination(
                        from: error,
                        tabID: tabID
                    )
                    if let tab = self.tabs.first(where: { $0.id == tabID }),
                       let recoveryURL = Self.validatedRecoveryURL(
                           for: tab,
                           preferredURL: page.url
                       ),
                       recoveryGate.claim(for: classification) {
                        self.pageFailures.removeValue(forKey: tabID)
                        pendingRecoveryURL = recoveryURL
                        continue
                    }
                    self.pageFailures[tabID] = classification.pageFailureKind
                    return
                }
            }
        }
    }

    private func isCurrentNavigationCallback(
        page: WebPage,
        tabID: UUID,
        generation: UInt64
    ) -> Bool {
        MobileNavigationCallbackValidity.accepts(
            expectedGeneration: generation,
            currentGeneration: navigationDocumentGenerations[tabID],
            tabExists: tabs.contains(where: { $0.id == tabID }),
            pageIsCurrent: pages[tabID] === page
        )
    }

    private func retainNavigationFailureDestination(
        from error: Error,
        tabID: UUID
    ) {
        guard let failedURL = Self.validatedNavigationFailureURL(error),
              let tabIndex = tabs.firstIndex(where: { $0.id == tabID }),
              MobileNavigationFailureDestinationPolicy.apply(
                  failedURL,
                  to: &tabs[tabIndex]
              ) else {
            return
        }
        faviconFetchInFlight.removeValue(forKey: tabID)
        faviconAttemptedDocumentURLs.removeValue(forKey: tabID)
        persistSoon()
    }

    private func resetTransientNavigationState(for tabID: UUID) {
        expectedPolicyCancellationTabIDs.remove(tabID)
        permissionCoordinator.cancelPending(forTabID: tabID)
        dialogPresenters[tabID]?.cancelPending()
        if pendingLink?.sourceTabID == tabID {
            pendingLink = nil
        }
        if pendingExternalOpen?.sourceTabID == tabID {
            pendingExternalOpen = nil
        }
        faviconFetchInFlight.removeValue(forKey: tabID)
        faviconAttemptedDocumentURLs.removeValue(forKey: tabID)
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
        // Certificate trust and ATS policy deliberately stay with WebKit's
        // default handling. Never install a permissive challenge override.
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
        policy.onBlockedNavigation = { [weak self] _ in
            guard let self, self.selectedTabID == tabID else { return }
            self.lastError = CompanionL10n.string(
                "browser.error.blocked_scheme",
                fallback: "AhoiBrowser only opens safe web links and approved app links."
            )
        }
        policy.onHTTPFailure = { [weak self] url, _, failure in
            guard let self,
                  let tabIndex = self.tabs.firstIndex(where: { $0.id == tabID }) else {
                return
            }
            self.expectedPolicyCancellationTabIDs.insert(tabID)
            self.pageFailures[tabID] = failure
            if MobileNavigationFailureDestinationPolicy.apply(
                url,
                to: &self.tabs[tabIndex]
            ) {
                self.faviconFetchInFlight.removeValue(forKey: tabID)
                self.faviconAttemptedDocumentURLs.removeValue(forKey: tabID)
                self.persistSoon()
            }
        }
        policy.onDownload = { [weak self] request, sourceOrigin in
            self?.expectedPolicyCancellationTabIDs.insert(tabID)
            self?.pageFailures.removeValue(forKey: tabID)
            self?.downloadCoordinator.start(
                request: request,
                websiteDataStore: websiteDataStore,
                initiatingOrigin: sourceOrigin,
                isPrivate: mode == .privateBrowsing
            )
        }
        policy.onDownloadRejected = { [weak self] url, initiatingOrigin, reason in
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
            self.expectedPolicyCancellationTabIDs.insert(tabID)
            self.pageFailures.removeValue(forKey: tabID)
            self.lastError = message
            guard let url else { return }
            self.downloadCoordinator.recordFailure(
                sourceURL: url,
                initiatingOrigin: initiatingOrigin,
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
