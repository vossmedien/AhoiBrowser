import XCTest

final class MobileBrowserScaleLifecycleRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testVisibleOneFiveTwentyNormalTabsRestoreExactRealHTTPSSelection() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        reduceNormalPopulationToOne(in: app)
        XCTAssertTrue(
            waitForTabCount(1, in: app, timeout: 5),
            "The visible scale journey must normalize to one product-created tab."
        )
        attachScreenshot(named: "Visible tab scale - 1", of: app)

        createNormalTabs(until: 5, in: app)
        XCTAssertTrue(waitForTabCount(5, in: app, timeout: 5))
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.header"].exists,
            "A tab created through visible product UI must expose the real blank-tab surface."
        )
        attachScreenshot(named: "Visible tab scale - 5", of: app)

        createNormalTabs(until: 20, in: app)
        XCTAssertTrue(waitForTabCount(20, in: app, timeout: 8))
        attachScreenshot(named: "Visible tab scale - 20", of: app)

        let selectedURL = fixture.url(
            path: "/navigation?visible-scale=20&token=\(UUID().uuidString.lowercased())"
        )
        navigate(to: selectedURL, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8),
            "The twentieth product-created tab must render the real local HTTPS fixture."
        )
        XCTAssertTrue(waitForTabCount(20, in: app, timeout: 3))

        relaunchExactCandidate(app)
        XCTAssertTrue(
            waitForTabCount(20, in: app, timeout: 8),
            "All twenty normal tabs must be restored after process termination."
        )
        assertAddress(selectedURL, containsOrigin: fixture.origin, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 10),
            "The selected restored tab must reload its exact real HTTPS document."
        )
        attachScreenshot(named: "Visible tab scale - 20 restored", of: app)
        reduceNormalPopulationToOne(in: app)
    }

    @MainActor
    func testVisiblePrivatePopulationIsGoneAfterProcessTermination() async throws {
        _ = try await requireReachableFixture()
        let app = coldLaunchApplication()
        reduceNormalPopulationToOne(in: app)
        XCTAssertTrue(waitForTabCount(1, in: app, timeout: 5))

        for expectedCount in 1...5 {
            createPrivateTab(in: app)
            XCTAssertTrue(
                waitForTabCount(expectedCount, in: app, timeout: 5),
                "The visible private population must reach \(expectedCount) tabs without a launch seam."
            )
        }
        XCTAssertTrue(app.buttons["browser.address.private"].exists)
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 4)
        )
        attachScreenshot(named: "Visible private population - 5", of: app)

        relaunchExactCandidate(app)
        XCTAssertFalse(
            app.buttons["browser.address.private"].waitForExistence(timeout: 1),
            "No private tab may reappear after process termination."
        )
        XCTAssertTrue(
            waitForTabCount(1, in: app, timeout: 5),
            "The isolated normal tab must remain after the private population is purged."
        )

        app.buttons["browser.tabs"].tap()
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 4))
        mode.coordinate(withNormalizedOffset: CGVector(dx: 0.75, dy: 0.5)).tap()
        let privateRows = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        ))
        XCTAssertTrue(
            waitForQueryCount(0, query: privateRows, timeout: 4),
            "The private tab switcher must contain no rows after process restoration."
        )
        let emptyState = app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["No Private Tabs", "Keine privaten Tabs"]
        )).firstMatch
        XCTAssertTrue(
            emptyState.waitForExistence(timeout: 4),
            "The visible private mode must explain that the ephemeral population is empty."
        )
        attachScreenshot(named: "Private population purged after termination", of: app)
    }

    @MainActor
    private func createNormalTabs(until expectedCount: Int, in app: XCUIApplication) {
        guard let currentCount = tabCount(in: app.buttons["browser.tabs"]),
              currentCount <= expectedCount else {
            XCTFail("The visible tab counter must be readable before scale creation.")
            return
        }
        if currentCount == expectedCount { return }
        for count in (currentCount + 1)...expectedCount {
            let more = app.buttons["browser.more"]
            XCTAssertTrue(more.waitForExistence(timeout: 4))
            more.tap()
            let newTab = app.buttons["browser.actions.new-tab"]
            XCTAssertTrue(newTab.waitForExistence(timeout: 3))
            newTab.tap()
            XCTAssertTrue(
                waitForTabCount(count, in: app, timeout: 4),
                "Visible new-tab action did not reach normal-tab count \(count)."
            )
        }
    }

    @MainActor
    private func createPrivateTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 4))
        more.tap()
        let newPrivateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(newPrivateTab.waitForExistence(timeout: 3))
        newPrivateTab.tap()
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 4))
    }

    @MainActor
    private func reduceNormalPopulationToOne(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        tabs.tap()
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 4))
        mode.coordinate(withNormalizedOffset: CGVector(dx: 0.25, dy: 0.5)).tap()
        let closeButtons = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-close."
        ))
        var attempts = 0
        while closeButtons.count > 1, attempts < 40 {
            attempts += 1
            let close = (0..<closeButtons.count)
                .map { closeButtons.element(boundBy: $0) }
                .first(where: \.isHittable)
            guard let close else {
                XCTFail("The visible scale cleanup must retain a hittable close control.")
                return
            }
            let identifier = close.identifier
            close.tap()
            XCTAssertTrue(app.buttons[identifier].waitForNonExistence(timeout: 3))
        }
        XCTAssertEqual(closeButtons.count, 1)
        let remainingRow = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        )).firstMatch
        XCTAssertTrue(remainingRow.waitForExistence(timeout: 3))
        remainingRow.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))
        XCTAssertTrue(waitForTabCount(1, in: app, timeout: 3))
    }

    @MainActor
    private func waitForTabCount(
        _ expected: Int,
        in app: XCUIApplication,
        timeout: TimeInterval
    ) -> Bool {
        let tabs = app.buttons["browser.tabs"]
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if tabCount(in: tabs) == expected { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return tabCount(in: tabs) == expected
    }

    @MainActor
    private func tabCount(in element: XCUIElement) -> Int? {
        if let value = element.value as? String,
           let count = Int(value.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return count
        }
        if let value = element.value as? NSNumber { return value.intValue }
        let digits = element.label.compactMap(\.wholeNumberValue)
        guard !digits.isEmpty else { return nil }
        return digits.reduce(0) { $0 * 10 + $1 }
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
    private func attachScreenshot(named name: String, of app: XCUIApplication) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
