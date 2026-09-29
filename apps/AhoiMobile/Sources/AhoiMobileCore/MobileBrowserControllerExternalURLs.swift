import Foundation
import WebKit

// Opening links handed to the app from outside (URL schemes, Universal Links,
// share/open-in) and the confirmation of links that leave the browser (split
// from MobileBrowserController.swift, source line budget).
extension MobileBrowserController {
    public func handleExternalURL(_ url: URL) {
        do {
            let safeURL = try MobileBrowserInputRouter.validateWebURL(url)
            guard didLoad else {
                // Claim at receipt time, not after session restoration. Cold
                // bootstrap latency must not consume the redelivery window.
                guard externalOpenDeduplicator.accepts(safeURL) else { return }
                stageStartupURL(safeURL, wasClaimed: true)
                return
            }
            guard externalOpenDeduplicator.accepts(safeURL) else { return }
            lastError = nil
            openValidatedExternalURL(safeURL)
        } catch {
            lastError = CompanionL10n.string(
                "browser.error.blocked_scheme",
                fallback: "AhoiBrowser only opens HTTP and HTTPS links here."
            )
        }
    }

    /// Opens a URL whose activation was already atomically claimed by the app
    /// root. The separate path prevents the receiving controller from rejecting
    /// the one authoritative delivery against its own persistent receipt.
    public func handleClaimedExternalURL(_ url: URL) {
        do {
            let safeURL = try MobileBrowserInputRouter.validateWebURL(url)
            guard didLoad else {
                stageStartupURL(safeURL, wasClaimed: true)
                return
            }
            externalOpenDeduplicator.rememberAccepted(safeURL)
            lastError = nil
            openValidatedExternalURL(safeURL)
        } catch {
            handleExternalURL(url)
        }
    }

    public func confirmPendingExternalOpen(requestID: UUID) -> URL? {
        guard let request = pendingExternalOpen,
              request.id == requestID else { return nil }
        defer { pendingExternalOpen = nil }
        guard case .externalApp(let safeURL) = MobileNavigationTargetPolicy.decide(request.url)
        else { return nil }
        return safeURL
    }

    public func cancelPendingExternalOpen(requestID: UUID? = nil) {
        guard requestID == nil || pendingExternalOpen?.id == requestID else { return }
        pendingExternalOpen = nil
    }

    func drainPendingStartupURL() {
        guard let pendingStartupURL else { return }
        let wasClaimed = pendingStartupURLWasClaimed
        self.pendingStartupURL = nil
        pendingStartupURLWasClaimed = false
        if wasClaimed {
            externalOpenDeduplicator.rememberAccepted(pendingStartupURL)
        } else {
            guard externalOpenDeduplicator.accepts(pendingStartupURL) else { return }
        }
        openValidatedExternalURL(pendingStartupURL)
    }

    private func stageStartupURL(_ url: URL, wasClaimed: Bool) {
        if pendingStartupURL == url {
            pendingStartupURLWasClaimed = pendingStartupURLWasClaimed || wasClaimed
            return
        }
        pendingStartupURL = url
        pendingStartupURLWasClaimed = wasClaimed
    }

    private func openValidatedExternalURL(_ safeURL: URL) {
        if selectedTab?.url == nil, selectedTab?.mode == .normal {
            guard let selectedTabID,
                  let page = page(for: selectedTabID, createIfBlank: true) else { return }
            observeNavigations(of: page, tabID: selectedTabID)
            prepareExplicitSharedNavigation(tabID: selectedTabID, url: safeURL)
            page.load(safeURL)
            updateSelectedMetadata(url: safeURL, title: nil)
        } else {
            _ = createTab(url: safeURL)
        }
    }
}
