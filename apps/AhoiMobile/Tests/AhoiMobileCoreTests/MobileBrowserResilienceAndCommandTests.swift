import CoreGraphics
import XCTest
@testable import AhoiMobileCore

final class MobileBrowserResilienceAndCommandTests: XCTestCase {
    func testOlderRetryCompletionCannotClearNewerFeedbackGeneration() {
        let tabID = UUID()
        var registry = MobilePageRetryFeedbackRegistry()

        let olderGeneration = registry.begin(tabID: tabID)
        let newerGeneration = registry.begin(tabID: tabID)

        XCTAssertNotEqual(olderGeneration, newerGeneration)
        XCTAssertFalse(registry.finish(tabID: tabID, generation: olderGeneration))
        XCTAssertTrue(registry.finish(tabID: tabID, generation: newerGeneration))
        XCTAssertFalse(registry.finish(tabID: tabID, generation: newerGeneration))
    }

    func testCancellingRetryFeedbackInvalidatesPendingCompletion() {
        let tabID = UUID()
        var registry = MobilePageRetryFeedbackRegistry()
        let generation = registry.begin(tabID: tabID)

        registry.cancel(tabID: tabID)

        XCTAssertFalse(registry.finish(tabID: tabID, generation: generation))
    }

    func testHarborWidthHonorsFiniteSplitViewProposal() {
        XCTAssertEqual(
            MobileHarborLayoutPolicy.resolvedWidth(
                proposedWidth: 212,
                naturalWidth: 440
            ),
            212
        )
        XCTAssertEqual(
            MobileHarborLayoutPolicy.resolvedWidth(
                proposedWidth: nil,
                naturalWidth: 440
            ),
            440
        )
    }

    func testHarborControlsWrapWithoutMinimumWidthOverflow() {
        XCTAssertEqual(
            MobileHarborLayoutPolicy.rows(
                itemWidths: [44, 44, 44, 44, 44],
                availableWidth: 144,
                spacing: 6
            ),
            [[0, 1, 2], [3, 4]]
        )
        XCTAssertEqual(
            MobileHarborLayoutPolicy.rows(
                itemWidths: [180, 90, 70],
                availableWidth: 120,
                spacing: 6
            ),
            [[0], [1], [2]]
        )
    }

    func testNumberedTabPolicyUsesNineForLastTabAndRejectsMissingTabs() {
        XCTAssertEqual(
            MobileBrowserCommandTabPolicy.targetIndex(number: 1, tabCount: 12),
            0
        )
        XCTAssertEqual(
            MobileBrowserCommandTabPolicy.targetIndex(number: 8, tabCount: 12),
            7
        )
        XCTAssertEqual(
            MobileBrowserCommandTabPolicy.targetIndex(number: 9, tabCount: 12),
            11
        )
        XCTAssertNil(MobileBrowserCommandTabPolicy.targetIndex(number: 8, tabCount: 7))
        XCTAssertNil(MobileBrowserCommandTabPolicy.targetIndex(number: 0, tabCount: 12))
    }

    @MainActor
    func testTabSwitchingWrapsWithinTheSelectedPrivacyMode() {
        let browser = MobileBrowserController()
        let firstNormal = browser.createTab()
        let secondNormal = browser.createTab()
        let privateTab = browser.createTab(mode: .privateBrowsing)

        browser.select(firstNormal)
        browser.switchSelectedTab(direction: -1)
        XCTAssertEqual(browser.selectedTabID, secondNormal)
        browser.switchSelectedTab(direction: 1)
        XCTAssertEqual(browser.selectedTabID, firstNormal)

        browser.select(privateTab)
        browser.switchSelectedTab(direction: 1)
        XCTAssertEqual(browser.selectedTabID, privateTab)
    }

    @MainActor
    func testNumberedCommandDispatchesOneAuthoritativeSelection() {
        var selections: [Int] = []
        let actions = makeCommandActions(tabCount: 4) {
            selections.append($0)
        }

        actions.selectTab(number: 9)
        actions.selectTab(number: 5)

        XCTAssertEqual(selections, [9])
    }

    @MainActor
    private func makeCommandActions(
        tabCount: Int,
        select: @escaping (Int) -> Void
    ) -> MobileBrowserCommandActions {
        MobileBrowserCommandActions(
            tabCount: tabCount,
            canReopenClosedTab: false,
            canSwitchWorkspace: false,
            canToggleSidebar: false,
            newTab: {},
            newPrivateTab: {},
            reopenClosedTab: {},
            closeSelectedTab: {},
            presentAddress: {},
            presentTabs: {},
            toggleSidebar: {},
            switchWorkspace: { _ in },
            switchTab: { _ in },
            selectNumberedTab: select
        )
    }
}
