import Foundation
import XCTest

final class MobileExternalURLRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testColdExternalURLIsVisiblyDeduplicatedWithoutDefaultBrowserGrant() throws {
        guard #available(iOS 16.4, *) else {
            throw XCTSkip("XCUIApplication.open(_:) requires iOS 16.4 or newer.")
        }

        let fixtureURL = "https://fixture.ahoibrowser.test/start"
        let externalURL = try XCTUnwrap(
            URL(string: "https://external-url-dedupe.ahoibrowser.test/pre-grant")
        )
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
            duration: 1.75
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
