import Foundation

/// Most-recently-used order for the Harbor deck's tab flick (ADR 0012,
/// MOB-FLICK-01/02). Candidates share the selected tab's browsing mode; normal
/// tabs also share its Workspace. The order is captured when a flick sequence
/// starts and kept while the user keeps flicking, because every selection
/// refreshes `lastActiveAt` and would otherwise reshuffle it.
public struct MobileRecentTabCycler: Equatable, Sendable {
    /// A flick within this interval of the previous one continues the sequence.
    public static let continuationInterval: TimeInterval = 2

    /// Candidate IDs, most recently used first; index 0 is the start tab.
    public private(set) var order: [UUID]
    public private(set) var position = 0
    public private(set) var lastStepAt: Date

    /// Nil when the selected tab has no other candidate to flick to.
    public init?(tabs: [MobileTabRecord], selectedID: UUID, now: Date) {
        guard let selected = tabs.first(where: { $0.id == selectedID }) else { return nil }
        let candidates = Self.candidates(in: tabs, like: selected)
        guard candidates.count > 1 else { return nil }
        order = candidates.sorted {
            $0.lastActiveAt != $1.lastActiveAt
                ? $0.lastActiveAt > $1.lastActiveAt
                : $0.id.uuidString < $1.id.uuidString
        }.map(\.id)
        if let index = order.firstIndex(of: selectedID), index != 0 {
            order.remove(at: index)
            order.insert(selectedID, at: 0)
        }
        lastStepAt = now
    }

    /// Whether a new flick continues this sequence: it comes soon enough, the
    /// selection is still the tab this cycler selected, and no candidate was
    /// opened or closed in between.
    public func isContinuation(tabs: [MobileTabRecord], selectedID: UUID, now: Date) -> Bool {
        guard order[position] == selectedID,
              now.timeIntervalSince(lastStepAt) <= Self.continuationInterval,
              let selected = tabs.first(where: { $0.id == selectedID })
        else { return false }
        return Set(Self.candidates(in: tabs, like: selected).map(\.id)) == Set(order)
    }

    /// Positive steps go to older tabs, negative ones back toward newer; both wrap.
    public mutating func step(_ direction: Int, now: Date) -> UUID? {
        guard let target = peek(direction) else { return nil }
        position = (position + (direction < 0 ? -1 : 1) + order.count) % order.count
        lastStepAt = now
        return target
    }

    /// The tab `step(direction)` would select, without stepping.
    public func peek(_ direction: Int) -> UUID? {
        guard direction != 0 else { return nil }
        let delta = direction < 0 ? -1 : 1
        return order[(position + delta + order.count) % order.count]
    }

    private static func candidates(
        in tabs: [MobileTabRecord],
        like selected: MobileTabRecord
    ) -> [MobileTabRecord] {
        tabs.filter {
            $0.mode == selected.mode &&
                (selected.mode == .privateBrowsing || $0.workspaceID == selected.workspaceID)
        }
    }
}

extension MobileBrowserController {
    /// Switches to the previous (`direction` > 0: older) or next recently used
    /// tab of the current Workspace and mode. Returns false when there is none.
    @discardableResult
    public func switchRecentTab(direction: Int, now: Date = Date()) -> Bool {
        guard direction != 0, let selectedTabID else { return false }
        if recentTabCycler?.isContinuation(tabs: tabs, selectedID: selectedTabID, now: now) != true {
            recentTabCycler = MobileRecentTabCycler(tabs: tabs, selectedID: selectedTabID, now: now)
        }
        guard var cycler = recentTabCycler,
              let target = cycler.step(direction, now: now)
        else { return false }
        recentTabCycler = cycler
        select(target)
        return true
    }

    /// The tab a flick in `direction` would switch to, for the preview the
    /// deck shows while the finger is still down (ADR 0012: the neighbor
    /// page is visible during the drag). Nothing changes here: a running
    /// sequence keeps its captured order, otherwise the order a new
    /// sequence would capture now is used.
    public func recentTabPreview(direction: Int, now: Date = Date()) -> MobileTabRecord? {
        guard direction != 0, let selectedTabID else { return nil }
        let cycler = recentTabCycler?.isContinuation(
            tabs: tabs, selectedID: selectedTabID, now: now
        ) == true
            ? recentTabCycler
            : MobileRecentTabCycler(tabs: tabs, selectedID: selectedTabID, now: now)
        guard let target = cycler?.peek(direction) else { return nil }
        return tabs.first { $0.id == target }
    }
}
