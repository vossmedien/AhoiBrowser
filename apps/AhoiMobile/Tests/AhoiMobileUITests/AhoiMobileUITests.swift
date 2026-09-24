import UIKit
import XCTest

final class AhoiMobileUITests: MobileBrowserUITestCase {
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
    func testReaderExtractsVisibleArticleAndReturnsToSamePage() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer { app.terminate() }
        XCTAssertTrue(app.webViews.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 8))

        app.buttons["browser.more"].tap()
        let reader = app.buttons["browser.actions.reader"]
        XCTAssertTrue(reader.waitForExistence(timeout: 5))
        for _ in 0..<4 {
            if reader.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(reader.isHittable)
        reader.tap()

        let content = app.descendants(matching: .any)["browser.reader.content"]
        XCTAssertTrue(content.waitForExistence(timeout: 8))
        XCTAssertTrue(content.staticTexts["Ahoi Reader fixture article"].exists)
        XCTAssertTrue(content.staticTexts.matching(NSPredicate(
            format: "label BEGINSWITH %@", "The first paragraph is ordinary visible prose"
        )).firstMatch.exists)
        XCTAssertFalse(content.staticTexts["Ahoi fixture page"].exists)
        XCTAssertFalse(content.staticTexts["Ahoi visible find target"].exists)
        attachScreenshot(named: "reader-loaded-article", of: app)

        app.buttons["browser.reader.return"].tap()
        XCTAssertTrue(app.webViews.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["browser.address"].exists)
    }

    @MainActor
    func testPageLinkCopiesAndUnavailableReaderKeepOriginalPage() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer { app.terminate() }
        XCTAssertTrue(app.webViews.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 8))

        app.buttons["browser.more"].tap()
        let addressCopy = app.buttons["browser.actions.copy-address"]
        let markdownCopy = app.buttons["browser.actions.copy-markdown"]
        XCTAssertTrue(addressCopy.waitForExistence(timeout: 5))
        for _ in 0..<4 {
            if addressCopy.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(addressCopy.isHittable)
        addressCopy.tap()
        XCTAssertEqual(readPasteboardString(), "https://fixture.ahoibrowser.test/start")
        XCTAssertTrue(markdownCopy.isHittable)
        markdownCopy.tap()
        XCTAssertEqual(
            readPasteboardString(),
            "[Ahoi Fixture](<https://fixture.ahoibrowser.test/start>)"
        )
        attachScreenshot(named: "page-link-copy-actions", of: app)
        app.buttons["browser.actions.done"].tap()

        let removeArticle = app.webViews.buttons["Remove Reader article fixture"]
        XCTAssertTrue(removeArticle.waitForExistence(timeout: 5))
        removeArticle.tap()
        XCTAssertFalse(app.webViews.staticTexts["Ahoi Reader fixture article"].exists)
        app.buttons["browser.more"].tap()
        let reader = app.buttons["browser.actions.reader"]
        XCTAssertTrue(reader.waitForExistence(timeout: 5))
        for _ in 0..<4 {
            if reader.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(reader.isHittable)
        reader.tap()

        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 8))
        XCTAssertTrue(
            alert.staticTexts["No readable article found on this page."].exists ||
            alert.staticTexts["Auf dieser Seite wurde kein lesbarer Artikel gefunden."].exists
        )
        XCTAssertFalse(app.descendants(matching: .any)["browser.reader.content"].exists)
        attachScreenshot(named: "reader-unavailable-original-page", of: app)
        alert.buttons.firstMatch.tap()
        app.buttons["browser.actions.done"].tap()
        XCTAssertTrue(app.webViews.staticTexts["Ahoi fixture page"].exists)
    }

    @MainActor
    func testContextualHelpExplainsHomeAddressAndLinkPreview() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer { app.terminate() }
        XCTAssertTrue(app.webViews.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 8))

        app.buttons["browser.more"].tap()
        let save = app.buttons["browser.actions.save-to-workspace"]
        XCTAssertTrue(save.waitForExistence(timeout: 5))
        for _ in 0..<4 where !save.isHittable { app.swipeUp() }
        save.tap()
        let destination = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@", "browser.actions.save-to-workspace."
        )).firstMatch
        XCTAssertTrue(destination.waitForExistence(timeout: 4))
        destination.tap()
        if app.buttons["browser.actions.done"].waitForExistence(timeout: 2) {
            app.buttons["browser.actions.done"].tap()
        }

        app.buttons["browser.more"].tap()
        let homeHelp = app.staticTexts["browser.actions.home-help"]
        XCTAssertTrue(homeHelp.waitForExistence(timeout: 8))
        for _ in 0..<6 where !homeHelp.isHittable { app.swipeUp() }
        XCTAssertTrue(
            homeHelp.label.hasPrefix("Die Ausgangsadresse ist der feste Startpunkt") ||
            homeHelp.label.hasPrefix("A Home Address is this saved page")
        )
        XCTAssertTrue(app.staticTexts["browser.actions.home-state"].exists)
        attachScreenshot(named: "home-address-help", of: app)
        app.buttons["browser.actions.done"].tap()

        let link = app.webViews.links["Open Ahoi link actions"]
        XCTAssertTrue(link.waitForExistence(timeout: 5))
        link.press(forDuration: 1.2)
        let previewHelp = app.staticTexts["browser.link-actions.preview-help"]
        XCTAssertTrue(previewHelp.waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["browser.link-actions.preview"].exists)
        XCTAssertTrue(
            previewHelp.label.hasPrefix("Die Vorschau lädt den Link") ||
            previewHelp.label.hasPrefix("Preview loads the link")
        )
        attachScreenshot(named: "link-preview-help", of: app)
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
        let setupIssue = app.descendants(matching: .any)["settings.sync.setup-issue"]
        let configurationMissingElement = app.descendants(matching: .any)[
            "settings.sync.configuration-missing"
        ]
        let activationDeadline = Date(timeIntervalSinceNow: 30)
        while Date() < activationDeadline,
              keysOff.contains(keyLifecycle.value as? String ?? ""),
              !setupIssue.exists,
              !configurationMissingElement.exists {
            RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.2))
        }

        let stateValue = state.value as? String ?? ""
        let keyValue = keyLifecycle.value as? String ?? ""
        let configurationMissing = configurationMissingElement.exists
        let setupIssueExists = setupIssue.exists
        let setupIssueValue = setupIssueExists ? (setupIssue.value as? String ?? "") : ""
        let operationError = app.descendants(matching: .any)["browser.library.error"].exists
        let observation = "state=\(stateValue);keyLifecycle=\(keyValue);" +
            "configurationMissing=\(configurationMissing);" +
            "setupIssue=\(setupIssueValue);operationError=\(operationError)"
        let observationAttachment = XCTAttachment(string: observation)
        observationAttachment.name = "CloudKitDevelopment transport boundary"
        observationAttachment.lifetime = .keepAlways
        add(observationAttachment)
        XCTAssertTrue(
            !keysOff.contains(keyValue) || configurationMissing || setupIssueExists ||
                operationError,
            "The real CloudKitDevelopment activation neither settled nor exposed an error. " +
                observation
        )

        let activationBlocked = configurationMissing || setupIssueExists
        if activationBlocked {
            XCTAssertFalse(
                app.buttons["settings.sync.now"].isEnabled,
                "A failed real transport activation must not enable manual Sync. \(observation)"
            )
            if setupIssueExists {
                XCTAssertFalse(
                    setupIssueValue.isEmpty,
                    "A typed setup issue must publish its bounded evidence value."
                )
            }
        } else {
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

    /// Reads off the main thread so a system paste-consent prompt can be answered.
    @MainActor
    private func readPasteboardString() -> String? {
        final class Box: @unchecked Sendable { var value: String? }
        let box = Box()
        let done = expectation(description: "pasteboard read")
        DispatchQueue.global(qos: .userInitiated).async {
            box.value = UIPasteboard.general.string
            done.fulfill()
        }
        let allow = XCUIApplication(bundleIdentifier: "com.apple.springboard").buttons.matching(
            NSPredicate(format: "label IN %@", ["Allow Paste", "Einsetzen erlauben"])
        ).firstMatch
        if allow.waitForExistence(timeout: 4) { allow.tap() }
        wait(for: [done], timeout: 10)
        return box.value
    }

    @MainActor
    private func attachScreenshot(named name: String, of app: XCUIApplication) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
