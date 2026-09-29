import XCTest
@testable import AhoiMobileCore

@MainActor
private final class MutableMonotonicClock {
    var value: UInt64

    init(_ value: UInt64) {
        self.value = value
    }

    func advance(by nanoseconds: UInt64) {
        value += nanoseconds
    }
}

final class MobileBrowserPerformanceTests: XCTestCase {
    @MainActor
    func testRecorderPublishesOnlyBoundedCountsAndDurations() {
        let clock = MutableMonotonicClock(10_000_000)
        let recorder = MobileBrowserPerformanceRecorder { clock.value }

        clock.advance(by: 125_000_000)
        recorder.completeLaunch(normalTabs: 20, privateTabs: 20, livePages: 5)
        let flushStart = recorder.beginSessionFlush()
        clock.advance(by: 40_000_000)
        recorder.completeSessionFlush(startedAt: flushStart, succeeded: true)
        recorder.recordDiscard(
            removedPages: 3,
            pressure: .background,
            observedLivePagesBeforeDiscard: 5,
            normalTabs: 20,
            privateTabs: 20,
            livePages: 2
        )

        XCTAssertEqual(recorder.snapshot.launchMilliseconds, 125)
        XCTAssertEqual(recorder.snapshot.latestSessionFlushMilliseconds, 40)
        XCTAssertEqual(recorder.snapshot.maximumObservedTabCount, 40)
        XCTAssertEqual(recorder.snapshot.maximumObservedLivePageCount, 5)
        XCTAssertEqual(recorder.snapshot.discardedPageCount, 3)
        XCTAssertEqual(recorder.snapshot.latestPressure, .background)
        XCTAssertTrue(recorder.violations().isEmpty)
    }

    @MainActor
    func testRecorderFailsClosedWhenBudgetsAreExceeded() {
        let clock = MutableMonotonicClock(0)
        let recorder = MobileBrowserPerformanceRecorder { clock.value }

        clock.value = 3_000_000_000
        recorder.completeLaunch(normalTabs: 1, privateTabs: 0, livePages: 1)
        let flushStart = recorder.beginSessionFlush()
        clock.advance(by: 1_500_000_000)
        recorder.completeSessionFlush(startedAt: flushStart, succeeded: false)
        recorder.recordDiscard(
            removedPages: 0,
            pressure: .memoryWarning,
            observedLivePagesBeforeDiscard: 2,
            normalTabs: 1,
            privateTabs: 0,
            livePages: 2
        )

        XCTAssertEqual(recorder.violations(), [
            .launch(actualMilliseconds: 3_000, maximumMilliseconds: 2_500),
            .sessionFlush(actualMilliseconds: 1_500, maximumMilliseconds: 1_000),
            .sessionFlushFailures(count: 1),
            .livePages(actual: 2, maximum: 1, pressure: .memoryWarning),
        ])
    }

    @MainActor
    func testLaunchAndFlushCompletionAreIdempotentAndMonotonic() {
        let clock = MutableMonotonicClock(500_000_000)
        let recorder = MobileBrowserPerformanceRecorder { clock.value }

        clock.advance(by: 50_000_000)
        recorder.completeLaunch(normalTabs: 1, privateTabs: 0, livePages: 1)
        clock.advance(by: 5_000_000_000)
        recorder.completeLaunch(normalTabs: 5, privateTabs: 0, livePages: 5)

        XCTAssertEqual(recorder.snapshot.launchMilliseconds, 50)
        XCTAssertEqual(recorder.snapshot.maximumObservedTabCount, 5)
        XCTAssertEqual(recorder.snapshot.maximumObservedLivePageCount, 5)
    }

    @MainActor
    func testPreTrimHighWaterRemainsALatchedViolation() {
        let recorder = MobileBrowserPerformanceRecorder { 0 }

        recorder.recordDiscard(
            removedPages: 15,
            pressure: .foreground,
            observedLivePagesBeforeDiscard: 20,
            normalTabs: 20,
            privateTabs: 0,
            livePages: 5
        )
        recorder.recordDiscard(
            removedPages: 4,
            pressure: .memoryWarning,
            observedLivePagesBeforeDiscard: 5,
            normalTabs: 20,
            privateTabs: 0,
            livePages: 1
        )

        XCTAssertEqual(recorder.snapshot.livePageCount, 1)
        XCTAssertEqual(recorder.snapshot.maximumObservedLivePagesForeground, 20)
        XCTAssertEqual(recorder.snapshot.maximumObservedLivePagesAfterMemoryWarning, 5)
        XCTAssertEqual(recorder.violations(), [
            .transientLivePages(
                actual: 20,
                maximum: 6,
                pressure: .foreground
            ),
        ])
    }
}
