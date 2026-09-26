import UIKit
import XCTest

// Face ID shield of private sessions: leaving and returning, cancelled and
// failed authentication, retained content after unlock (split from
// AhoiMobileUITests.swift, source line budget).
extension AhoiMobileUITests {
    @MainActor
    func testPrivateSessionIsShieldedAfterLeavingAndReturning() throws {
        guard ProcessInfo.processInfo.environment["AHOI_PRIVATE_LOCK_E2E"] == "1" else {
            throw XCTSkip("Explicitly opt in to the device-authentication UI journey.")
        }
        let app = XCUIApplication()
        app.launchArguments = []
        app.terminate()
        app.launch()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))

        openSettings(in: app)
        let toggle = app.switches["settings.private.lock"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
        if (toggle.value as? String) != "1" {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        }
        let enabled = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", "1"), object: toggle
        )
        guard XCTWaiter.wait(for: [enabled], timeout: 4) == .completed else {
            attachScreenshot(named: "private-lock-device-auth-unavailable", of: app)
            XCTFail("The real device-authentication setting did not enable on this device.")
            return
        }
        app.buttons["settings.done"].tap()

        app.buttons["browser.more"].tap()
        let privateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(privateTab.waitForExistence(timeout: 3))
        privateTab.tap()
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 5))
        attachScreenshot(named: "private-lock-before-background", of: app)

        XCUIDevice.shared.press(.home)
        app.activate()
        let unlock = app.buttons["browser.private.lock.unlock"]
        XCTAssertTrue(
            unlock.waitForExistence(timeout: 8),
            "Returning to a retained private session must show the native lock shield."
        )
        XCTAssertFalse(
            app.buttons["browser.address.private"].exists,
            "The shield must not expose private browser controls to accessibility."
        )
        attachScreenshot(named: "private-lock-after-return", of: app)
    }

    @MainActor
    func testPrivateSessionRemainsShieldedWhenAuthenticationIsCancelled() throws {
        guard ProcessInfo.processInfo.environment["AHOI_PRIVATE_LOCK_E2E"] == "1" else {
            throw XCTSkip("Explicitly opt in to the device-authentication UI journey.")
        }
        let app = XCUIApplication()
        app.launchArguments = []
        app.terminate()
        app.launch()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))

        openSettings(in: app)
        let toggle = app.switches["settings.private.lock"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
        if (toggle.value as? String) != "1" {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        }
        let enabled = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", "1"), object: toggle
        )
        guard XCTWaiter.wait(for: [enabled], timeout: 4) == .completed else {
            attachScreenshot(named: "private-lock-cancel-device-auth-unavailable", of: app)
            XCTFail("The real device-authentication setting did not enable on this device.")
            return
        }
        app.buttons["settings.done"].tap()

        app.buttons["browser.more"].tap()
        let privateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(privateTab.waitForExistence(timeout: 3))
        privateTab.tap()
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 5))
        XCUIDevice.shared.press(.home)
        app.activate()

        let unlock = app.buttons["browser.private.lock.unlock"]
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        unlock.tap()
        let springboard = XCUIApplication(bundleIdentifier: "com.apple.springboard")
        let authenticationUI = springboard.otherElements["authentication_ui"]
        guard authenticationUI.waitForExistence(timeout: 8) else {
            attachScreenshot(named: "private-lock-cancel-auth-prompt-missing", of: app)
            XCTFail("The real device-authentication screen must be presented.")
            return
        }
        attachScreenshot(named: "private-lock-authentication-prompt", of: app)
        let cancel = springboard.buttons["Cancel"]
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        cancel.tap()

        XCTAssertTrue(unlock.waitForExistence(timeout: 5))
        XCTAssertFalse(
            app.buttons["browser.address.private"].exists,
            "Cancelling device authentication must never expose the private tab."
        )
        attachScreenshot(named: "private-lock-after-authentication-cancel", of: app)
    }

    @MainActor
    func testLoadedPrivatePageStaysShieldedAfterCancelAndSecondBackground() throws {
        guard ProcessInfo.processInfo.environment["AHOI_PRIVATE_LOCK_E2E"] == "1" else {
            throw XCTSkip("Explicitly opt in to the device-authentication UI journey.")
        }
        let app = XCUIApplication()
        app.launchArguments = []
        app.terminate()
        app.launch()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))

        openSettings(in: app)
        let toggle = app.switches["settings.private.lock"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
        if (toggle.value as? String) != "1" {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        }
        let enabled = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", "1"), object: toggle
        )
        guard XCTWaiter.wait(for: [enabled], timeout: 4) == .completed else {
            attachScreenshot(named: "private-loaded-page-device-auth-unavailable", of: app)
            XCTFail("The real device-authentication setting did not enable on this device.")
            return
        }
        app.buttons["settings.done"].tap()

        app.terminate()
        app.launchArguments = [
            "-AhoiUITestFixture", "-AhoiUITestPrivateTabCount", "1", "-AhoiUITestSelectPrivate"
        ]
        app.launch()
        let privateAddress = app.buttons["browser.address.private"]
        XCTAssertTrue(privateAddress.waitForExistence(timeout: 8))
        let privatePage = app.webViews.staticTexts["Scale tab"]
        XCTAssertTrue(privatePage.waitForExistence(timeout: 8))
        attachScreenshot(named: "private-loaded-page-before-background", of: app)

        XCUIDevice.shared.press(.home)
        app.activate()
        let unlock = app.buttons["browser.private.lock.unlock"]
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        XCTAssertFalse(privatePage.exists)
        XCTAssertFalse(privateAddress.exists)
        unlock.tap()
        let springboard = XCUIApplication(bundleIdentifier: "com.apple.springboard")
        XCTAssertTrue(springboard.otherElements["authentication_ui"].waitForExistence(timeout: 8))
        let cancel = springboard.buttons["Cancel"]
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        cancel.tap()
        XCTAssertTrue(unlock.waitForExistence(timeout: 5))
        XCTAssertFalse(privatePage.exists)
        XCTAssertFalse(privateAddress.exists)
        attachScreenshot(named: "private-loaded-page-after-cancel", of: app)

        XCUIDevice.shared.press(.home)
        app.activate()
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        XCTAssertFalse(privatePage.exists)
        XCTAssertFalse(privateAddress.exists)
        XCTAssertTrue(unlock.isHittable)
        NSLog("AHOI_PRIVATE_LOCK_SECOND_RETURN_READY")
        // Keep the actual foreground scene observable for a separately captured
        // Simulator screenshot; XCTest's repeated screenshot API can return an
        // empty frame even while the external compositor still shows the shield.
        if ProcessInfo.processInfo.environment["AHOI_PRIVATE_LOCK_CAPTURE_HOLD"] == "1" {
            Thread.sleep(forTimeInterval: 8)
        }

        app.terminate()
        app.launchArguments = []
        app.launch()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))
        XCTAssertFalse(privateAddress.exists)
        XCTAssertFalse(privatePage.exists)
    }

    /// Requires a Simulator whose Face ID enrollment the host enabled beforehand
    /// (`notifyutil -s com.apple.BiometricKit.enrollmentChanged 1`); the match is
    /// the Simulator's own biometric event, not an app-side bypass.
    @MainActor
    func testLoadedPrivatePageUnlocksWithRetainedContentAndRelocks() throws {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AHOI_PRIVATE_LOCK_E2E"] == "1",
              environment["AHOI_PRIVATE_LOCK_SIMULATED_FACE_ID"] == "1" else {
            throw XCTSkip("Explicitly opt in with an enrolled Simulator Face ID.")
        }
        let app = XCUIApplication()
        app.launchArguments = []
        app.terminate()
        app.launch()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))

        openSettings(in: app)
        let toggle = app.switches["settings.private.lock"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
        if (toggle.value as? String) != "1" {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        }
        let enabled = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", "1"), object: toggle
        )
        guard XCTWaiter.wait(for: [enabled], timeout: 4) == .completed else {
            attachScreenshot(named: "private-unlock-device-auth-unavailable", of: app)
            XCTFail("The real device-authentication setting did not enable on this device.")
            return
        }
        app.buttons["settings.done"].tap()

        app.terminate()
        app.launchArguments = [
            "-AhoiUITestFixture", "-AhoiUITestPrivateTabCount", "1", "-AhoiUITestSelectPrivate"
        ]
        app.launch()
        let privateAddress = app.buttons["browser.address.private"]
        XCTAssertTrue(privateAddress.waitForExistence(timeout: 8))
        let privatePage = app.webViews.staticTexts["Scale tab"]
        XCTAssertTrue(privatePage.waitForExistence(timeout: 8))

        XCUIDevice.shared.press(.home)
        app.activate()
        let unlock = app.buttons["browser.private.lock.unlock"]
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        XCTAssertFalse(privatePage.exists)
        XCTAssertFalse(privateAddress.exists)
        unlock.tap()
        // Let the system biometric sheet appear before the Simulator match event.
        Thread.sleep(forTimeInterval: 2)
        attachScreenshot(named: "private-unlock-face-id-prompt", of: app)
        let systemUI = XCTAttachment(
            string: XCUIApplication(bundleIdentifier: "com.apple.springboard").debugDescription
        )
        systemUI.name = "private-unlock-system-auth-hierarchy"
        systemUI.lifetime = .keepAlways
        add(systemUI)
        // The host may also deliver the same Simulator match event while this waits.
        NSLog("AHOI_PRIVATE_UNLOCK_AWAITING_MATCH")
        CFNotificationCenterPostNotification(
            CFNotificationCenterGetDarwinNotifyCenter(),
            CFNotificationName("com.apple.BiometricKit_Sim.pearl.match" as CFString),
            nil, nil, true
        )

        XCTAssertTrue(privateAddress.waitForExistence(timeout: 20))
        XCTAssertTrue(privatePage.waitForExistence(timeout: 5))
        XCTAssertFalse(unlock.exists)
        attachScreenshot(named: "private-unlock-retained-page", of: app)

        XCUIDevice.shared.press(.home)
        app.activate()
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        XCTAssertFalse(privatePage.exists)
        XCTAssertFalse(privateAddress.exists)
        attachScreenshot(named: "private-unlock-relocked", of: app)

        app.terminate()
        app.launchArguments = []
        app.launch()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))
        XCTAssertFalse(privateAddress.exists)
        XCTAssertFalse(privatePage.exists)
    }

    /// Same Simulator Face ID opt-in as the unlock journey; the host delivers a
    /// non-matching face (`com.apple.BiometricKit_Sim.pearl.nomatch`).
    @MainActor
    func testFailedFaceIDKeepsLoadedPrivatePageShielded() throws {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AHOI_PRIVATE_LOCK_E2E"] == "1",
              environment["AHOI_PRIVATE_LOCK_SIMULATED_FACE_ID_NOMATCH"] == "1" else {
            throw XCTSkip("Explicitly opt in with an enrolled Simulator Face ID.")
        }
        let app = XCUIApplication()
        app.launchArguments = []
        app.terminate()
        app.launch()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))
        openSettings(in: app)
        let toggle = app.switches["settings.private.lock"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 3))
        revealSyncToggle(toggle, in: app)
        if (toggle.value as? String) != "1" {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        }
        XCTAssertTrue(XCTWaiter.wait(for: [XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", "1"), object: toggle
        )], timeout: 4) == .completed)
        app.buttons["settings.done"].tap()

        app.terminate()
        app.launchArguments = [
            "-AhoiUITestFixture", "-AhoiUITestPrivateTabCount", "1", "-AhoiUITestSelectPrivate"
        ]
        app.launch()
        let privateAddress = app.buttons["browser.address.private"]
        let privatePage = app.webViews.staticTexts["Scale tab"]
        XCTAssertTrue(privatePage.waitForExistence(timeout: 8))

        XCUIDevice.shared.press(.home)
        app.activate()
        let unlock = app.buttons["browser.private.lock.unlock"]
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        unlock.tap()
        NSLog("AHOI_PRIVATE_UNLOCK_AWAITING_NOMATCH")
        // The host sends non-matching faces; the system eventually offers a
        // retry/passcode sheet, which the test cancels.
        Thread.sleep(forTimeInterval: 6)
        XCTAssertFalse(privatePage.exists, "A failed face must never reveal the private page.")
        XCTAssertFalse(privateAddress.exists)
        attachScreenshot(named: "private-nomatch-shielded", of: app)
        let springboard = XCUIApplication(bundleIdentifier: "com.apple.springboard")
        let cancel = springboard.buttons.matching(
            NSPredicate(format: "label IN %@", ["Cancel", "Abbrechen"])
        ).firstMatch
        if cancel.waitForExistence(timeout: 8) { cancel.tap() }
        XCTAssertTrue(unlock.waitForExistence(timeout: 8))
        XCTAssertFalse(privatePage.exists)
        XCTAssertFalse(privateAddress.exists)
        attachScreenshot(named: "private-nomatch-after-cancel", of: app)
    }

}
