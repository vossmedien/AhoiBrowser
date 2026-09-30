import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// ADR 0012, MOB-FLICK-01/02: flick through recently used tabs.
final class MobileRecentTabCyclerTests: XCTestCase {
    private let start = Date(timeIntervalSince1970: 1_000)
    private let work = WorkspaceID(rawValue: UUID())
    private let home = WorkspaceID(rawValue: UUID())

    private func tab(
        _ age: TimeInterval,
        workspace: WorkspaceID? = nil,
        mode: MobileBrowsingMode = .normal
    ) -> MobileTabRecord {
        MobileTabRecord(
            workspaceID: mode == .normal ? (workspace ?? work) : nil,
            lastActiveAt: start.addingTimeInterval(-age),
            mode: mode
        )
    }

    func testStepsFollowMostRecentUseAndWrap() throws {
        let current = tab(0), previous = tab(10), oldest = tab(20)
        var cycler = try XCTUnwrap(MobileRecentTabCycler(
            tabs: [oldest, current, previous], selectedID: current.id, now: start))
        XCTAssertEqual(cycler.order, [current.id, previous.id, oldest.id])
        XCTAssertEqual(cycler.step(1, now: start), previous.id)
        XCTAssertEqual(cycler.step(1, now: start), oldest.id)
        XCTAssertEqual(cycler.step(1, now: start), current.id)
        XCTAssertEqual(cycler.step(-1, now: start), oldest.id)
    }

    func testOrderStaysFixedWhileTheUserKeepsFlicking() throws {
        var current = tab(0), previous = tab(10)
        let oldest = tab(20)
        var cycler = try XCTUnwrap(MobileRecentTabCycler(
            tabs: [current, previous, oldest], selectedID: current.id, now: start))
        XCTAssertEqual(cycler.step(1, now: start), previous.id)
        // Selecting refreshes lastActiveAt; the running sequence ignores that.
        previous.lastActiveAt = start.addingTimeInterval(1)
        current.lastActiveAt = start
        let soon = start.addingTimeInterval(1.5)
        XCTAssertTrue(cycler.isContinuation(
            tabs: [current, previous, oldest], selectedID: previous.id, now: soon))
        XCTAssertEqual(cycler.step(1, now: soon), oldest.id)

        // After a pause, or once another tab is selected, a new sequence starts.
        XCTAssertFalse(cycler.isContinuation(
            tabs: [current, previous, oldest], selectedID: oldest.id,
            now: soon.addingTimeInterval(MobileRecentTabCycler.continuationInterval + 1)))
        XCTAssertFalse(cycler.isContinuation(
            tabs: [current, previous, oldest], selectedID: current.id, now: soon))
        // A closed candidate ends the sequence too.
        XCTAssertFalse(cycler.isContinuation(
            tabs: [previous, oldest], selectedID: oldest.id, now: soon))
    }

    func testSingleTabHasNothingToFlick() {
        let only = tab(0)
        XCTAssertNil(MobileRecentTabCycler(tabs: [only, tab(5, workspace: home)],
                                           selectedID: only.id, now: start))
    }

    func testPrivateAndNormalTabsStaySeparate() throws {
        let privateCurrent = tab(0, mode: .privateBrowsing)
        let privatePrevious = tab(30, mode: .privateBrowsing)
        let normal = tab(10)
        let cycler = try XCTUnwrap(MobileRecentTabCycler(
            tabs: [privateCurrent, normal, privatePrevious],
            selectedID: privateCurrent.id, now: start))
        XCTAssertEqual(cycler.order, [privateCurrent.id, privatePrevious.id])
    }

    func testOnlyTabsOfTheCurrentWorkspaceTakePart() throws {
        let current = tab(0), sameWorkspace = tab(30), otherWorkspace = tab(10, workspace: home)
        let cycler = try XCTUnwrap(MobileRecentTabCycler(
            tabs: [current, otherWorkspace, sameWorkspace],
            selectedID: current.id, now: start))
        XCTAssertEqual(cycler.order, [current.id, sameWorkspace.id])
    }

    @MainActor
    func testControllerFlicksBackToThePreviouslyUsedTab() throws {
        let browser = MobileBrowserController()
        let first = browser.createTab()
        let second = browser.createTab()
        _ = browser.createTab(mode: .privateBrowsing)
        browser.select(first)
        browser.select(second)
        // Distinct use times keep the order independent of the clock's resolution.
        for (id, seconds) in [(first, 10.0), (second, 20.0)] {
            let index = try XCTUnwrap(browser.tabs.firstIndex { $0.id == id })
            browser.tabs[index].lastActiveAt = Date(timeIntervalSince1970: seconds)
        }

        XCTAssertTrue(browser.switchRecentTab(direction: 1))
        XCTAssertEqual(browser.selectedTabID, first)
        XCTAssertTrue(browser.switchRecentTab(direction: 1))
        XCTAssertEqual(browser.selectedTabID, second)
    }

    func testPeekNamesTheNextStepWithoutTakingIt() throws {
        let current = tab(0), previous = tab(10), oldest = tab(20)
        var cycler = try XCTUnwrap(MobileRecentTabCycler(
            tabs: [current, previous, oldest], selectedID: current.id, now: start))
        XCTAssertEqual(cycler.peek(1), previous.id)
        XCTAssertEqual(cycler.peek(-1), oldest.id)
        XCTAssertNil(cycler.peek(0))
        XCTAssertEqual(cycler.position, 0)
        XCTAssertEqual(cycler.step(1, now: start), previous.id)
        XCTAssertEqual(cycler.peek(1), oldest.id)
        XCTAssertEqual(cycler.peek(-1), current.id)
    }

    /// The drag preview names the tab the flick will select and changes
    /// nothing, also in the middle of a running flick sequence.
    @MainActor
    func testControllerPreviewMatchesTheFlickAndSelectsNothing() throws {
        let browser = MobileBrowserController()
        let oldest = browser.createTab()
        let previous = browser.createTab()
        let current = browser.createTab()
        browser.select(current)
        for (id, seconds) in [(oldest, 10.0), (previous, 20.0), (current, 30.0)] {
            let index = try XCTUnwrap(browser.tabs.firstIndex { $0.id == id })
            browser.tabs[index].lastActiveAt = Date(timeIntervalSince1970: seconds)
        }

        XCTAssertEqual(browser.recentTabPreview(direction: 1)?.id, previous)
        XCTAssertEqual(browser.recentTabPreview(direction: -1)?.id, oldest)
        XCTAssertNil(browser.recentTabPreview(direction: 0))
        XCTAssertEqual(browser.selectedTabID, current)
        XCTAssertNil(browser.recentTabCycler, "A preview must not start a sequence.")

        XCTAssertTrue(browser.switchRecentTab(direction: 1))
        XCTAssertEqual(browser.selectedTabID, previous)
        // Selecting refreshed `previous`; the running order still leads on.
        XCTAssertEqual(browser.recentTabPreview(direction: 1)?.id, oldest)
        XCTAssertTrue(browser.switchRecentTab(direction: 1))
        XCTAssertEqual(browser.selectedTabID, oldest)
    }

    @MainActor
    func testSingleTabHasNoPreview() {
        let browser = MobileBrowserController()
        let only = browser.createTab()
        browser.select(only)
        XCTAssertNil(browser.recentTabPreview(direction: 1))
        XCTAssertNil(browser.recentTabPreview(direction: -1))
    }

    func testFlickStatePreviewsBeforeItCommits() {
        let record = tab(0)
        XCTAssertNil(MobileRecentTabFlickState.direction(
            of: CGSize(width: 20, height: 0), committing: false))
        XCTAssertEqual(MobileRecentTabFlickState.direction(
            of: CGSize(width: 40, height: 4), committing: false), 1)
        XCTAssertNil(MobileRecentTabFlickState.direction(
            of: CGSize(width: 40, height: 4), committing: true))
        XCTAssertEqual(MobileRecentTabFlickState.direction(
            of: CGSize(width: -90, height: 10), committing: true), -1)
        XCTAssertNil(
            MobileRecentTabFlickState.direction(
                of: CGSize(width: 80, height: 70), committing: false),
            "A mostly vertical drag is not a flick."
        )
        let short = MobileRecentTabFlickState(
            tab: record, direction: 1, translation: CGSize(width: 40, height: 0))
        XCTAssertFalse(short.willCommit)
        let far = MobileRecentTabFlickState(
            tab: record, direction: 1, translation: CGSize(width: 300, height: 0))
        XCTAssertTrue(far.willCommit)
        XCTAssertEqual(far.offset, 40, "The card follows the finger only a little.")
    }
}
