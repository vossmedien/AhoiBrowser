import XCTest

final class AhoiMobileUITests: XCTestCase {
    @MainActor
    func testLocalFixtureAndPrivateTabLifecycle() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        XCTAssertTrue(
            app.webViews.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 8),
            "The deterministic local WebPage fixture must render."
        )
        XCTAssertTrue(app.buttons["browser.address"].exists)
        XCTAssertTrue(app.buttons["browser.tabs"].exists)

        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.actions.new-tab"].waitForExistence(timeout: 2))
        XCTAssertTrue(app.buttons["browser.actions.workspaces"].exists)
        XCTAssertTrue(app.buttons["browser.actions.share"].exists)
        XCTAssertTrue(app.buttons["browser.new-private-tab"].waitForExistence(timeout: 2))
        app.buttons["browser.new-private-tab"].tap()
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 3))

        app.terminate()
        app.launchArguments = []
        app.launch()
        XCTAssertFalse(
            app.buttons["browser.address.private"].waitForExistence(timeout: 1),
            "Private tabs must never survive process restart."
        )
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 5))
    }

    @MainActor
    func testColdExternalURLIsVisiblyDeduplicatedWithoutDefaultBrowserGrant() throws {
        guard #available(iOS 16.4, *) else {
            throw XCTSkip("XCUIApplication.open(_:) requires iOS 16.4 or newer.")
        }

        let fixtureURL = "https://fixture.ahoibrowser.test/start"
        let externalURL = try XCTUnwrap(
            URL(string: "https://external-url-dedupe.ahoibrowser.test/pre-grant")
        )
        let app = XCUIApplication()

        // Seed and then verify one persisted normal tab. The external URL can
        // therefore only produce the expected second visible tab after the
        // subsequent cold start.
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()
        XCTAssertTrue(
            app.webViews.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 8),
            "The deterministic local fixture must render before seeding the session."
        )
        let address = app.buttons["browser.address"]
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForAccessibilityValue(fixtureURL, of: address, timeout: 3))
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))

        app.terminate()
        app.launchArguments = []
        app.launch()
        XCTAssertTrue(
            waitForAccessibilityValue(fixtureURL, of: address, timeout: 8),
            "The controlled one-tab fixture session must be restored before the cold URL open."
        )
        XCTAssertTrue(waitForTabCount(1, in: tabs, timeout: 3))
        app.terminate()

        // This drives the exact test application directly. It intentionally
        // does not exercise or claim system/default-browser routing.
        app.open(externalURL)
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

        // Invoke the duplicate immediately after observing the first delivery,
        // keeping the second open inside the production deduplication window.
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
    func testUnsafeSchemeIsExplainedAndRejected() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 5))
        app.buttons["browser.address"].tap()
        let address = app.textFields.firstMatch
        XCTAssertTrue(address.waitForExistence(timeout: 3))
        let clearAddress = app.buttons["browser.address.clear"]
        XCTAssertTrue(clearAddress.waitForExistence(timeout: 2))
        clearAddress.tap()
        XCTAssertFalse((address.value as? String ?? "").contains("fixture.ahoibrowser"))
        address.tap()
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 2))
        address.typeText("javascript:alert(1)")
        XCTAssertEqual(address.value as? String, "javascript:alert(1)")
        app.keyboards.buttons["Go"].tap()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.error.message"].waitForExistence(timeout: 3),
            "The localized validation message must be exposed to assistive technology."
        )
        XCTAssertTrue(address.exists, "Invalid input must keep the address editor open.")
    }

    @MainActor
    func testOfflineFailureExplainsAndOffersRetry() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestOffline"]
        app.launch()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.page-failure"].waitForExistence(timeout: 8)
        )
        XCTAssertTrue(app.descendants(matching: .any)["browser.retry"].exists)
    }

    @MainActor
    func testDebugLocalSyncOptInStaysLocalAndFailClosed() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        openSettings(in: app)
        let toggle = app.switches["settings.sync.enabled"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        setSwitch(toggle, enabled: false)
        setSwitch(toggle, enabled: true)

        XCTAssertTrue(
            app.descendants(matching: .any)["settings.sync.configuration-missing"]
                .waitForExistence(timeout: 3),
            "Provider-free DebugLocal must explain that enabled sync remains local-only."
        )
        let state = app.descendants(matching: .any)["settings.sync.state"]
        let keyLifecycle = app.descendants(matching: .any)["settings.sync.key-lifecycle"]
        XCTAssertTrue(state.exists)
        XCTAssertTrue(keyLifecycle.exists)
        XCTAssertTrue(
            ["Local only", "Nur lokal"].contains(state.value as? String ?? ""),
            "The provider-free status must remain local-only in every supported test locale."
        )
        XCTAssertTrue(
            ["Sync keys are off", "Sync-Schlüssel sind deaktiviert"]
                .contains(keyLifecycle.value as? String ?? ""),
            "DebugLocal must not activate or fabricate a sync key."
        )
        XCTAssertFalse(
            app.buttons["settings.sync.now"].isEnabled,
            "Sync now must stay disabled without an entitled runtime."
        )
        XCTAssertTrue(app.buttons["settings.done"].exists)

        app.buttons["settings.done"].tap()
        app.terminate()
        app.launch()

        openSettings(in: app)
        let restoredToggle = app.switches["settings.sync.enabled"]
        XCTAssertTrue(restoredToggle.waitForExistence(timeout: 3))
        XCTAssertEqual(restoredToggle.value as? String, "1")
        XCTAssertTrue(
            ["Local only", "Nur lokal"].contains(
                app.descendants(matching: .any)["settings.sync.state"].value as? String ?? ""
            ),
            "The opt-in must persist without fabricating an entitled runtime."
        )

        setSwitch(restoredToggle, enabled: false)
        XCTAssertTrue(
            app.descendants(matching: .any)["settings.sync.configuration-missing"]
                .waitForNonExistence(timeout: 3)
        )
        XCTAssertFalse(app.buttons["settings.sync.now"].isEnabled)
    }

    @MainActor
    func testDeviceRevocationConfirmsScopeAndRemovesRemoteTarget() throws {
        let app = XCUIApplication()
        app.launchArguments = [
            "-AhoiUITestFixture",
            "-AhoiUITestDeviceRevocation",
        ]
        app.launch()

        openSettings(in: app)
        let removeFixtureMac = app.buttons[
            "settings.devices.remove.72000000-0000-4000-8000-000000000002"
        ]
        reveal(removeFixtureMac, in: app)
        XCTAssertTrue(
            removeFixtureMac.isHittable,
            "The deterministic Mac fixture must expose an explicit revoke action."
        )
        removeFixtureMac.tap()

        let warning = localizedStaticText(
            in: app,
            labels: [
                "Fixture Mac will disappear from synced devices and remote-command targets. This does not rotate the shared encrypted payload key.",
                "Fixture Mac verschwindet aus synchronisierten Geräten und Fernbefehlszielen. Der gemeinsame verschlüsselte Nutzdaten-Schlüssel wird dadurch nicht gewechselt.",
            ]
        )
        XCTAssertTrue(
            warning.waitForExistence(timeout: 3),
            "Confirmation must distinguish device removal from payload-key rotation."
        )

        let identifiedConfirmation = app.buttons.matching(
            identifier: "settings.devices.remove.confirm"
        ).firstMatch
        let labeledConfirmation = localizedButton(
            in: app,
            labels: ["Revoke and remove", "Widerrufen und entfernen"]
        )
        let confirmation = identifiedConfirmation.waitForExistence(timeout: 1)
            ? identifiedConfirmation
            : labeledConfirmation
        XCTAssertTrue(confirmation.waitForExistence(timeout: 2))
        confirmation.tap()

        XCTAssertTrue(
            removeFixtureMac.waitForNonExistence(timeout: 4),
            "A revoked device must disappear from actionable settings rows."
        )
        XCTAssertFalse(
            app.buttons[
                "settings.devices.remove.72000000-0000-4000-8000-000000000002"
            ].exists,
            "The revoked Mac must no longer be exposed as a remote-command target action."
        )

        let cryptographicLimit = localizedStaticText(
            in: app,
            labels: [
                "Removing a device stops Ahoi Sync and remote-command targeting from current records. Because the encrypted payload key is currently shared, this is not complete per-device cryptographic isolation.",
                "Das Entfernen stoppt Ahoi Sync und Fernbefehle für das Gerät in den aktuellen Datensätzen. Da der verschlüsselte Nutzdaten-Schlüssel derzeit geteilt wird, ist dies noch keine vollständige kryptografische Isolierung pro Gerät.",
            ]
        )
        reveal(cryptographicLimit, in: app)
        XCTAssertTrue(
            cryptographicLimit.exists,
            "The honest shared-key isolation limit must remain visible after revocation."
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
            predicate: NSPredicate(
                format: "label BEGINSWITH %@",
                "\(expectedCount) "
            ),
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

    @MainActor
    private func openSettings(in app: XCUIApplication) {
        XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
        app.buttons["browser.more"].tap()
        let settings = app.buttons["browser.actions.settings"]
        for _ in 0..<4 where !settings.exists {
            app.swipeUp()
        }
        XCTAssertTrue(settings.waitForExistence(timeout: 3))
        XCTAssertTrue(settings.isHittable)
        settings.tap()
    }

    @MainActor
    private func setSwitch(
        _ toggle: XCUIElement,
        enabled: Bool,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let expectedValue = enabled ? "1" : "0"
        guard (toggle.value as? String) != expectedValue else { return }

        // Tapping the row label is not guaranteed to toggle a SwiftUI switch.
        // Target the trailing native control and wait for the accessibility
        // value before performing the next action.
        toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        let predicate = NSPredicate(format: "value == %@", expectedValue)
        let expectation = XCTNSPredicateExpectation(predicate: predicate, object: toggle)
        XCTAssertEqual(
            XCTWaiter.wait(for: [expectation], timeout: 3),
            .completed,
            "The sync opt-in switch did not reach the requested state.",
            file: file,
            line: line
        )
    }

    @MainActor
    private func reveal(
        _ element: XCUIElement,
        in app: XCUIApplication,
        maximumSwipes: Int = 7
    ) {
        for _ in 0..<maximumSwipes {
            if element.waitForExistence(timeout: 1), element.isHittable { return }
            app.swipeUp()
        }
    }

    @MainActor
    private func localizedStaticText(
        in app: XCUIApplication,
        labels: [String]
    ) -> XCUIElement {
        app.staticTexts.matching(
            NSPredicate(format: "label IN %@", labels)
        ).firstMatch
    }

    @MainActor
    private func localizedButton(
        in app: XCUIApplication,
        labels: [String]
    ) -> XCUIElement {
        app.buttons.matching(
            NSPredicate(format: "label IN %@", labels)
        ).firstMatch
    }
}
