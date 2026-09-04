import Foundation
import XCTest

final class MobileExternalURLRealE2EUITests: MobileBrowserRealE2ETestCase {
    private let externalURLStabilityWindow: TimeInterval = 12

    @MainActor
    func testColdExternalURLIsVisiblyDeduplicatedWithoutDefaultBrowserGrant() throws {
        guard #available(iOS 16.4, *) else {
            throw XCTSkip("XCUIApplication.open(_:) requires iOS 16.4 or newer.")
        }

        let fixtureURL = "https://fixture.ahoibrowser.test/start"
        let runToken = UUID().uuidString.lowercased()
        let externalURL = try XCTUnwrap(URL(
            string: "https://external-url-dedupe.ahoibrowser.test/pre-grant?run=\(runToken)"
        ))
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        let address = app.buttons["browser.address"]
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForAccessibilityValue(fixtureURL, of: address, timeout: 8))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 8))

        relaunchExactCandidate(app)
        XCTAssertTrue(
            waitForAccessibilityValue(fixtureURL, of: address, timeout: 8),
            "The controlled one-tab fixture session must be restored before the cold URL open."
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
        app.terminate()

        // This drives the exact test application directly. It intentionally
        // does not exercise or claim system/default-browser routing.
        app.open(externalURL)
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))
        assertExactCandidateBinding(in: app)
        XCTAssertTrue(
            waitForAccessibilityValue(externalURL.absoluteString, of: address, timeout: 8),
            "The cold external URL must become the authoritative address accessibility value."
        )
        XCTAssertTrue(
            waitForTabCount(2, in: tabs, timeout: 8),
            "The first external URL must add exactly one visible tab to the restored fixture tab."
        )
        let firstDeliveryTabLabel = tabs.label
        XCTAssertEqual(tabCount(from: firstDeliveryTabLabel), 2)

        app.open(externalURL)
        assertExternalURLStateRemainsStable(
            address: address,
            expectedURL: externalURL.absoluteString,
            tabs: tabs,
            expectedTabLabel: firstDeliveryTabLabel,
            duration: externalURLStabilityWindow
        )
        XCTAssertEqual(tabCount(from: tabs.label), 2)
        attachExternalURLReceipt(
            externalURL: externalURL,
            address: address,
            firstDeliveryTabLabel: firstDeliveryTabLabel,
            finalTabLabel: tabs.label
        )
    }

    @MainActor
    func testWarmExternalURLCallbackCreatesExactlyOneNormalTab() async throws {
        guard #available(iOS 16.4, *) else {
            throw XCTSkip("XCUIApplication.open(_:) requires iOS 16.4 or newer.")
        }

        let fixture = try await requireReachableFixture()
        let externalURL = fixture.url(
            path: "/navigation?external-url=warm-callback&run=\(UUID().uuidString.lowercased())"
        )
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        let address = app.buttons["browser.address"]
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(
            waitForAccessibilityValue(
                "https://fixture.ahoibrowser.test/start",
                of: address,
                timeout: 8
            )
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 5))

        // The candidate remains foregrounded between launch and delivery, so
        // this exercises the running-scene callback rather than cold bootstrap.
        app.open(externalURL)
        assertExactCandidateBinding(in: app)
        XCTAssertTrue(
            waitForAccessibilityValue(externalURL.absoluteString, of: address, timeout: 8),
            "A warm external callback must select its newly created normal tab."
        )
        XCTAssertTrue(
            waitForTabCount(2, in: tabs, timeout: 8),
            "A loaded normal page must remain intact while one new normal tab is created."
        )
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8),
            "The warm callback must render through the production WebPage path."
        )
        let firstDeliveryTabLabel = tabs.label

        app.open(externalURL)
        assertExternalURLStateRemainsStable(
            address: address,
            expectedURL: externalURL.absoluteString,
            tabs: tabs,
            expectedTabLabel: firstDeliveryTabLabel,
            duration: externalURLStabilityWindow
        )
        XCTAssertEqual(tabCount(from: tabs.label), 2)
    }

    @MainActor
    func testWarmExternalURLReusesSelectedEmptyNormalTab() async throws {
        guard #available(iOS 16.4, *) else {
            throw XCTSkip("XCUIApplication.open(_:) requires iOS 16.4 or newer.")
        }

        let fixture = try await requireReachableFixture()
        let externalURL = fixture.url(
            path: "/navigation?external-url=reuse-empty&run=\(UUID().uuidString.lowercased())"
        )
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        let address = app.buttons["browser.address"]
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(
            waitForAccessibilityValue(
                "https://fixture.ahoibrowser.test/start",
                of: address,
                timeout: 8
            )
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 5))

        createEmptyNormalTab(in: app)
        XCTAssertTrue(waitForTabCount(2, in: tabs, timeout: 5))
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.header"]
                .waitForExistence(timeout: 4),
            "The selected normal tab must visibly be empty before external delivery."
        )
        let preDeliveryTabLabel = tabs.label

        app.open(externalURL)
        assertExactCandidateBinding(in: app)
        XCTAssertTrue(
            waitForAccessibilityValue(externalURL.absoluteString, of: address, timeout: 8),
            "The incoming URL must replace the selected empty normal tab's address."
        )
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8),
            "The reused normal tab must render the real HTTPS fixture."
        )
        assertExternalURLStateRemainsStable(
            address: address,
            expectedURL: externalURL.absoluteString,
            tabs: tabs,
            expectedTabLabel: preDeliveryTabLabel,
            duration: externalURLStabilityWindow
        )
        XCTAssertEqual(
            tabCount(from: tabs.label),
            2,
            "Reusing the empty normal tab must not create a third tab."
        )
    }

    @MainActor
    func testWarmExternalURLLeavesActivePrivateTabUntouchedAndUnpersisted() async throws {
        guard #available(iOS 16.4, *) else {
            throw XCTSkip("XCUIApplication.open(_:) requires iOS 16.4 or newer.")
        }

        let fixture = try await requireReachableFixture()
        let externalURL = fixture.url(
            path: "/navigation?external-url=private-isolation&run=\(UUID().uuidString.lowercased())"
        )
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        let normalAddress = app.buttons["browser.address"]
        let privateAddress = app.buttons["browser.address.private"]
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(
            waitForAccessibilityValue(
                "https://fixture.ahoibrowser.test/start",
                of: normalAddress,
                timeout: 8
            )
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 5))

        createEmptyPrivateTab(in: app)
        XCTAssertTrue(privateAddress.waitForExistence(timeout: 4))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 4))
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 4),
            "The selected private tab must visibly be empty before external delivery."
        )

        app.open(externalURL)
        assertExactCandidateBinding(in: app)
        XCTAssertTrue(
            normalAddress.waitForExistence(timeout: 5),
            "An external URL received over a private tab must select a normal tab."
        )
        XCTAssertTrue(
            waitForAccessibilityValue(externalURL.absoluteString, of: normalAddress, timeout: 8)
        )
        XCTAssertTrue(
            waitForTabCount(2, in: tabs, timeout: 8),
            "The external URL must create one normal tab without replacing the fixture tab."
        )
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )

        selectPrivateTab(in: app)
        XCTAssertTrue(privateAddress.waitForExistence(timeout: 4))
        XCTAssertNotEqual(privateAddress.value as? String, externalURL.absoluteString)
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 4),
            "Selecting the original private tab must reveal the untouched blank private surface."
        )

        relaunchExactCandidate(app)
        XCTAssertFalse(
            privateAddress.waitForExistence(timeout: 1),
            "The active private tab must not survive process termination."
        )
        XCTAssertTrue(
            waitForAccessibilityValue(externalURL.absoluteString, of: normalAddress, timeout: 8),
            "The normal external-URL tab must remain selected after private-free restoration."
        )
        XCTAssertTrue(waitForTabCount(2, in: tabs, timeout: 5))
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 10)
        )
        assertPrivateTabSwitcherIsEmpty(in: app)
    }

    @MainActor
    private func waitForAccessibilityValue(
        _ expectedValue: String,
        of element: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", expectedValue),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    @MainActor
    private func waitForTabCount(
        _ expectedCount: Int,
        in tabs: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "label BEGINSWITH %@", "\(expectedCount) "),
            object: tabs
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    private func tabCount(from accessibilityLabel: String) -> Int? {
        Int(accessibilityLabel.prefix(while: { $0.isNumber }))
    }

    @MainActor
    private func createEmptyNormalTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: 4))
        more.tap()
        let newTab = app.buttons["browser.actions.new-tab"]
        XCTAssertTrue(waitForHittable(newTab, timeout: 3))
        newTab.tap()
    }

    @MainActor
    private func createEmptyPrivateTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: 4))
        more.tap()
        let newPrivateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(waitForHittable(newPrivateTab, timeout: 3))
        newPrivateTab.tap()
    }

    @MainActor
    private func selectPrivateTab(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForHittable(tabs, timeout: 4))
        tabs.tap()
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 3))
        selectPrivateMode(using: mode)

        let privateRows = tabRows(in: app)
        XCTAssertTrue(
            waitForQueryCount(1, query: privateRows, timeout: 4),
            "The untouched private population must contain exactly one visible row."
        )
        let privateRow = privateRows.firstMatch
        XCTAssertTrue(waitForHittable(privateRow, timeout: 3))
        privateRow.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))
    }

    @MainActor
    private func assertPrivateTabSwitcherIsEmpty(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForHittable(tabs, timeout: 4))
        tabs.tap()
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 3))
        selectPrivateMode(using: mode)

        XCTAssertTrue(
            waitForQueryCount(0, query: tabRows(in: app), timeout: 4),
            "No private tab row may be restored after process termination."
        )
        let emptyState = app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["No Private Tabs", "Keine privaten Tabs"]
        )).firstMatch
        XCTAssertTrue(
            emptyState.waitForExistence(timeout: 4),
            "The restored candidate must visibly expose an empty private population."
        )
    }

    @MainActor
    private func selectPrivateMode(using mode: XCUIElement) {
        mode.coordinate(withNormalizedOffset: CGVector(dx: 0.75, dy: 0.5)).tap()
    }

    @MainActor
    private func tabRows(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        ))
    }

    @MainActor
    private func waitForQueryCount(
        _ expectedCount: Int,
        query: XCUIElementQuery,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if query.count == expectedCount { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return query.count == expectedCount
    }

    @MainActor
    private func assertExternalURLStateRemainsStable(
        address: XCUIElement,
        expectedURL: String,
        tabs: XCUIElement,
        expectedTabLabel: String,
        duration: TimeInterval,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let addressChanged = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value != %@", expectedURL),
            object: address
        )
        addressChanged.isInverted = true
        let tabLabelChanged = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "label != %@", expectedTabLabel),
            object: tabs
        )
        tabLabelChanged.isInverted = true
        XCTAssertEqual(
            XCTWaiter.wait(for: [addressChanged, tabLabelChanged], timeout: duration),
            .completed,
            "The repeated external URL changed the visible address or tab count.",
            file: file,
            line: line
        )
        XCTAssertEqual(address.value as? String, expectedURL, file: file, line: line)
        XCTAssertEqual(tabs.label, expectedTabLabel, file: file, line: line)
    }

    @MainActor
    private func attachExternalURLReceipt(
        externalURL: URL,
        address: XCUIElement,
        firstDeliveryTabLabel: String,
        finalTabLabel: String
    ) {
        let screenshot = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        screenshot.name = "External URL after repeated direct app open"
        screenshot.lifetime = .keepAlways
        add(screenshot)

        let receipt = XCTAttachment(string: """
        delivery=XCUIApplication.open(_:)
        delivery_scope=direct_test_application
        system_default_browser_routing_asserted=false
        external_url=\(externalURL.absoluteString)
        browser.address.value=\(address.value as? String ?? "<missing>")
        browser.tabs.label.after_first=\(firstDeliveryTabLabel)
        browser.tabs.label.after_second=\(finalTabLabel)
        """)
        receipt.name = "External URL accessibility receipt"
        receipt.lifetime = .keepAlways
        add(receipt)
    }
}
