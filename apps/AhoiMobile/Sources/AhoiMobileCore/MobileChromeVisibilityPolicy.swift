import CoreGraphics
import Foundation

struct MobileChromeResetContext: Equatable {
    let selectedTabID: UUID?
    let pageIsLoading: Bool
    let hasPageFailure: Bool
    let browserErrorMessage: String?
    let permissionRequestID: UUID?
    let externalRequestID: UUID?
    let pendingLinkID: UUID?
    let findPresented: Bool
    let addressPresented: Bool
    let regularWidth: Bool

    func requiresExpansion(comparedTo previous: Self) -> Bool {
        selectedTabID != previous.selectedTabID ||
            (pageIsLoading && !previous.pageIsLoading) ||
            hasPageFailure != previous.hasPageFailure ||
            (browserErrorMessage != nil &&
                browserErrorMessage != previous.browserErrorMessage) ||
            permissionRequestID != previous.permissionRequestID ||
            externalRequestID != previous.externalRequestID ||
            pendingLinkID != previous.pendingLinkID ||
            findPresented != previous.findPresented ||
            addressPresented != previous.addressPresented ||
            regularWidth != previous.regularWidth
    }
}

struct MobileChromePresentationResetContext: Equatable {
    let javaScriptDialogID: UUID?
    let fileInputRequestID: UUID?

    func requiresExpansion(comparedTo previous: Self) -> Bool {
        (javaScriptDialogID != nil &&
            javaScriptDialogID != previous.javaScriptDialogID) ||
            (fileInputRequestID != nil &&
                fileInputRequestID != previous.fileInputRequestID)
    }
}

struct MobileChromeVisibilityPolicy {
    static let collapseTravel: CGFloat = 28
    static let expandTravel: CGFloat = 14
    static let minimumContentOffset: CGFloat = 32
    static let maximumCountedSampleTravel: CGFloat = 120

    private(set) var accumulatedTravel: CGFloat = 0

    mutating func nextCollapsedState(
        currentlyCollapsed: Bool,
        previousContentOffset: CGFloat,
        contentOffset: CGFloat,
        pullDistance: CGFloat,
        layoutIsStable: Bool = true
    ) -> Bool {
        guard pullDistance <= 0.5 else {
            reset()
            return false
        }
        guard layoutIsStable else {
            reset()
            return currentlyCollapsed
        }
        guard currentlyCollapsed || contentOffset >= Self.minimumContentOffset else {
            reset()
            return false
        }

        let delta = contentOffset - previousContentOffset
        guard abs(delta) >= 0.75 else { return currentlyCollapsed }
        let countedDelta = min(
            max(delta, -Self.maximumCountedSampleTravel),
            Self.maximumCountedSampleTravel
        )

        if currentlyCollapsed {
            guard countedDelta < 0 else {
                accumulatedTravel = 0
                return true
            }
            accumulatedTravel = min(0, accumulatedTravel) + countedDelta
            if accumulatedTravel <= -Self.expandTravel {
                reset()
                return false
            }
            return true
        }

        guard countedDelta > 0 else {
            accumulatedTravel = 0
            return false
        }
        accumulatedTravel = max(0, accumulatedTravel) + countedDelta
        if accumulatedTravel >= Self.collapseTravel {
            reset()
            return true
        }
        return false
    }

    mutating func reset() {
        accumulatedTravel = 0
    }
}

final class MobileChromeScrollReducer {
    private var visibilityPolicy = MobileChromeVisibilityPolicy()
    private var previousEvent: MobilePageScrollEvent?

    func reset(baseline: MobilePageScrollEvent?) {
        visibilityPolicy.reset()
        previousEvent = baseline
    }

    func invalidateBaseline() {
        reset(baseline: nil)
    }

    func nextCollapsedState(
        event: MobilePageScrollEvent,
        currentlyCollapsed: Bool,
        pullDistance: CGFloat,
        suspended: Bool
    ) -> Bool {
        defer { previousEvent = event }
        guard !suspended, event.isUserInitiated else {
            visibilityPolicy.reset()
            return currentlyCollapsed
        }
        guard let previousEvent,
              previousEvent.isUserInitiated,
              previousEvent.interactionID == event.interactionID else {
            visibilityPolicy.reset()
            return currentlyCollapsed
        }
        return visibilityPolicy.nextCollapsedState(
            currentlyCollapsed: currentlyCollapsed,
            previousContentOffset: CGFloat(previousEvent.contentOffsetY),
            contentOffset: CGFloat(event.contentOffsetY),
            pullDistance: pullDistance,
            layoutIsStable: event.hasStableLayout(comparedTo: previousEvent)
        )
    }
}
