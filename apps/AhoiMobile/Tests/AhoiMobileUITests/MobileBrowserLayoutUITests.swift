import XCTest

final class MobileBrowserLayoutUITests: XCTestCase {
    @MainActor
    func testFocusVoyageAndHarborDeckKeepAllCoreControlsReachable() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.actions.new-tab"].waitForExistence(timeout: 3))
        app.buttons["browser.actions.new-tab"].tap()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.header"]
                .waitForExistence(timeout: 3)
        )
        XCTAssertTrue(app.buttons["browser.focus-voyage.search"].exists)
        XCTAssertTrue(app.buttons["browser.back"].exists)
        XCTAssertTrue(app.buttons["browser.forward"].exists)
        XCTAssertTrue(app.buttons["browser.reload-stop"].exists)
        XCTAssertTrue(app.buttons["browser.tabs"].exists)
        XCTAssertTrue(app.buttons["browser.more"].exists)
    }

    @MainActor
    func testPrivateFocusVoyageDoesNotExposeNormalJourneySections() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.new-private-tab"].waitForExistence(timeout: 3))
        app.buttons["browser.new-private-tab"].tap()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 3)
        )
        XCTAssertTrue(app.buttons["browser.address.private"].exists)
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private-explanation"].exists
        )
        XCTAssertFalse(app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.focus-voyage.item."
        )).firstMatch.exists)
    }

    @MainActor
    func testHarborDeckCollapsesOnPageScrollAndRestoresOnReverseScroll() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: 3))
        XCTAssertTrue(
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3),
            "Scroll assertions start only after the deterministic document is ready."
        )

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
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
    func testHarborDeckTracksNestedScrollerAndRestoresOnReverseScroll() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

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

        nestedScroller.swipeDown()
        XCTAssertTrue(
            workspace.waitForExistence(timeout: 3),
            "Reverse travel inside the nested scroller must restore the full Harbor Deck."
        )
    }

    @MainActor
    func testInteractiveWebPresentationsExpandCollapsedHarborDeck() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3)
        )

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        let alertButton = webView.buttons["Show JavaScript alert"]
        XCTAssertTrue(alertButton.waitForExistence(timeout: 3))
        alertButton.tap()

        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 3))
        app.buttons["browser.dialog.accept"].firstMatch.tap()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3),
                      "A JavaScript dialog must leave the full Harbor Deck open.")

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        let fileInput = webView.buttons["Choose a fixture file"]
        XCTAssertTrue(fileInput.waitForExistence(timeout: 3))
        fileInput.tap()

        let fileInputCancel = app.buttons["browser.file_input.cancel"].firstMatch
        XCTAssertTrue(fileInputCancel.waitForExistence(timeout: 3))
        fileInputCancel.tap()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3),
                      "A file-input request must leave the full Harbor Deck open.")
        assertReachableHitTarget(app.buttons["browser.address"])
    }

    @MainActor
    func testReduceMotionSystemSettingAndHarborDeckJourney() throws {
        let settings = XCUIApplication(bundleIdentifier: "com.apple.Preferences")
        settings.launch()
        let accessibility = settings.staticTexts.matching(NSPredicate(
            format: "label == %@ OR label == %@",
            "Accessibility",
            "Bedienungshilfen"
        )).firstMatch
        XCTAssertTrue(accessibility.waitForExistence(timeout: 8))
        accessibility.tap()

        let motion = settings.staticTexts.matching(NSPredicate(
            format: "label == %@ OR label == %@",
            "Motion",
            "Bewegung"
        )).firstMatch
        for _ in 0..<4 where !motion.exists {
            settings.swipeUp()
        }
        XCTAssertTrue(motion.waitForExistence(timeout: 3))
        motion.tap()

        let reduceMotion = settings.switches.matching(NSPredicate(
            format: "label == %@ OR label == %@",
            "Reduce Motion",
            "Bewegung reduzieren"
        )).firstMatch
        XCTAssertTrue(reduceMotion.waitForExistence(timeout: 3))
        let wasEnabled = (reduceMotion.value as? String) == "1"
        if !wasEnabled {
            tapSwitchControl(reduceMotion)
            let enabled = XCTNSPredicateExpectation(
                predicate: NSPredicate(format: "value == %@", "1"),
                object: reduceMotion
            )
            XCTAssertEqual(XCTWaiter.wait(for: [enabled], timeout: 3), .completed)
        }
        defer {
            if !wasEnabled {
                settings.activate()
                if (reduceMotion.value as? String) == "1" {
                    tapSwitchControl(reduceMotion)
                }
                let restored = XCTNSPredicateExpectation(
                    predicate: NSPredicate(format: "value == %@", "0"),
                    object: reduceMotion
                )
                XCTAssertEqual(
                    XCTWaiter.wait(for: [restored], timeout: 3),
                    .completed,
                    "The E2E journey must restore the simulator accessibility setting."
                )
            }
        }
        XCTAssertEqual(
            reduceMotion.value as? String,
            "1",
            "The E2E journey must start from the visibly enabled system setting."
        )

        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()
        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3))

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        assertReachableHitTarget(app.buttons["browser.address"])
        assertReachableHitTarget(app.buttons["browser.tabs"])
        assertReachableHitTarget(app.buttons["browser.more"])

        webView.swipeDown()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3))
    }

    @MainActor
    private func tapSwitchControl(_ element: XCUIElement) {
        // Settings exposes the whole switch row as one accessibility element.
        // Tapping its center can hit the label without toggling the control, so
        // drive the visible trailing switch thumb explicitly.
        element.coordinate(
            withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)
        ).tap()
    }

    @MainActor
    private func assertReachableHitTarget(
        _ element: XCUIElement,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertTrue(element.waitForExistence(timeout: 3), file: file, line: line)
        let hittable = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "hittable == true"),
            object: element
        )
        XCTAssertEqual(
            XCTWaiter.wait(for: [hittable], timeout: 3),
            .completed,
            "Collapsed Harbor Deck controls must remain tappable.",
            file: file,
            line: line
        )
        XCTAssertGreaterThanOrEqual(element.frame.width, 44, file: file, line: line)
        XCTAssertGreaterThanOrEqual(element.frame.height, 44, file: file, line: line)
    }

    @MainActor
    func testWorkspaceCanvasExposesPersistentCommandSidebarOnIPad() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()
        try XCTSkipIf(app.frame.width < 700, "Regular-width Workspace Canvas is iPad-only.")

        XCTAssertTrue(app.buttons["browser.sidebar.command"].waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["browser.sidebar.new-tab"].exists)
    }
}
