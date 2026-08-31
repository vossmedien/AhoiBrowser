import Combine
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
        XCTAssertGreaterThanOrEqual(
            MobileBrowserChromeTheme.reducedMotionCrossfadeDuration,
            0.18
        )
        XCTAssertLessThanOrEqual(
            MobileBrowserChromeTheme.reducedMotionCrossfadeDuration,
            0.22
        )
        XCTAssertNil(MobileBrowserChromeTheme.chromeAnimation(reduceMotion: true))
    }

    func testChromeCollapseAccumulatesIntentionalScrollAndReversesQuickly() {
        var policy = MobileChromeVisibilityPolicy()
        var collapsed = false

        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 40,
            contentOffset: 54,
            pullDistance: 0
        )
        XCTAssertFalse(collapsed)
        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 54,
            contentOffset: 68,
            pullDistance: 0
        )
        XCTAssertTrue(collapsed)

        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 68,
            contentOffset: 54,
            pullDistance: 0
        )
        XCTAssertFalse(collapsed)
    }

    func testChromeCollapseDoesNotTreatViewportSettlingAsThePageTop() {
        var policy = MobileChromeVisibilityPolicy()
        var collapsed = false

        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 40,
            contentOffset: 68,
            pullDistance: 0
        )
        XCTAssertTrue(collapsed)
        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 68,
            contentOffset: 0,
            pullDistance: 0,
            layoutIsStable: false
        )
        XCTAssertTrue(collapsed)
        collapsed = policy.nextCollapsedState(
            currentlyCollapsed: collapsed,
            previousContentOffset: 0,
            contentOffset: 0,
            pullDistance: 0
        )
        XCTAssertTrue(collapsed)
    }

    func testChromeCollapseIgnoresLayoutFeedbackAndNormalizesLargeScrollSamples() {
        var policy = MobileChromeVisibilityPolicy()

        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: false,
            previousContentOffset: 40,
            contentOffset: 60,
            pullDistance: 0
        ))
        XCTAssertEqual(policy.accumulatedTravel, 20)

        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: false,
            previousContentOffset: 60,
            contentOffset: 42,
            pullDistance: 0,
            layoutIsStable: false
        ))
        XCTAssertEqual(policy.accumulatedTravel, 0)

        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: false,
            previousContentOffset: 42,
            contentOffset: 42.25,
            pullDistance: 0
        ))
        XCTAssertTrue(policy.nextCollapsedState(
            currentlyCollapsed: false,
            previousContentOffset: 42.25,
            contentOffset: 200,
            pullDistance: 0
        ))
        XCTAssertEqual(policy.accumulatedTravel, 0)
    }

    func testChromeCollapseResetDropsPartialScrollIntent() {
        var policy = MobileChromeVisibilityPolicy()

        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: false,
            previousContentOffset: 40,
            contentOffset: 60,
            pullDistance: 0
        ))
        policy.reset()
        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: false,
            previousContentOffset: 60,
            contentOffset: 70,
            pullDistance: 0
        ))
        XCTAssertEqual(policy.accumulatedTravel, 10)
    }

    func testChromeCollapseExpandsAfterReverseTravelAtTopAndDuringPullToRefresh() {
        var policy = MobileChromeVisibilityPolicy()

        XCTAssertTrue(policy.nextCollapsedState(
            currentlyCollapsed: true,
            previousContentOffset: 40,
            contentOffset: 31,
            pullDistance: 0
        ))
        XCTAssertFalse(policy.nextCollapsedState(
            currentlyCollapsed: true,
            previousContentOffset: 31,
            contentOffset: 25,
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

    func testPageScrollEventValidatesAndClampsNumericLayoutTelemetry() throws {
        let event = try XCTUnwrap(MobilePageScrollEvent(
            messageBody: [
                "offsetY": NSNumber(value: 500),
                "sourceID": NSNumber(value: 0),
                "contentHeight": NSNumber(value: 900),
                "viewportHeight": NSNumber(value: 600),
            ],
            sequence: 7
        ))

        XCTAssertEqual(event.sequence, 7)
        XCTAssertEqual(event.sourceID, 0)
        XCTAssertEqual(event.contentOffsetY, 300)
        XCTAssertEqual(event.contentHeight, 900)
        XCTAssertEqual(event.viewportHeight, 600)
    }

    func testPageScrollEventAcceptsNumericZeroAndOneButRejectsBooleans() throws {
        let numericZeroAndOne = [
            NSNumber(value: 0),
            NSNumber(value: 1),
            NSNumber(value: 0.0),
            NSNumber(value: 1.0),
        ]
        for sourceID in numericZeroAndOne {
            let event = try XCTUnwrap(MobilePageScrollEvent(
                messageBody: [
                    "offsetY": NSNumber(value: 1),
                    "sourceID": sourceID,
                    "contentHeight": NSNumber(value: 2),
                    "viewportHeight": NSNumber(value: 1),
                ],
                sequence: 1
            ))
            XCTAssertEqual(event.contentOffsetY, 1)
            XCTAssertEqual(event.sourceID, UInt32(sourceID.doubleValue))
        }

        for boolean in [false, true] {
            XCTAssertNil(MobilePageScrollEvent(
                messageBody: [
                    "offsetY": NSNumber(value: 1),
                    "sourceID": boolean,
                    "contentHeight": NSNumber(value: 2),
                    "viewportHeight": NSNumber(value: 1),
                ],
                sequence: 1
            ))
        }
    }

    func testPageScrollEventRejectsMalformedOrUnboundedTelemetry() {
        let valid: [String: Any] = [
            "offsetY": NSNumber(value: 20),
            "contentHeight": NSNumber(value: 900),
            "viewportHeight": NSNumber(value: 600),
        ]

        for invalidBody in [
            valid.merging(["offsetY": true]) { _, new in new },
            valid.merging(["offsetY": NSNumber(value: -1)]) { _, new in new },
            valid.merging(["offsetY": NSNumber(value: Double.nan)]) { _, new in new },
            valid.merging(["offsetY": NSNumber(value: Double.infinity)]) { _, new in new },
            valid.merging(["offsetY": "20"]) { _, new in new },
            valid.merging(["contentHeight": NSNumber(value: 10_000_001)]) { _, new in new },
            valid.merging(["viewportHeight": NSNumber(value: 0)]) { _, new in new },
            valid.merging(["sourceID": NSNumber(value: -1)]) { _, new in new },
            valid.merging(["sourceID": NSNumber(value: 1.25)]) { _, new in new },
            valid.merging(["sourceID": NSNumber(value: 1_000_001)]) { _, new in new },
            ["offsetY": NSNumber(value: 20), "contentHeight": NSNumber(value: 900)],
        ] {
            XCTAssertNil(MobilePageScrollEvent(
                messageBody: invalidBody,
                sequence: 1
            ))
        }
    }

    func testPageScrollEventDistinguishesLayoutChangesFromScrollTravel() throws {
        let baseline = try XCTUnwrap(MobilePageScrollEvent(
            messageBody: [
                "offsetY": NSNumber(value: 40),
                "sourceID": NSNumber(value: 4),
                "contentHeight": NSNumber(value: 1_800),
                "viewportHeight": NSNumber(value: 600),
            ],
            sequence: 1
        ))
        let scrollOnly = try XCTUnwrap(MobilePageScrollEvent(
            messageBody: [
                "offsetY": NSNumber(value: 80),
                "sourceID": NSNumber(value: 4),
                "contentHeight": NSNumber(value: 1_800),
                "viewportHeight": NSNumber(value: 600),
            ],
            sequence: 2
        ))
        let resized = try XCTUnwrap(MobilePageScrollEvent(
            messageBody: [
                "offsetY": NSNumber(value: 80),
                "sourceID": NSNumber(value: 4),
                "contentHeight": NSNumber(value: 1_800),
                "viewportHeight": NSNumber(value: 650),
            ],
            sequence: 3
        ))
        let differentScroller = try XCTUnwrap(MobilePageScrollEvent(
            messageBody: [
                "offsetY": NSNumber(value: 80),
                "sourceID": NSNumber(value: 5),
                "contentHeight": NSNumber(value: 1_800),
                "viewportHeight": NSNumber(value: 600),
            ],
            sequence: 4
        ))

        XCTAssertTrue(scrollOnly.hasStableLayout(comparedTo: baseline))
        XCTAssertFalse(resized.hasStableLayout(comparedTo: scrollOnly))
        XCTAssertFalse(differentScroller.hasStableLayout(comparedTo: scrollOnly))
    }

    func testPageScrollDecoderRejectsSubframesAndOtherMessageKinds() {
        let body: [String: Any] = [
            "kind": "scroll",
            "offsetY": NSNumber(value: 20),
            "contentHeight": NSNumber(value: 900),
            "viewportHeight": NSNumber(value: 600),
        ]

        XCTAssertNotNil(MobilePageScrollMessageDecoder.decode(
            messageBody: body,
            isMainFrame: true,
            sequence: 1
        ))
        XCTAssertNil(MobilePageScrollMessageDecoder.decode(
            messageBody: body,
            isMainFrame: false,
            sequence: 1
        ))
        XCTAssertNil(MobilePageScrollMessageDecoder.decode(
            messageBody: body.merging(["kind": "link"]) { _, new in new },
            isMainFrame: true,
            sequence: 1
        ))
    }

    func testScrollReducerPublishesOnlyOneCollapseAcrossRepeatedFrames() throws {
        let reducer = MobileChromeScrollReducer()
        let baseline = try XCTUnwrap(MobilePageScrollEvent(
            messageBody: [
                "offsetY": NSNumber(value: 40),
                "contentHeight": NSNumber(value: 2_000),
                "viewportHeight": NSNumber(value: 600),
            ],
            sequence: 0
        ))
        reducer.reset(baseline: baseline)
        var collapsed = false
        var transitions = 0

        for sequence in 1...100 {
            let event = try XCTUnwrap(MobilePageScrollEvent(
                messageBody: [
                    "offsetY": NSNumber(value: 40 + sequence * 5),
                    "contentHeight": NSNumber(value: 2_000),
                    "viewportHeight": NSNumber(value: 600),
                ],
                sequence: UInt64(sequence)
            ))
            let next = reducer.nextCollapsedState(
                event: event,
                currentlyCollapsed: collapsed,
                pullDistance: 0,
                suspended: false
            )
            if next != collapsed { transitions += 1 }
            collapsed = next
        }

        XCTAssertTrue(collapsed)
        XCTAssertEqual(transitions, 1)
    }

    @MainActor
    func testInjectedScrollBridgeAcceptsValidatedIsolatedWorldMessage() async throws {
        let browser = MobileBrowserController()
        browser.loadUITestFixture()
        let page = try XCTUnwrap(browser.selectedPage)
        let coordinator = try XCTUnwrap(browser.selectedLinkInteractionCoordinator)
        var events: [MobilePageScrollEvent] = []
        let observation = coordinator.scrollEvents.sink { event in
            events.append(event)
        }
        defer { observation.cancel() }

        var fixtureReady = false
        for _ in 0..<100 {
            let state = try? await page.callJavaScript(
                "return typeof activateNestedScrollFixture === 'function' && " +
                    "document.getElementById('nested-scroll-fixture') !== null;"
            )
            if state as? Bool == true {
                fixtureReady = true
                break
            }
            try await Task.sleep(for: .milliseconds(20))
        }
        XCTAssertTrue(fixtureReady)
        _ = try await page.callJavaScript(
            """
            globalThis.webkit.messageHandlers.ahoiLinkActions.postMessage({
              kind: 'scroll',
              sourceID: 9,
              offsetY: 100,
              contentHeight: 500,
              viewportHeight: 200,
            });
            """,
            contentWorld: MobileLinkInteractionCoordinator.contentWorld
        )
        try await Task.sleep(for: .milliseconds(100))
        let event = try XCTUnwrap(events.last)
        XCTAssertEqual(event.sourceID, 9)
        XCTAssertEqual(event.contentOffsetY, 100)
        XCTAssertEqual(event.contentHeight, 500)
        XCTAssertEqual(event.viewportHeight, 200)
    }

    @MainActor
    func testInjectedScrollBridgePublishesDocumentAndNestedScrollEvents() async throws {
        let browser = MobileBrowserController()
        browser.loadUITestFixture()
        let page = try XCTUnwrap(browser.selectedPage)
        let coordinator = try XCTUnwrap(browser.selectedLinkInteractionCoordinator)
        var events: [MobilePageScrollEvent] = []
        let observation = coordinator.scrollEvents.sink { event in
            events.append(event)
        }
        defer { observation.cancel() }

        var fixtureReady = false
        for _ in 0..<100 {
            let state = try? await page.callJavaScript(
                "return document.readyState === 'complete' && " +
                    "typeof activateNestedScrollFixture === 'function';"
            )
            if state as? Bool == true {
                fixtureReady = true
                break
            }
            try await Task.sleep(for: .milliseconds(20))
        }
        XCTAssertTrue(fixtureReady)

        events.removeAll()
        _ = try await page.callJavaScript(
            "window.scrollTo(0, 240); return window.scrollY;"
        )
        var documentEvent: MobilePageScrollEvent?
        for _ in 0..<100 {
            documentEvent = events.last { event in
                event.sourceID == 0 && event.contentOffsetY >= 100
            }
            if documentEvent != nil { break }
            try await Task.sleep(for: .milliseconds(20))
        }
        XCTAssertNotNil(
            documentEvent,
            "The installed bridge must publish a real document scroll event."
        )

        events.removeAll()
        _ = try await page.callJavaScript(
            """
            activateNestedScrollFixture();
            const scroller = document.getElementById('nested-scroll-fixture');
            scroller.scrollTop = 240;
            scroller.dispatchEvent(new Event('scroll'));
            return scroller.scrollTop;
            """
        )
        var nestedEvent: MobilePageScrollEvent?
        for _ in 0..<100 {
            nestedEvent = events.last { event in
                event.sourceID > 0 && event.contentOffsetY >= 100
            }
            if nestedEvent != nil { break }
            try await Task.sleep(for: .milliseconds(20))
        }
        XCTAssertNotNil(
            nestedEvent,
            "Mutable bridge frame/source state must publish nested-scroller travel."
        )
    }

    func testChromeResetContextExpandsForBrowserErrors() {
        let baseline = chromeResetContext(browserErrorMessage: nil)
        let error = chromeResetContext(browserErrorMessage: "Fixture failure")
        let replacement = chromeResetContext(browserErrorMessage: "Replacement failure")

        XCTAssertTrue(error.requiresExpansion(comparedTo: baseline))
        XCTAssertTrue(replacement.requiresExpansion(comparedTo: error))
        XCTAssertFalse(error.requiresExpansion(comparedTo: error))
        XCTAssertFalse(baseline.requiresExpansion(comparedTo: error))
    }

    func testChromePresentationResetOnlyExpandsForNewRequests() {
        let empty = MobileChromePresentationResetContext(
            javaScriptDialogID: nil,
            fileInputRequestID: nil
        )
        let dialogID = UUID()
        let dialog = MobileChromePresentationResetContext(
            javaScriptDialogID: dialogID,
            fileInputRequestID: nil
        )
        let fileInput = MobileChromePresentationResetContext(
            javaScriptDialogID: nil,
            fileInputRequestID: UUID()
        )

        XCTAssertTrue(dialog.requiresExpansion(comparedTo: empty))
        XCTAssertFalse(empty.requiresExpansion(comparedTo: dialog))
        XCTAssertFalse(dialog.requiresExpansion(comparedTo: dialog))
        XCTAssertTrue(fileInput.requiresExpansion(comparedTo: empty))
    }

    private func chromeResetContext(
        browserErrorMessage: String?
    ) -> MobileChromeResetContext {
        MobileChromeResetContext(
            selectedTabID: nil,
            pageIsLoading: false,
            hasPageFailure: false,
            browserErrorMessage: browserErrorMessage,
            permissionRequestID: nil,
            externalRequestID: nil,
            pendingLinkID: nil,
            findPresented: false,
            addressPresented: false,
            regularWidth: false
        )
    }
}
