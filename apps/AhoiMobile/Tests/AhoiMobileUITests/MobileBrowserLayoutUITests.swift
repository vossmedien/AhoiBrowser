import XCTest

final class MobileBrowserLayoutUITests: MobileBrowserUITestCase {
    // INTEGRATION ONLY: these two scale checks use launch seams to create
    // deterministic tab populations. They are not visible E2E evidence.
    @MainActor
    func testIntegrationScaleFixtureOneFiveTwentyNormalTabsKeepChromeReachable() throws {
        for count in [1, 5, 20] {
            let app = launchExactCandidate(arguments: [
                "-AhoiUITestFixture",
                "-AhoiUITestNormalTabCount", "\(count)",
            ])

            let tabs = app.buttons["browser.tabs"]
            XCTAssertTrue(tabs.waitForExistence(timeout: 8))
            XCTAssertTrue(
                waitForTabCount(count, in: tabs, timeout: 3),
                "Fixture \(count): tabs label '\(tabs.label)' value '\(String(describing: tabs.value))'"
            )
            tabs.tap()
            XCTAssertTrue(
                app.descendants(matching: .any)["browser.tabs.mode"]
                    .waitForExistence(timeout: 3)
            )
            XCTAssertTrue(app.buttons["browser.tabs.done"].isHittable)
            app.buttons["browser.tabs.done"].tap()
        }
    }

    @MainActor
    func testIntegrationScaleFixtureTwentyPrivateTabsKeepChromeReachable() throws {
        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture",
            "-AhoiUITestPrivateTabCount", "20",
            "-AhoiUITestSelectPrivate",
        ])

        let privateAddress = app.buttons["browser.address.private"]
        XCTAssertTrue(privateAddress.waitForExistence(timeout: 8))
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForTabCount(20, in: tabs, timeout: 3))
        XCTAssertTrue(app.buttons["browser.more"].isHittable)
    }

    @MainActor
    func testVisibleNormalTabCreationReachesOneFiveTwentyMilestones() throws {
        let app = coldLaunchApplication()

        normalizeToFreshNormalTab(in: app)
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 8))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
        for count in 2...20 {
            XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 3))
            app.buttons["browser.more"].tap()
            let newTab = app.buttons["browser.actions.new-tab"]
            XCTAssertTrue(newTab.waitForExistence(timeout: 3))
            newTab.tap()
            if count == 5 || count == 20 {
                XCTAssertTrue(waitForTabCount(count, in: tabs, timeout: 3))
            }
        }
        // `isHittable` is a false signal for the address: the web view's
        // accessibility frame reaches under the bottom deck (79828c0).
        assertBrowserAcceptsAddressInput(in: app)
        XCTAssertTrue(app.buttons["browser.more"].isHittable)

        normalizeToFreshNormalTab(in: app)
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
    }

    @MainActor
    func testVisiblePrivateTabCreationReachesOneFiveTwentyMilestones() throws {
        let app = coldLaunchApplication()

        normalizeToFreshNormalTab(in: app)
        let tabs = app.buttons["browser.tabs"]
        for count in 1...20 {
            XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
            app.buttons["browser.more"].tap()
            let newPrivateTab = app.buttons["browser.new-private-tab"]
            XCTAssertTrue(newPrivateTab.waitForExistence(timeout: 3))
            newPrivateTab.tap()
            XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 3))
            if count == 1 || count == 5 || count == 20 {
                XCTAssertTrue(waitForTabCount(count, in: tabs, timeout: 3))
            }
        }
        XCTAssertTrue(app.buttons["browser.more"].isHittable)

        assertNormalPrivateIsolationAndClearPrivateTabs(
            expectedPrivateCount: 20,
            in: app
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
    }

    @MainActor
    func testFocusVoyageAndHarborDeckKeepAllCoreControlsReachable() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

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
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

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
        let reduceMotionPredicate = NSPredicate(
            format: "label == %@ OR label == %@",
            "Reduce Motion",
            "Bewegung reduzieren"
        )
        let reduceMotion = settings.switches.matching(reduceMotionPredicate).firstMatch
        XCTAssertTrue(reduceMotion.waitForExistence(timeout: 3))
        guard let wasEnabled = MobileUIAcceptanceContract.switchIsOn(reduceMotion) else {
            XCTFail("Settings must expose a Boolean Reduce Motion switch value.")
            return
        }
        if !wasEnabled {
            tapSwitchControl(reduceMotion)
            XCTAssertTrue(
                MobileUIAcceptanceContract.waitForSwitch(
                    reduceMotion,
                    toEqual: true,
                    timeout: 3
                )
            )
        }
        defer {
            if !wasEnabled {
                settings.activate()
                XCTAssertTrue(settings.wait(for: .runningForeground, timeout: 3))
                let restoredControl = settings.switches.matching(reduceMotionPredicate).firstMatch
                XCTAssertTrue(restoredControl.waitForExistence(timeout: 3))
                if MobileUIAcceptanceContract.switchIsOn(restoredControl) == true {
                    tapSwitchControl(restoredControl)
                }
                XCTAssertTrue(
                    MobileUIAcceptanceContract.waitForSwitch(
                        restoredControl,
                        toEqual: false,
                        timeout: 3
                    ),
                    "The E2E journey must restore the simulator accessibility setting."
                )
            }
        }
        XCTAssertEqual(
            MobileUIAcceptanceContract.switchIsOn(reduceMotion),
            true,
            "The E2E journey must start from the visibly enabled system setting."
        )

        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture",
            "-AhoiUITestReduceMotionEvidence",
        ])
        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(
            webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 3),
            "The Reduce Motion journey must use the deterministic local fixture."
        )
        let motionEvidence = app.descendants(matching: .any)["browser.e2e.reduce-motion"]
        XCTAssertTrue(
            motionEvidence.waitForExistence(timeout: 5),
            "The running app must visibly confirm its inherited motion environment."
        )
        XCTAssertEqual(
            motionEvidence.value as? String,
            "enabled",
            "A Settings toggle alone is not proof that this app inherited Reduce Motion."
        )

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
    private func normalizeToFreshNormalTab(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 8))
        tabs.tap()

        let modeControl = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(modeControl.waitForExistence(timeout: 3))
        selectTabSwitcherMode(.normal, using: modeControl)

        let closeButtons = tabCloseButtons(in: app)
        XCTAssertTrue(
            waitForAtLeastOneElement(in: closeButtons, timeout: 3),
            "A loaded product session must expose at least one normal tab."
        )
        closeTabRows(until: 1, in: app)

        let remainingRow = firstHittableElement(in: tabRows(in: app))
        XCTAssertNotNil(remainingRow)
        remainingRow?.tap()
        XCTAssertTrue(modeControl.waitForNonExistence(timeout: 3))

        // Closing the selected final normal tab exercises the production
        // replacement path and leaves a fresh blank tab, independent of any
        // persisted URL/title from an earlier UI journey.
        XCTAssertTrue(tabs.waitForExistence(timeout: 3))
        tabs.tap()
        XCTAssertTrue(modeControl.waitForExistence(timeout: 3))
        selectTabSwitcherMode(.normal, using: modeControl)
        XCTAssertTrue(waitForQueryCount(1, query: closeButtons, timeout: 3))
        guard let selectedClose = firstHittableElement(in: closeButtons) else {
            XCTFail("The selected normal tab must expose its visible close control.")
            return
        }
        let closedIdentifier = selectedClose.identifier
        selectedClose.tap()
        XCTAssertTrue(
            app.buttons[closedIdentifier].waitForNonExistence(timeout: 3),
            "The old persisted normal tab must visibly leave the switcher."
        )
        XCTAssertTrue(waitForQueryCount(1, query: closeButtons, timeout: 3))
        let freshRow = firstHittableElement(in: tabRows(in: app))
        XCTAssertNotNil(freshRow)
        freshRow?.tap()
        XCTAssertTrue(modeControl.waitForNonExistence(timeout: 3))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
    }

    @MainActor
    private func assertNormalPrivateIsolationAndClearPrivateTabs(
        expectedPrivateCount: Int,
        in app: XCUIApplication
    ) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 3))
        XCTAssertTrue(waitForTabCount(expectedPrivateCount, in: tabs, timeout: 3))
        tabs.tap()

        let modeControl = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(modeControl.waitForExistence(timeout: 3))
        let closeButtons = tabCloseButtons(in: app)
        XCTAssertTrue(
            waitForAtLeastOneElement(in: closeButtons, timeout: 3),
            "The verified private population must expose visible close controls."
        )

        selectTabSwitcherMode(.normal, using: modeControl)
        XCTAssertTrue(
            waitForQueryCount(1, query: closeButtons, timeout: 3),
            "Creating private tabs must not change the normalized normal population."
        )
        guard let normalRow = firstHittableElement(in: tabRows(in: app)) else {
            XCTFail("The isolated normal tab must remain selectable.")
            return
        }
        normalRow.tap()
        XCTAssertTrue(modeControl.waitForNonExistence(timeout: 3))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))

        tabs.tap()
        XCTAssertTrue(modeControl.waitForExistence(timeout: 3))
        selectTabSwitcherMode(.privateBrowsing, using: modeControl)
        XCTAssertTrue(waitForAtLeastOneElement(in: closeButtons, timeout: 3))
        closeTabRows(until: 0, in: app)
        XCTAssertTrue(waitForQueryCount(0, query: closeButtons, timeout: 3))

        // Closing the final private tab ends the private session; the private
        // scene shield then dismisses the still-presented switcher so no
        // private UI draft survives (9ca3bd1, mobile checkpoint).
        XCTAssertTrue(
            modeControl.waitForNonExistence(timeout: 5),
            "Ending the private session must dismiss the private tab switcher."
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
        XCTAssertTrue(waitForHittable(tabs, timeout: 5))
        tabs.tap()
        XCTAssertTrue(modeControl.waitForExistence(timeout: 3))
        selectTabSwitcherMode(.normal, using: modeControl)
        XCTAssertTrue(waitForQueryCount(1, query: closeButtons, timeout: 3))
        let remainingNormalRow = firstHittableElement(in: tabRows(in: app))
        XCTAssertNotNil(remainingNormalRow)
        remainingNormalRow?.tap()
        XCTAssertTrue(modeControl.waitForNonExistence(timeout: 3))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
    }

    @MainActor
    private func selectTabSwitcherMode(
        _ mode: MobileUITestBrowsingMode,
        using control: XCUIElement
    ) {
        control.coordinate(withNormalizedOffset: CGVector(
            dx: mode == .normal ? 0.25 : 0.75,
            dy: 0.5
        )).tap()
    }

    @MainActor
    private func closeTabRows(until expectedCount: Int, in app: XCUIApplication) {
        let closeButtons = tabCloseButtons(in: app)
        var attempts = 0
        while closeButtons.count > expectedCount, attempts < 80 {
            attempts += 1
            guard let closeButton = firstHittableElement(in: closeButtons) else {
                XCTFail("Every visible tab population must retain a hittable close control.")
                return
            }
            let identifier = closeButton.identifier
            closeButton.tap()
            XCTAssertTrue(
                app.buttons[identifier].waitForNonExistence(timeout: 3),
                "Closing a tab must remove that exact row before the next action."
            )
        }
        XCTAssertEqual(closeButtons.count, expectedCount)
    }

    @MainActor
    private func tabCloseButtons(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-close."
        ))
    }

    @MainActor
    private func tabRows(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        ))
    }

    @MainActor
    private func firstHittableElement(in query: XCUIElementQuery) -> XCUIElement? {
        (0..<query.count)
            .map { query.element(boundBy: $0) }
            .first(where: \.isHittable)
    }

    @MainActor
    private func waitForAtLeastOneElement(
        in query: XCUIElementQuery,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if query.count > 0 { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return query.count > 0
    }

    @MainActor
    private func waitForQueryCount(
        _ expected: Int,
        query: XCUIElementQuery,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if query.count == expected { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return query.count == expected
    }

    @MainActor
    private func navigate(
        _ app: XCUIApplication,
        to url: URL,
        waitingForText text: String
    ) {
        let address = app.buttons["browser.address"]
        XCTAssertTrue(address.waitForExistence(timeout: 3))
        address.tap()
        let field = app.textFields.firstMatch
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        field.tap()
        field.typeText(url.absoluteString)
        let navigate = app.buttons["browser.search.navigate"]
        XCTAssertTrue(navigate.waitForExistence(timeout: 3))
        navigate.tap()
        XCTAssertTrue(
            app.webViews.firstMatch.staticTexts[text].waitForExistence(timeout: 8),
            "The product address flow must visibly load the loopback document."
        )
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
    func dragPageUpKeepingFixtureActionsVisible(_ webView: XCUIElement) {
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
    func drag(
        _ element: XCUIElement,
        fromY: CGFloat,
        toY: CGFloat
    ) {
        // The jitter journey passes the fixture's non-interactive nested
        // scroller, so its center remains a stable scroll target throughout.
        let start = element.coordinate(
            withNormalizedOffset: CGVector(dx: 0.5, dy: fromY)
        )
        let end = element.coordinate(
            withNormalizedOffset: CGVector(dx: 0.5, dy: toY)
        )
        start.press(forDuration: 0.08, thenDragTo: end)
    }

    @MainActor
    func assertReachableHitTarget(
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
    private func waitForTabCount(
        _ expected: Int,
        in element: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if tabCount(in: element) == expected { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return tabCount(in: element) == expected
    }

    @MainActor
    private func tabCount(in element: XCUIElement) -> Int? {
        if let value = element.value as? String,
           let count = Int(value.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return count
        }
        if let value = element.value as? NSNumber {
            return value.intValue
        }
        return tabCount(from: element.label)
    }

    private func tabCount(from label: String) -> Int? {
        let digits = label.compactMap(\.wholeNumberValue)
        guard !digits.isEmpty else { return nil }
        return digits.reduce(0) { $0 * 10 + $1 }
    }

    @MainActor
    func assertCompactHarborDeckSemantics(
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
        for identifier in ["browser.back", "browser.address", "browser.tabs", "browser.more"] {
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
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        try MobileUIAcceptanceContract.requireRegularWidthIPad(app: app)

        XCTAssertTrue(app.buttons["browser.sidebar.command"].waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["browser.sidebar.new-tab"].exists)
    }
}

private enum MobileUITestBrowsingMode { case normal, privateBrowsing }
