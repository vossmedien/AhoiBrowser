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
        XCTAssertTrue(webView.waitForExistence(timeout: stateTimeout))
        XCTAssertTrue(
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: stateTimeout),
            "Scroll assertions start only after the deterministic document is ready."
        )
        let expandedWebFrame = webView.frame

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: stateTimeout))
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

        let heading = webView.staticTexts["Ahoi fixture page"]
        webView.swipeUp()
        // Evaluate only once the bounce has settled: the page no longer moves.
        XCTAssertTrue(
            waitForStableFrame(of: heading, timeout: stateTimeout),
            "The page must come to rest after the bottom bounce."
        )
        XCTAssertFalse(
            workspace.exists,
            "Bottom bounce and viewport settling must not reopen the Harbor Deck."
        )

        webView.swipeDown()
        XCTAssertTrue(workspace.waitForExistence(timeout: stateTimeout))
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
        // Wait for the workload's scripted travel itself instead of a fixed
        // delay: the heading must leave its top position and come back (one
        // scrollTo down and one up), and the deck must stay at every sample.
        let topY = page.frame.minY
        var travelledDown = false
        var returnedUp = false
        var deckAlwaysPresent = true
        _ = waitUntil(timeout: 20) {
            deckAlwaysPresent = deckAlwaysPresent && workspace.exists
            let y = page.frame.minY
            if y < topY - 100 { travelledDown = true }
            if travelledDown, abs(y - topY) < 2 { returnedUp = true }
            return returnedUp || !deckAlwaysPresent
        }
        XCTAssertTrue(travelledDown, "The scripted workload must scroll the page down.")
        XCTAssertTrue(returnedUp, "The scripted workload must scroll the page back up.")
        XCTAssertTrue(
            deckAlwaysPresent && workspace.exists,
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
        XCTAssertTrue(webView.waitForExistence(timeout: stateTimeout))

        let activate = webView.buttons["Activate nested scroll fixture"]
        XCTAssertTrue(activate.waitForExistence(timeout: stateTimeout))
        activate.tap()

        let nestedScroller = webView.descendants(matching: .any).matching(NSPredicate(
            format: "label BEGINSWITH %@",
            "Nested scroll fixture"
        )).firstMatch
        let startMarker = webView.staticTexts["Nested scroll starts here"]
        XCTAssertTrue(nestedScroller.waitForExistence(timeout: stateTimeout))
        XCTAssertTrue(startMarker.waitForExistence(timeout: stateTimeout))
        let initialMarkerY = startMarker.frame.minY

        nestedScroller.swipeUp()
        XCTAssertTrue(
            waitUntil(timeout: stateTimeout) { startMarker.frame.minY < initialMarkerY - 24 },
            "The gesture must move the nested page region before chrome is evaluated."
        )
        XCTAssertTrue(
            workspace.waitForNonExistence(timeout: stateTimeout),
            "A nested page scroller must collapse the Harbor Deck like document scrolling."
        )
        assertCompactHarborDeckSemantics(app)

        nestedScroller.swipeDown()
        XCTAssertTrue(
            workspace.waitForExistence(timeout: stateTimeout),
            "Reverse travel inside the nested scroller must restore the full Harbor Deck."
        )
    }

    @MainActor
    func testHarborDeckIgnoresJitterAndExpandsOnIntentionalReverseTravel() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: stateTimeout))
        XCTAssertTrue(webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: stateTimeout))

        // The nested fixture is an interactive-control-free production scroll path.
        let activate = webView.buttons["Activate nested scroll fixture"]
        XCTAssertTrue(activate.waitForExistence(timeout: stateTimeout))
        activate.tap()
        let scrollSurface = webView.descendants(matching: .any).matching(NSPredicate(
            format: "label BEGINSWITH %@",
            "Nested scroll fixture"
        )).firstMatch
        XCTAssertTrue(scrollSurface.waitForExistence(timeout: stateTimeout))

        drag(scrollSurface, fromY: 0.72, toY: 0.52)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: stateTimeout))
        assertCompactHarborDeckSemantics(app)

        // Three deliberately bounded opposite-direction corrections model a
        // settling finger without depending on WebKit's pixel projection.
        // None may flicker the accessibility/control tree open.
        for offset in [0.532, 0.534, 0.536] {
            drag(scrollSurface, fromY: 0.52, toY: offset)
        }
        XCTAssertTrue(
            waitForStableFrame(
                of: webView.staticTexts["Nested scroll starts here"],
                timeout: stateTimeout
            ),
            "The nested scroller must come to rest before the deck is evaluated."
        )
        XCTAssertFalse(
            workspace.exists,
            "Sub-threshold reverse travel must keep the compact deck stable."
        )

        drag(scrollSurface, fromY: 0.48, toY: 0.62)
        XCTAssertTrue(
            workspace.waitForExistence(timeout: stateTimeout),
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
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: stateTimeout)
        )

        dragPageUpKeepingFixtureActionsVisible(webView)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: stateTimeout))
        assertCompactHarborDeckSemantics(app)
        let alertButton = webView.buttons["Show JavaScript alert"]
        XCTAssertTrue(waitForHittable(alertButton, timeout: stateTimeout))
        alertButton.tap()

        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: stateTimeout))
        let accept = alert.buttons["browser.dialog.accept"].firstMatch
        XCTAssertTrue(waitForHittable(accept, timeout: stateTimeout))
        accept.tap()
        XCTAssertTrue(alert.waitForNonExistence(timeout: stateTimeout))
        XCTAssertTrue(workspace.waitForExistence(timeout: stateTimeout),
                      "A JavaScript dialog must leave the full Harbor Deck open.")

        dragPageUpKeepingFixtureActionsVisible(webView)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: stateTimeout))
        assertCompactHarborDeckSemantics(app)
        let fileInput = webView.buttons["Choose a fixture file"]
        XCTAssertTrue(waitForHittable(fileInput, timeout: stateTimeout))
        fileInput.tap()

        let fileInputCancel = app.buttons["browser.file_input.cancel"].firstMatch
        XCTAssertTrue(waitForHittable(fileInputCancel, timeout: stateTimeout))
        fileInputCancel.tap()
        XCTAssertTrue(workspace.waitForExistence(timeout: stateTimeout),
                      "A file-input request must leave the full Harbor Deck open.")
        // With the full deck over a loaded page `isHittable` is a false
        // signal for the address (79828c0); prove input instead.
        assertBrowserAcceptsAddressInput(in: app)
    }

    /// True once two consecutive samples 0.3 s apart report the same frame,
    /// i.e. scrolling and deck animation have come to rest.
    @MainActor
    func waitForStableFrame(of element: XCUIElement, timeout: TimeInterval) -> Bool {
        var previous = element.frame
        return waitUntil(timeout: timeout) {
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
            let current = element.frame
            defer { previous = current }
            return current == previous && !current.isEmpty
        }
    }
}
