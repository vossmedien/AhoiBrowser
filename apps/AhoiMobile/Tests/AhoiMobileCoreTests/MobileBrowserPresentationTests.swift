import SwiftUI
import XCTest
@testable import AhoiMobileCore

final class MobileBrowserPresentationTests: XCTestCase {
    @MainActor
    func testOpenTabSearchNeverProjectsPrivateMetadata() {
        let browser = MobileBrowserController()
        let normalID = browser.createTab(
            url: URL(string: "https://normal.example/harbor"),
            mode: .normal
        )
        let privateID = browser.createTab(
            url: URL(string: "https://private.example/secret-voyage"),
            mode: .privateBrowsing,
            select: false
        )

        browser.select(normalID)
        XCTAssertEqual(browser.searchOpenTabs("normal").map(\.id), [normalID])
        XCTAssertTrue(browser.searchOpenTabs("secret-voyage").isEmpty)

        browser.select(privateID)
        XCTAssertTrue(
            browser.searchOpenTabs("normal").isEmpty,
            "Open-tab results stay out of the private command projection."
        )
        XCTAssertTrue(browser.searchOpenTabs("secret-voyage").isEmpty)
    }

    func testPrivateFocusVoyageIsConstructedWithoutNormalOrPrivateRecords() {
        let content = MobileFocusVoyageContent.make(
            mode: .privateBrowsing,
            tabs: [
                MobileTabRecord(url: "https://normal.example", mode: .normal),
                MobileTabRecord(url: "https://private.example/secret", mode: .privateBrowsing),
            ],
            snapshot: .empty,
            workspaceID: nil
        )

        XCTAssertEqual(content, .empty)
    }

    func testNormalFocusVoyageIsBoundedDeduplicatedAndPrivateFree() {
        let tabs = (0..<6).map { index in
            MobileTabRecord(
                title: "Normal \(index)",
                url: "https://normal\(index).example",
                lastActiveAt: Date(timeIntervalSince1970: TimeInterval(index)),
                mode: .normal
            )
        } + [
            MobileTabRecord(
                title: "Secret",
                url: "https://private.example/secret",
                mode: .privateBrowsing
            ),
            MobileTabRecord(
                title: "Duplicate",
                url: "https://normal5.example",
                mode: .normal
            ),
        ]

        let content = MobileFocusVoyageContent.make(
            mode: .normal,
            tabs: tabs,
            snapshot: .empty,
            workspaceID: nil
        )

        XCTAssertEqual(content.journeyItems.count, 4)
        XCTAssertEqual(Set(content.journeyItems.map(\.url)).count, 4)
        XCTAssertFalse(content.journeyItems.contains {
            $0.url.host() == "private.example"
        })
        XCTAssertTrue(content.savedItems.isEmpty)
    }

    @MainActor
    func testWorkspaceIconPolicyOnlyEmitsResolvedSFSymbols() {
        XCTAssertEqual(
            MobileWorkspaceIconPolicy.systemName(for: "briefcase.fill"),
            "briefcase.fill"
        )
        XCTAssertEqual(
            MobileWorkspaceIconPolicy.systemName(for: "💼"),
            "briefcase.fill"
        )
        XCTAssertEqual(
            MobileWorkspaceIconPolicy.systemName(for: "not a symbol 🚩"),
            MobileWorkspaceIconPolicy.fallbackSystemName
        )
    }

    func testChromeMotionStaysWithinTheApprovedWindow() {
        XCTAssertGreaterThanOrEqual(MobileBrowserChromeTheme.motionDuration, 0.18)
        XCTAssertLessThanOrEqual(MobileBrowserChromeTheme.motionDuration, 0.24)
    }

    func testChromeCollapseAccumulatesIntentionalScrollAndReversesQuickly() {
        var policy = MobileChromeVisibilityPolicy()
        var collapsed = false

        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 40,
            contentOffset: 49,
            pullDistance: 0
        )
        XCTAssertFalse(collapsed)
        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 49,
            contentOffset: 58,
            pullDistance: 0
        )
        XCTAssertTrue(collapsed)

        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 58,
            contentOffset: 46,
            pullDistance: 0
        )
        XCTAssertFalse(collapsed)
    }

    func testChromeCollapseAlwaysExpandsAtTopAndDuringPullToRefresh() {
        var policy = MobileChromeVisibilityPolicy()

        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: true,
            previousContentOffset: 40,
            contentOffset: 31,
            pullDistance: 0
        ))
        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: true,
            previousContentOffset: 40,
            contentOffset: 40,
            pullDistance: 8
        ))
        XCTAssertEqual(policy.accumulatedTravel, 0)
    }
}
