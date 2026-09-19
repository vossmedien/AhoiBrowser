import XCTest

final class AhoiMobileUITests: MobileBrowserUITestCase {
    @MainActor
    func testLocalFixtureAndPrivateTabLifecycle() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

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
        relaunchExactCandidate(app)
        XCTAssertFalse(
            app.buttons["browser.address.private"].waitForExistence(timeout: 1),
            "Private tabs must never survive process restart."
        )
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 5))
    }

    @MainActor
    func testUnsafeSchemeIsExplainedAndRejected() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])

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
        let app = launchExactCandidate(arguments: ["-AhoiUITestOffline"])

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.page-failure"].waitForExistence(timeout: 8)
        )
        XCTAssertTrue(app.descendants(matching: .any)["browser.retry"].exists)
        XCTAssertFalse(
            app.webViews.firstMatch.exists,
            "A failure presentation must replace stale web content semantically and visually."
        )
    }

    @MainActor
    func testDebugLocalSyncOptInStaysLocalAndFailClosed() throws {
        let app = launchExactCandidate(arguments: [])
        attachScreenshot(named: "01-normal-browser-before-sync", of: app)

        openSettings(in: app)
        let toggle = app.switches["settings.sync.enabled"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
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
        attachScreenshot(named: "02-provider-free-sync-opt-in", of: app)

        app.buttons["settings.done"].tap()
        app.terminate()
        relaunchExactCandidate(app, arguments: [])

        openSettings(in: app)
        let restoredToggle = app.switches["settings.sync.enabled"]
        XCTAssertTrue(restoredToggle.waitForExistence(timeout: 3))
        revealSyncToggle(restoredToggle, in: app)
        XCTAssertEqual(restoredToggle.value as? String, "1")
        XCTAssertTrue(
            ["Local only", "Nur lokal"].contains(
                app.descendants(matching: .any)["settings.sync.state"].value as? String ?? ""
            ),
            "The opt-in must persist without fabricating an entitled runtime."
        )
        attachScreenshot(named: "03-sync-opt-in-after-normal-relaunch", of: app)

        setSwitch(restoredToggle, enabled: false)
        XCTAssertTrue(
            app.descendants(matching: .any)["settings.sync.configuration-missing"]
                .waitForNonExistence(timeout: 3)
        )
        XCTAssertFalse(app.buttons["settings.sync.now"].isEnabled)
        attachScreenshot(named: "04-sync-opt-out-restored", of: app)
    }

    @MainActor
    func testCloudKitDevelopmentSyncOptInShowsRealTransportBoundary() throws {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AHOI_MOBILE_REAL_E2E"] == "1",
              environment["AHOI_MOBILE_EXPECTED_BUILD_MODE"] == "CloudKitDevelopment" else {
            throw XCTSkip(
                "This journey requires an exact CloudKitDevelopment simulator candidate binding."
            )
        }

        let app = launchExactCandidate(arguments: [])
        openSettings(in: app)
        let toggle = app.switches["settings.sync.enabled"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
        setSwitch(toggle, enabled: false)
        setSwitch(toggle, enabled: true)

        let state = app.descendants(matching: .any)["settings.sync.state"]
        let keyLifecycle = app.descendants(matching: .any)["settings.sync.key-lifecycle"]
        XCTAssertTrue(state.waitForExistence(timeout: 3))
        XCTAssertTrue(keyLifecycle.waitForExistence(timeout: 3))

        let keysOff = ["Sync keys are off", "Sync-Schlüssel sind deaktiviert"]
        let activationSettled = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "NOT value IN %@", keysOff),
            object: keyLifecycle
        )
        let activationResult = XCTWaiter.wait(for: [activationSettled], timeout: 30)

        let stateValue = state.value as? String ?? ""
        let keyValue = keyLifecycle.value as? String ?? ""
        let configurationMissing = app.descendants(matching: .any)[
            "settings.sync.configuration-missing"
        ].exists
        let operationError = app.descendants(matching: .any)["browser.library.error"].exists
        let observation = "state=\(stateValue);keyLifecycle=\(keyValue);" +
            "configurationMissing=\(configurationMissing);operationError=\(operationError)"
        let observationAttachment = XCTAttachment(string: observation)
        observationAttachment.name = "CloudKitDevelopment transport boundary"
        observationAttachment.lifetime = .keepAlways
        add(observationAttachment)
        XCTAssertTrue(
            activationResult == .completed || operationError,
            "The real CloudKitDevelopment activation neither settled nor exposed an error. " +
                observation
        )

        let localOnly = ["Local only", "Nur lokal"].contains(stateValue)
        if localOnly {
            XCTAssertTrue(
                configurationMissing,
                "A failed real transport activation must remain visibly local-only. \(observation)"
            )
            XCTAssertFalse(
                app.buttons["settings.sync.now"].isEnabled,
                "A failed real transport activation must not enable manual Sync. \(observation)"
            )
        } else {
            XCTAssertFalse(
                configurationMissing,
                "An active entitled transport must not display the local-only warning. \(observation)"
            )
            XCTAssertTrue(
                app.buttons["settings.sync.now"].isEnabled,
                "An active entitled transport must expose manual Sync. \(observation)"
            )
        }

        attachScreenshot(named: "01-cloudkit-development-transport-boundary", of: app)
        setSwitch(toggle, enabled: false)
        XCTAssertEqual(toggle.value as? String, "0")
        attachScreenshot(named: "02-cloudkit-development-opt-out", of: app)
    }

    @MainActor
    func testDeviceRevocationConfirmsScopeAndRemovesRemoteTarget() throws {
        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture",
            "-AhoiUITestDeviceRevocation",
        ])

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
        guard waitForHittable(toggle, timeout: 3) else {
            XCTFail("The sync opt-in switch is not visible and actionable.", file: file, line: line)
            return
        }

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
    private func revealSyncToggle(_ toggle: XCUIElement, in app: XCUIApplication) {
        let form = app.descendants(matching: .any)["settings.form"]
        XCTAssertTrue(form.waitForExistence(timeout: 3))
        for _ in 0..<5 {
            let frame = toggle.frame
            let visibleFrame = form.frame.insetBy(dx: 0, dy: 12)
            if toggle.exists, visibleFrame.contains(frame) { return }
            form.swipeUp()
        }
        XCTAssertTrue(
            form.frame.insetBy(dx: 0, dy: 12).contains(toggle.frame),
            "The sync opt-in switch must be fully visible before interaction."
        )
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

    @MainActor
    private func attachScreenshot(named name: String, of app: XCUIApplication) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
