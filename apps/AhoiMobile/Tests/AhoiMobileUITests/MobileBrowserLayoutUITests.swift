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
        assertCompactHarborDeckSemantics(app)

        nestedScroller.swipeDown()
        XCTAssertTrue(
            workspace.waitForExistence(timeout: 3),
            "Reverse travel inside the nested scroller must restore the full Harbor Deck."
        )
    }

    @MainActor
    func testHarborDeckIgnoresJitterAndExpandsOnIntentionalReverseTravel() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: 3))
        XCTAssertTrue(webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3))

        drag(webView, fromY: 0.72, toY: 0.52)
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        assertCompactHarborDeckSemantics(app)

        // A tiny opposite-direction correction is common while a finger is
        // settling. It must move the document, yet remain below the 14-point
        // expand threshold so the deck cannot flicker open.
        let marker = webView.staticTexts["Ahoi fixture page"]
        let markerYBeforeJitter = marker.frame.minY
        drag(webView, fromY: 0.52, toY: 0.537)
        Thread.sleep(forTimeInterval: 0.35)
        let jitterTravel = marker.frame.minY - markerYBeforeJitter
        XCTAssertGreaterThan(
            jitterTravel,
            1,
            "The reverse correction must move the visible document rather than pass as touch slop."
        )
        XCTAssertLessThan(
            jitterTravel,
            14,
            "The fixture correction must stay below the production expand threshold."
        )
        XCTAssertFalse(
            workspace.exists,
            "Sub-threshold reverse travel must keep the compact deck stable."
        )

        drag(webView, fromY: 0.48, toY: 0.62)
        XCTAssertTrue(
            workspace.waitForExistence(timeout: 3),
            "A deliberate reverse gesture must restore the complete deck."
        )
        XCTAssertEqual(app.buttons.matching(identifier: "browser.address").count, 1)
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
        assertReachableHitTarget(app.buttons["browser.address"])
    }

    @MainActor
    func testReduceMotionSystemSettingAndHarborDeckJourney() throws {
        try XCTSkipIf(
            ProcessInfo.processInfo.environment["SIMULATOR_UDID"] == nil,
            "Automating the system-wide Reduce Motion setting is a simulator-only journey."
        )
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
        assertCompactHarborDeckSemantics(app)
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
    private func dragPageUpKeepingFixtureActionsVisible(_ webView: XCUIElement) {
        // A full XCUI swipe scales with the destination and can move the
        // dialog/file controls outside WebKit's physical-device accessibility
        // projection. This bounded drag still exceeds the 28-point chrome
        // threshold while retaining the controls as visible tap targets.
        let start = webView.coordinate(
            withNormalizedOffset: CGVector(dx: 0.5, dy: 0.68)
        )
        let end = webView.coordinate(
            withNormalizedOffset: CGVector(dx: 0.5, dy: 0.54)
        )
        start.press(forDuration: 0.05, thenDragTo: end)
    }

    @MainActor
    private func drag(
        _ element: XCUIElement,
        fromY: CGFloat,
        toY: CGFloat
    ) {
        let start = element.coordinate(
            withNormalizedOffset: CGVector(dx: 0.5, dy: fromY)
        )
        let end = element.coordinate(
            withNormalizedOffset: CGVector(dx: 0.5, dy: toY)
        )
        start.press(forDuration: 0.08, thenDragTo: end)
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
    private func assertCompactHarborDeckSemantics(
        _ app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertEqual(
            app.descendants(matching: .any)
                .matching(identifier: "browser.harbor-deck.workspace").count,
            0,
            "Compact chrome must remove the workspace rail from accessibility.",
            file: file,
            line: line
        )
        XCTAssertEqual(
            app.buttons.matching(identifier: "browser.forward").count,
            0,
            "Compact chrome must not project its hidden forward control.",
            file: file,
            line: line
        )
        XCTAssertEqual(
            app.buttons.matching(identifier: "browser.reload-stop").count,
            0,
            "Compact chrome must not project its hidden reload control.",
            file: file,
            line: line
        )
        for identifier in ["browser.address", "browser.tabs", "browser.more"] {
            XCTAssertEqual(
                app.buttons.matching(identifier: identifier).count,
                1,
                "Compact chrome must retain one stable \(identifier) control.",
                file: file,
                line: line
            )
        }
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
