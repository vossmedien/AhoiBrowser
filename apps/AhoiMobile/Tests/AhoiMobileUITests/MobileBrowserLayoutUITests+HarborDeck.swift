import XCTest

// Harbor Deck collapse and restore: page and nested scrolling, programmatic
// scrolling, jitter and interactive web presentations (split from
// MobileBrowserLayoutUITests.swift, source line budget).
extension MobileBrowserLayoutUITests {
    @MainActor
    func testHarborDeckCollapsesOnPageScrollAndRestoresOnReverseScroll() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: 3))
        XCTAssertTrue(
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3),
            "Scroll assertions start only after the deterministic document is ready."
        )
        let expandedWebFrame = webView.frame

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        XCTAssertEqual(webView.frame.width, expandedWebFrame.width, accuracy: 1)
        XCTAssertEqual(
            webView.frame.height,
            expandedWebFrame.height,
            accuracy: 1,
            "Chrome motion must not resize the live WebView viewport."
        )
        assertCompactHarborDeckSemantics(app)
        assertReachableHitTarget(app.buttons["browser.address"])
        assertReachableHitTarget(app.buttons["browser.tabs"])
        assertReachableHitTarget(app.buttons["browser.more"])

        webView.swipeUp()
        Thread.sleep(forTimeInterval: 0.5)
        XCTAssertFalse(
            workspace.exists,
            "Bottom bounce and viewport settling must not reopen the Harbor Deck."
        )

        webView.swipeDown()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3))
    }

    @MainActor
    func testProgrammaticPageScrollDoesNotCollapseHarborDeck() throws {
        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture",
            "-AhoiPerformanceWorkload", "scroll",
            "-AhoiPerformanceEvidenceScenario", "scroll-motion-standard",
            "-AhoiPerformanceEvidenceNonce", "layout-scroll-is-not-user-intent",
            "-AhoiPerformanceEvidenceMarker", "ahoi-performance-scroll-motion-standard.json",
            "-AhoiPerformanceReduceMotionOverride", "false",
        ])

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let page = app.webViews.firstMatch.staticTexts["Ahoi fixture page"]
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        // Gate on the committed fixture load first. The page renders promptly,
        // but WebKit's remote accessibility tree can stay unreachable
        // (kAXErrorServerNotFound) for a few seconds after launch while the
        // scripted scroll workload runs; the ~20 s workload outlasts this wait.
        let loadedAddress = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value CONTAINS %@", "fixture.ahoibrowser.test"),
            object: app.buttons["browser.address"]
        )
        XCTAssertEqual(XCTWaiter.wait(for: [loadedAddress], timeout: 8), .completed)
        XCTAssertTrue(page.waitForExistence(timeout: 8))
        Thread.sleep(forTimeInterval: 2.5)
        XCTAssertTrue(
            workspace.exists,
            "Scripted scrollTo travel must not masquerade as a finger gesture."
        )
        // With the full deck over a loaded page `isHittable` is a false
        // signal for the address (79828c0); prove input instead.
        assertBrowserAcceptsAddressInput(in: app)
    }

    @MainActor
    func testHarborDeckTracksNestedScrollerAndRestoresOnReverseScroll() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: 3))

        let activate = webView.buttons["Activate nested scroll fixture"]
        XCTAssertTrue(activate.waitForExistence(timeout: 3))
        activate.tap()

        let nestedScroller = webView.descendants(matching: .any).matching(NSPredicate(
            format: "label BEGINSWITH %@",
            "Nested scroll fixture"
        )).firstMatch
        let startMarker = webView.staticTexts["Nested scroll starts here"]
        XCTAssertTrue(nestedScroller.waitForExistence(timeout: 3))
        XCTAssertTrue(startMarker.waitForExistence(timeout: 3))
        let initialMarkerY = startMarker.frame.minY

        nestedScroller.swipeUp()
        XCTAssertLessThan(
            startMarker.frame.minY,
            initialMarkerY - 24,
            "The gesture must move the nested page region before chrome is evaluated."
        )
        XCTAssertTrue(
            workspace.waitForNonExistence(timeout: 3),
            "A nested page scroller must collapse the Harbor Deck like document scrolling."
        )
        assertCompactHarborDeckSemantics(app)

        nestedScroller.swipeDown()
        XCTAssertTrue(
            workspace.waitForExistence(timeout: 3),
            "Reverse travel inside the nested scroller must restore the full Harbor Deck."
        )
    }

    @MainActor
    func testHarborDeckIgnoresJitterAndExpandsOnIntentionalReverseTravel() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: 3))
        XCTAssertTrue(webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3))

        // The nested fixture is an interactive-control-free production scroll path.
        let activate = webView.buttons["Activate nested scroll fixture"]
        XCTAssertTrue(activate.waitForExistence(timeout: 3))
        activate.tap()
        let scrollSurface = webView.descendants(matching: .any).matching(NSPredicate(
            format: "label BEGINSWITH %@",
            "Nested scroll fixture"
        )).firstMatch
        XCTAssertTrue(scrollSurface.waitForExistence(timeout: 3))

        drag(scrollSurface, fromY: 0.72, toY: 0.52)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        assertCompactHarborDeckSemantics(app)

        // Three deliberately bounded opposite-direction corrections model a
        // settling finger without depending on WebKit's pixel projection.
        // None may flicker the accessibility/control tree open.
        for offset in [0.532, 0.534, 0.536] {
            drag(scrollSurface, fromY: 0.52, toY: offset)
        }
        Thread.sleep(forTimeInterval: 0.35)
        XCTAssertFalse(
            workspace.exists,
            "Sub-threshold reverse travel must keep the compact deck stable."
        )

        drag(scrollSurface, fromY: 0.48, toY: 0.62)
        XCTAssertTrue(
            workspace.waitForExistence(timeout: 3),
            "A deliberate reverse gesture must restore the complete deck."
        )
        XCTAssertEqual(app.buttons.matching(identifier: "browser.address").count, 1)
    }

    @MainActor
    func testInteractiveWebPresentationsExpandCollapsedHarborDeck() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3)
        )

        dragPageUpKeepingFixtureActionsVisible(webView)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        assertCompactHarborDeckSemantics(app)
        let alertButton = webView.buttons["Show JavaScript alert"]
        XCTAssertTrue(alertButton.waitForExistence(timeout: 3))
        XCTAssertTrue(alertButton.isHittable)
        alertButton.tap()

        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 3))
        app.buttons["browser.dialog.accept"].firstMatch.tap()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3),
                      "A JavaScript dialog must leave the full Harbor Deck open.")

        dragPageUpKeepingFixtureActionsVisible(webView)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        assertCompactHarborDeckSemantics(app)
        let fileInput = webView.buttons["Choose a fixture file"]
        XCTAssertTrue(fileInput.waitForExistence(timeout: 3))
        XCTAssertTrue(fileInput.isHittable)
        fileInput.tap()

        let fileInputCancel = app.buttons["browser.file_input.cancel"].firstMatch
        XCTAssertTrue(fileInputCancel.waitForExistence(timeout: 3))
        fileInputCancel.tap()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3),
                      "A file-input request must leave the full Harbor Deck open.")
        // With the full deck over a loaded page `isHittable` is a false
        // signal for the address (79828c0); prove input instead.
        assertBrowserAcceptsAddressInput(in: app)
    }
}
