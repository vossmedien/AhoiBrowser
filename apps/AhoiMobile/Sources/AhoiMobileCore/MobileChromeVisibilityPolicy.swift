import CoreGraphics

struct MobileChromeVisibilityPolicy {
    static let collapseTravel: CGFloat = 18
    static let expandTravel: CGFloat = 12
    static let minimumContentOffset: CGFloat = 32

    private(set) var accumulatedTravel: CGFloat = 0

    mutating func nextCollapsedState(
        currentlyCollapsed: Bool,
        previousContentOffset: CGFloat,
        contentOffset: CGFloat,
        pullDistance: CGFloat
    ) -> Bool {
        guard pullDistance == 0,
              contentOffset >= Self.minimumContentOffset else {
            reset()
            return false
        }

        let delta = contentOffset - previousContentOffset
        guard abs(delta) >= 0.5 else { return currentlyCollapsed }

        if accumulatedTravel.sign != delta.sign {
            accumulatedTravel = delta
        } else {
            accumulatedTravel += delta
        }

        if accumulatedTravel >= Self.collapseTravel {
            accumulatedTravel = 0
            return true
        }
        if accumulatedTravel <= -Self.expandTravel {
            accumulatedTravel = 0
            return false
        }
        return currentlyCollapsed
    }

    mutating func reset() {
        accumulatedTravel = 0
    }
}
