import Foundation
import WebKit

private let maximumUnderlyingNavigationFailureDepth = 12

private let transportSecurityNavigationErrorCodes: Set<Int> = [
    NSURLErrorAppTransportSecurityRequiresSecureConnection,
    NSURLErrorSecureConnectionFailed,
    NSURLErrorServerCertificateHasBadDate,
    NSURLErrorServerCertificateUntrusted,
    NSURLErrorServerCertificateHasUnknownRoot,
    NSURLErrorServerCertificateNotYetValid,
    NSURLErrorClientCertificateRejected,
    NSURLErrorClientCertificateRequired,
]

enum MobileNavigationFailureClassification: Equatable, Sendable {
    case offline
    case dnsLookupFailed
    case timedOut
    case invalidURL
    case transportSecurity
    case webContentTerminated
    case failed

    var pageFailureKind: MobilePageFailureKind {
        switch self {
        case .offline:
            .offline
        case .dnsLookupFailed:
            .dnsLookupFailed
        case .timedOut:
            .timedOut
        case .invalidURL:
            .invalidURL
        case .transportSecurity:
            .transportSecurity
        case .failed:
            .failed
        case .webContentTerminated:
            .webContentTerminated
        }
    }
}

struct MobileWebContentRecoveryGate: Sendable {
    private(set) var didAttemptRecovery = false

    mutating func claim(
        for classification: MobileNavigationFailureClassification
    ) -> Bool {
        guard classification == .webContentTerminated,
              !didAttemptRecovery else {
            return false
        }
        didAttemptRecovery = true
        return true
    }

    mutating func resetAfterFinishedNavigation() {
        didAttemptRecovery = false
    }
}

enum MobileNavigationCallbackValidity {
    static func accepts(
        expectedGeneration: UInt64,
        currentGeneration: UInt64?,
        tabExists: Bool,
        pageIsCurrent: Bool
    ) -> Bool {
        tabExists &&
            pageIsCurrent &&
            currentGeneration == expectedGeneration
    }
}

enum MobileNavigationObservationFailureAction: Equatable, Sendable {
    case resubscribeAfterCancellation
    case classifyFailure
}

enum MobileNavigationObservationFailurePolicy {
    static func action(
        expectedPolicyCancellation: Bool,
        isNavigationCancellation: Bool
    ) -> MobileNavigationObservationFailureAction {
        if expectedPolicyCancellation || isNavigationCancellation {
            return .resubscribeAfterCancellation
        }
        return .classifyFailure
    }
}

enum MobileNavigationFailureDestinationPolicy {
    static func apply(_ url: URL, to tab: inout MobileTabRecord) -> Bool {
        guard let normalizedURL = MobileTabRecord.normalizedURLString(
            url.absoluteString
        ) else {
            return false
        }
        let didChange = tab.url != normalizedURL ||
            !tab.title.isEmpty ||
            tab.faviconData != nil ||
            tab.websiteTintARGB != nil
        tab.url = normalizedURL
        // A failed provisional navigation has no new document title. Keeping
        // the previous document's title would mislabel the failed destination.
        // The separately stored user title deliberately survives.
        tab.title = ""
        tab.faviconData = nil
        tab.websiteTintARGB = nil
        return didChange
    }
}

enum MobilePageRetryFeedbackPolicy {
    static let minimumVisibleDuration: Duration = .milliseconds(800)
}

struct MobilePageRetryFeedbackRegistry: Equatable, Sendable {
    private var generations: [UUID: UInt64] = [:]

    mutating func begin(tabID: UUID) -> UInt64 {
        let generation = (generations[tabID] ?? 0) &+ 1
        generations[tabID] = generation
        return generation
    }

    mutating func finish(tabID: UUID, generation: UInt64) -> Bool {
        guard generations[tabID] == generation else { return false }
        generations.removeValue(forKey: tabID)
        return true
    }

    mutating func cancel(tabID: UUID) {
        generations.removeValue(forKey: tabID)
    }
}

extension MobileBrowserController {
    public var selectedPageIsRetrying: Bool {
        selectedTabID.map(pageRetryFeedbackTabIDs.contains) ?? false
    }

    public func retrySelectedPage() {
        guard let selectedTabID, let page = selectedPage else { return }
        pageFailures.removeValue(forKey: selectedTabID)
        beginPageRetryFeedback(for: selectedTabID)
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
        // HTTP response failures can be rejected before WebKit commits the
        // failed URL. In that case `page.url` still names the previous page,
        // while the tab record is the recovery source of truth. Reloading the
        // WebPage would silently retry the wrong document.
        if let tab = selectedTab,
           let recoveryURL = Self.validatedRecoveryURL(for: tab) {
            page.load(recoveryURL)
        } else if page.url != nil {
            page.reload()
        }
    }

    private func beginPageRetryFeedback(for tabID: UUID) {
        let generation = pageRetryFeedbackRegistry.begin(tabID: tabID)
        pageRetryFeedbackTabIDs.insert(tabID)
        Task { @MainActor [weak self] in
            try? await Task.sleep(
                for: MobilePageRetryFeedbackPolicy.minimumVisibleDuration
            )
            guard !Task.isCancelled else { return }
            guard let self,
                  pageRetryFeedbackRegistry.finish(
                      tabID: tabID,
                      generation: generation
                  ) else { return }
            pageRetryFeedbackTabIDs.remove(tabID)
        }
    }

    public static func classifyNavigationFailure(_ error: Error) -> MobilePageFailureKind {
        navigationFailureClassification(error).pageFailureKind
    }

    static func navigationFailureClassification(
        _ error: Error
    ) -> MobileNavigationFailureClassification {
        var visitedErrors = Set<ObjectIdentifier>()
        return navigationFailureClassification(
            error,
            depth: 0,
            visitedErrors: &visitedErrors
        )
    }

    static func isNavigationCancellation(_ error: Error) -> Bool {
        var visitedErrors = Set<ObjectIdentifier>()
        return isNavigationCancellation(
            error,
            depth: 0,
            visitedErrors: &visitedErrors
        )
    }

    static func validatedRecoveryURL(
        for tab: MobileTabRecord,
        preferredURL: URL? = nil
    ) -> URL? {
        if let preferredURL {
            return try? MobileBrowserInputRouter.validateWebURL(preferredURL)
        }
        guard let value = tab.url,
              let url = URL(string: value) else {
            return nil
        }
        return try? MobileBrowserInputRouter.validateWebURL(url)
    }

    static func validatedNavigationFailureURL(_ error: Error) -> URL? {
        var visitedErrors = Set<ObjectIdentifier>()
        return validatedNavigationFailureURL(
            error,
            depth: 0,
            visitedErrors: &visitedErrors
        )
    }

    private static func navigationFailureClassification(
        _ error: Error,
        depth: Int,
        visitedErrors: inout Set<ObjectIdentifier>
    ) -> MobileNavigationFailureClassification {
        guard depth < maximumUnderlyingNavigationFailureDepth else { return .failed }
        if let navigationError = error as? WebPage.NavigationError {
            switch navigationError {
            case .failedProvisionalNavigation(let underlying):
                return navigationFailureClassification(
                    underlying,
                    depth: depth + 1,
                    visitedErrors: &visitedErrors
                )
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
        guard visitedErrors.insert(ObjectIdentifier(failure)).inserted else {
            return .failed
        }
        if failure.domain == NSURLErrorDomain {
            switch failure.code {
            case NSURLErrorNotConnectedToInternet,
                 NSURLErrorNetworkConnectionLost,
                 NSURLErrorInternationalRoamingOff,
                 NSURLErrorCallIsActive,
                 NSURLErrorDataNotAllowed:
                return .offline
            case NSURLErrorCannotFindHost, NSURLErrorDNSLookupFailed:
                return .dnsLookupFailed
            case NSURLErrorTimedOut:
                return .timedOut
            case NSURLErrorBadURL, NSURLErrorUnsupportedURL:
                return .invalidURL
            default:
                if transportSecurityNavigationErrorCodes.contains(failure.code) {
                    return .transportSecurity
                }
            }
        }
        if let underlying = failure.userInfo[NSUnderlyingErrorKey] as? Error {
            return navigationFailureClassification(
                underlying,
                depth: depth + 1,
                visitedErrors: &visitedErrors
            )
        }
        return .failed
    }

    private static func validatedNavigationFailureURL(
        _ error: Error,
        depth: Int,
        visitedErrors: inout Set<ObjectIdentifier>
    ) -> URL? {
        guard depth < maximumUnderlyingNavigationFailureDepth else { return nil }
        if let navigationError = error as? WebPage.NavigationError {
            guard case .failedProvisionalNavigation(let underlying) = navigationError else {
                return nil
            }
            return validatedNavigationFailureURL(
                underlying,
                depth: depth + 1,
                visitedErrors: &visitedErrors
            )
        }

        let failure = error as NSError
        guard visitedErrors.insert(ObjectIdentifier(failure)).inserted else {
            return nil
        }
        // Redirect failures can wrap several URL errors. Prefer the deepest
        // failing URL because it names the actual redirect destination rather
        // than the already committed page or the redirect source.
        if let underlying = failure.userInfo[NSUnderlyingErrorKey] as? Error,
           let nestedURL = validatedNavigationFailureURL(
               underlying,
               depth: depth + 1,
               visitedErrors: &visitedErrors
           ) {
            return nestedURL
        }
        let candidate = (error as? URLError)?.failingURL ??
            (failure.userInfo[NSURLErrorFailingURLErrorKey] as? URL)
        guard let candidate else { return nil }
        return try? MobileBrowserInputRouter.validateWebURL(candidate)
    }

    private static func isNavigationCancellation(
        _ error: Error,
        depth: Int,
        visitedErrors: inout Set<ObjectIdentifier>
    ) -> Bool {
        guard depth < maximumUnderlyingNavigationFailureDepth else { return false }
        if let navigationError = error as? WebPage.NavigationError,
           case .failedProvisionalNavigation(let underlying) = navigationError {
            return isNavigationCancellation(
                underlying,
                depth: depth + 1,
                visitedErrors: &visitedErrors
            )
        }
        let failure = error as NSError
        guard visitedErrors.insert(ObjectIdentifier(failure)).inserted else {
            return false
        }
        if failure.domain == NSURLErrorDomain,
           failure.code == NSURLErrorCancelled {
            return true
        }
        guard let underlying = failure.userInfo[NSUnderlyingErrorKey] as? Error else {
            return false
        }
        return isNavigationCancellation(
            underlying,
            depth: depth + 1,
            visitedErrors: &visitedErrors
        )
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

}
