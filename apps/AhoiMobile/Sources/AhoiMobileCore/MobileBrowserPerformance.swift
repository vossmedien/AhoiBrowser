import Foundation
import OSLog

public struct MobileBrowserPerformanceBudget: Equatable, Sendable {
    public let maximumLaunchMilliseconds: UInt64
    public let maximumSessionFlushMilliseconds: UInt64
    public let maximumLivePagesForeground: Int
    public let maximumLivePagesBackground: Int
    public let maximumLivePagesAfterMemoryWarning: Int
    public let maximumTransientLivePages: Int

    public init(
        maximumLaunchMilliseconds: UInt64 = 2_500,
        maximumSessionFlushMilliseconds: UInt64 = 1_000,
        maximumLivePagesForeground: Int = 5,
        maximumLivePagesBackground: Int = 2,
        maximumLivePagesAfterMemoryWarning: Int = 1,
        maximumTransientLivePages: Int = 6
    ) {
        self.maximumLaunchMilliseconds = maximumLaunchMilliseconds
        self.maximumSessionFlushMilliseconds = maximumSessionFlushMilliseconds
        self.maximumLivePagesForeground = maximumLivePagesForeground
        self.maximumLivePagesBackground = maximumLivePagesBackground
        self.maximumLivePagesAfterMemoryWarning = maximumLivePagesAfterMemoryWarning
        self.maximumTransientLivePages = maximumTransientLivePages
    }

    public static let releaseDiagnostic = Self()
}

public enum MobileBrowserResourcePressure: String, Equatable, Sendable {
    case foreground
    case background
    case memoryWarning
}

/// Contains counts and monotonic durations only. It deliberately cannot carry
/// URLs, titles, origins, search terms, workspace IDs, or tab IDs.
public struct MobileBrowserPerformanceSnapshot: Equatable, Sendable {
    /// Browser-session readiness, not the operating-system launch duration.
    /// The latter is measured on the exact app bundle with Instruments.
    public internal(set) var launchMilliseconds: UInt64?
    public internal(set) var latestSessionFlushMilliseconds: UInt64?
    public internal(set) var maximumSessionFlushMilliseconds: UInt64 = 0
    public internal(set) var sessionFlushFailureCount: Int = 0
    public internal(set) var normalTabCount: Int = 0
    public internal(set) var privateTabCount: Int = 0
    public internal(set) var livePageCount: Int = 0
    public internal(set) var maximumObservedTabCount: Int = 0
    public internal(set) var maximumObservedLivePageCount: Int = 0
    public internal(set) var maximumObservedLivePagesForeground: Int = 0
    public internal(set) var maximumObservedLivePagesBackground: Int = 0
    public internal(set) var maximumObservedLivePagesAfterMemoryWarning: Int = 0
    public internal(set) var discardedPageCount: Int = 0
    public internal(set) var latestPressure: MobileBrowserResourcePressure = .foreground

    public init() {}

    public var totalTabCount: Int { normalTabCount + privateTabCount }
}

public enum MobileBrowserPerformanceViolation: Equatable, Sendable {
    case launch(actualMilliseconds: UInt64, maximumMilliseconds: UInt64)
    case sessionFlush(actualMilliseconds: UInt64, maximumMilliseconds: UInt64)
    case livePages(actual: Int, maximum: Int, pressure: MobileBrowserResourcePressure)
    case transientLivePages(
        actual: Int,
        maximum: Int,
        pressure: MobileBrowserResourcePressure
    )
    case sessionFlushFailures(count: Int)
}

@MainActor
public final class MobileBrowserPerformanceRecorder {
    public typealias MonotonicClock = @MainActor () -> UInt64

    public private(set) var snapshot = MobileBrowserPerformanceSnapshot()

    private static let logger = Logger(
        subsystem: "app.ahoibrowser.AhoiBrowser",
        category: "MobileBrowserPerformance"
    )
    private let nowNanoseconds: MonotonicClock
    private let launchStartedAt: UInt64
    private var launchCompleted = false
    private var hasReportedBudgetStatus = false
    private var lastReportedViolations: [MobileBrowserPerformanceViolation] = []

    public init(
        nowNanoseconds: @escaping MonotonicClock = {
            DispatchTime.now().uptimeNanoseconds
        }
    ) {
        self.nowNanoseconds = nowNanoseconds
        self.launchStartedAt = nowNanoseconds()
    }

    public func completeLaunch(
        normalTabs: Int,
        privateTabs: Int,
        livePages: Int
    ) {
        recordTabState(
            normalTabs: normalTabs,
            privateTabs: privateTabs,
            livePages: livePages
        )
        guard !launchCompleted else { return }
        launchCompleted = true
        snapshot.launchMilliseconds = elapsedMilliseconds(since: launchStartedAt)
        Self.logger.info(
            "session_ready ms=\(self.snapshot.launchMilliseconds ?? 0, privacy: .public) tabs=\(self.snapshot.totalTabCount, privacy: .public) live_pages=\(self.snapshot.livePageCount, privacy: .public)"
        )
        reportInternalGuardrailStatus()
    }

    public func recordTabState(
        normalTabs: Int,
        privateTabs: Int,
        livePages: Int
    ) {
        snapshot.normalTabCount = max(0, normalTabs)
        snapshot.privateTabCount = max(0, privateTabs)
        snapshot.livePageCount = max(0, livePages)
        snapshot.maximumObservedTabCount = max(
            snapshot.maximumObservedTabCount,
            snapshot.totalTabCount
        )
        snapshot.maximumObservedLivePageCount = max(
            snapshot.maximumObservedLivePageCount,
            snapshot.livePageCount
        )
    }

    public func recordDiscard(
        removedPages: Int,
        pressure: MobileBrowserResourcePressure,
        observedLivePagesBeforeDiscard: Int,
        normalTabs: Int,
        privateTabs: Int,
        livePages: Int
    ) {
        snapshot.discardedPageCount += max(0, removedPages)
        snapshot.latestPressure = pressure
        let observedBeforeDiscard = max(0, observedLivePagesBeforeDiscard)
        snapshot.maximumObservedLivePageCount = max(
            snapshot.maximumObservedLivePageCount,
            observedBeforeDiscard
        )
        switch pressure {
        case .foreground:
            snapshot.maximumObservedLivePagesForeground = max(
                snapshot.maximumObservedLivePagesForeground,
                observedBeforeDiscard
            )
        case .background:
            snapshot.maximumObservedLivePagesBackground = max(
                snapshot.maximumObservedLivePagesBackground,
                observedBeforeDiscard
            )
        case .memoryWarning:
            snapshot.maximumObservedLivePagesAfterMemoryWarning = max(
                snapshot.maximumObservedLivePagesAfterMemoryWarning,
                observedBeforeDiscard
            )
        }
        recordTabState(
            normalTabs: normalTabs,
            privateTabs: privateTabs,
            livePages: livePages
        )
        Self.logger.debug(
            "discard pressure=\(pressure.rawValue, privacy: .public) before=\(observedBeforeDiscard, privacy: .public) removed=\(max(0, removedPages), privacy: .public) live_pages=\(self.snapshot.livePageCount, privacy: .public)"
        )
        reportInternalGuardrailStatus()
    }

    public func beginSessionFlush() -> UInt64 { nowNanoseconds() }

    public func completeSessionFlush(startedAt: UInt64, succeeded: Bool) {
        let milliseconds = elapsedMilliseconds(since: startedAt)
        snapshot.latestSessionFlushMilliseconds = milliseconds
        snapshot.maximumSessionFlushMilliseconds = max(
            snapshot.maximumSessionFlushMilliseconds,
            milliseconds
        )
        if !succeeded { snapshot.sessionFlushFailureCount += 1 }
        Self.logger.info(
            "session_flush ms=\(milliseconds, privacy: .public) succeeded=\(succeeded, privacy: .public)"
        )
        reportInternalGuardrailStatus()
    }

    public func violations(
        budget: MobileBrowserPerformanceBudget = .releaseDiagnostic
    ) -> [MobileBrowserPerformanceViolation] {
        var result: [MobileBrowserPerformanceViolation] = []
        if let launch = snapshot.launchMilliseconds,
           launch > budget.maximumLaunchMilliseconds {
            result.append(.launch(
                actualMilliseconds: launch,
                maximumMilliseconds: budget.maximumLaunchMilliseconds
            ))
        }
        if snapshot.maximumSessionFlushMilliseconds >
            budget.maximumSessionFlushMilliseconds {
            result.append(.sessionFlush(
                actualMilliseconds: snapshot.maximumSessionFlushMilliseconds,
                maximumMilliseconds: budget.maximumSessionFlushMilliseconds
            ))
        }
        if snapshot.sessionFlushFailureCount > 0 {
            result.append(.sessionFlushFailures(
                count: snapshot.sessionFlushFailureCount
            ))
        }
        let maximumLivePages: Int
        switch snapshot.latestPressure {
        case .foreground:
            maximumLivePages = budget.maximumLivePagesForeground
        case .background:
            maximumLivePages = budget.maximumLivePagesBackground
        case .memoryWarning:
            maximumLivePages = budget.maximumLivePagesAfterMemoryWarning
        }
        if snapshot.livePageCount > maximumLivePages {
            result.append(.livePages(
                actual: snapshot.livePageCount,
                maximum: maximumLivePages,
                pressure: snapshot.latestPressure
            ))
        }
        let highWaterByPressure: [(MobileBrowserResourcePressure, Int)] = [
            (.foreground, snapshot.maximumObservedLivePagesForeground),
            (.background, snapshot.maximumObservedLivePagesBackground),
            (.memoryWarning, snapshot.maximumObservedLivePagesAfterMemoryWarning),
        ]
        for (pressure, highWater) in highWaterByPressure
            where highWater > budget.maximumTransientLivePages {
            result.append(.transientLivePages(
                actual: highWater,
                maximum: budget.maximumTransientLivePages,
                pressure: pressure
            ))
        }
        return result
    }

    /// Reports only the in-process safety guardrails represented by this
    /// recorder. It deliberately does not claim that the external Instruments
    /// launch, memory, CPU, network, wakeup, or hitch budgets have passed.
    private func reportInternalGuardrailStatus() {
        let currentViolations = violations()
        guard !hasReportedBudgetStatus ||
            currentViolations != lastReportedViolations else { return }
        hasReportedBudgetStatus = true
        lastReportedViolations = currentViolations
        if currentViolations.isEmpty {
            Self.logger.info("internal_guardrail_status=within_budget")
        } else {
            Self.logger.error(
                "internal_guardrail_status=fail_closed violations=\(currentViolations.count, privacy: .public)"
            )
        }
    }

    private func elapsedMilliseconds(since start: UInt64) -> UInt64 {
        let end = nowNanoseconds()
        guard end >= start else { return 0 }
        return (end - start) / 1_000_000
    }
}
