import Foundation
import XCTest

/// Owner-approved physical-device CloudKit Development journey. It drives the
/// normal product UI only: explicit Sync opt-in in Settings, the real key and
/// CloudKit lifecycle, one recognizable normal tab, then a visible bounded
/// Sync pass. It never deletes keys, records or zones and is skipped unless the
/// runner passes `AHOI_REAL_DEVICE_CLOUDKIT_SYNC=1` and an exact tab URL
/// (`TEST_RUNNER_` prefixed for xcodebuild). The installed app must be a
/// CloudKitDevelopment build bound to an isolated acceptance scope.
final class MobileRealDeviceCloudKitSyncUITests: MobileBrowserUITestCase {
    private static let readyStates: Set<String> = ["Ready", "Bereit"]
    private static let keyReadyStates: Set<String> = [
        "Encryption ready", "Verschlüsselung bereit",
    ]
    private static let syncedDetails: Set<String> = ["Synced", "Synchronisiert"]
    private var transitions: [String] = []
    private var screenshotIndex = 0

    @MainActor
    func testRealDeviceOptInReachesReadyAndUploadsRecognizableTab() throws {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AHOI_REAL_DEVICE_CLOUDKIT_SYNC"] == "1" else {
            throw XCTSkip("Set AHOI_REAL_DEVICE_CLOUDKIT_SYNC=1 for the physical-device journey.")
        }
        guard let rawURL = environment["AHOI_REAL_DEVICE_SYNC_TAB_URL"],
              let tabURL = URL(string: rawURL), tabURL.scheme == "https" else {
            XCTFail("AHOI_REAL_DEVICE_SYNC_TAB_URL must be an exact https URL.")
            return
        }
        let readyTimeout = TimeInterval(
            environment["AHOI_REAL_DEVICE_SYNC_READY_TIMEOUT"].flatMap(Double.init) ?? 300
        )
        record("tab-url", rawURL)
        addTeardownBlock { [weak self] in self?.attachTransitions() }

        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(
            app.buttons["browser.address"].waitForExistence(timeout: 20),
            "The fresh scoped browser must present its address control."
        )
        capture("fresh-launch", app)

        // 1. Explicit opt-in through the visible Settings toggle.
        openSettings(in: app)
        let toggle = app.switches["settings.sync.enabled"]
        reveal(toggle, in: app)
        record("sync-toggle-before", "\(toggle.value ?? "<nil>")")
        capture("settings-before-opt-in", app)
        if (toggle.value as? String) != "1" {
            tapSwitch(toggle)
        }
        XCTAssertTrue(
            wait(for: toggle, toHaveValue: "1", timeout: 5),
            "The visible CloudKit Sync toggle must turn on."
        )
        record("sync-toggle-after", "\(toggle.value ?? "<nil>")")

        // 2. Observe the real state machine until Ready + encryption ready.
        let reachedReady = observeSyncStatus(
            in: app,
            timeout: readyTimeout,
            label: "opt-in"
        ) { state, key, _ in
            Self.readyStates.contains(state) && Self.keyReadyStates.contains(key)
        }
        capture("after-opt-in", app)
        guard reachedReady else {
            XCTFail("Sync did not reach Ready + encryption ready: \(transitions.suffix(6))")
            return
        }

        // 3. Create one recognizable normal tab through the visible address UI.
        closeSettings(in: app)
        navigateOnDevice(to: tabURL, in: app)
        // example.com localizes its body per visitor; its single link is stable.
        XCTAssertTrue(
            app.webViews.links.firstMatch.waitForExistence(timeout: 30),
            "The recognizable test page must load its content."
        )
        capture("test-tab-loaded", app)

        // Normal lifecycle publish path: background and foreground once.
        XCUIDevice.shared.press(.home)
        sleepRunLoop(2)
        app.activate()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 10))
        assertAddress(tabURL, containsOrigin: origin(of: tabURL), in: app)

        // 4. Explicit visible Sync pass; "Synced" is only set after a bounded
        //    pass that drained every pending outbound record.
        openSettings(in: app)
        let syncNow = app.buttons["settings.sync.now"]
        reveal(syncNow, in: app)
        XCTAssertTrue(syncNow.isEnabled, "Sync now must be enabled once configured.")
        transitions.append("\(timestamp()) upload: tap settings.sync.now")
        syncNow.tap()
        let synced = observeSyncStatus(
            in: app,
            timeout: readyTimeout,
            label: "upload",
            minimumObservation: 3
        ) { state, key, detail in
            Self.readyStates.contains(state) && Self.keyReadyStates.contains(key) &&
                Self.syncedDetails.contains(detail)
        }
        revealTop(in: app)
        capture("after-sync-now", app)
        recordSettingsTexts(in: app)
        XCTAssertTrue(
            synced,
            "The bounded Sync pass must visibly finish as Ready + Synced: \(transitions.suffix(6))"
        )
    }

    // MARK: - Status observation

    @MainActor
    private func observeSyncStatus(
        in app: XCUIApplication,
        timeout: TimeInterval,
        label: String,
        minimumObservation: TimeInterval = 0,
        until done: (String, String, String) -> Bool
    ) -> Bool {
        let start = Date()
        let deadline = start.addingTimeInterval(timeout)
        var last = ""
        repeat {
            let state = value(of: "settings.sync.state", in: app)
            let key = value(of: "settings.sync.key-lifecycle", in: app)
            let detail = syncDetail(in: app)
            let issue = footerIssue(in: app)
            let line = "state=\(state) | encryption=\(key) | detail=\(detail) | footer=\(issue)"
            if line != last {
                transitions.append("\(timestamp()) \(label): \(line)")
                last = line
                if transitions.count <= 40 { capture("\(label)-transition", app) }
            }
            if Date().timeIntervalSince(start) >= minimumObservation,
               done(state, key, detail) {
                return true
            }
            sleepRunLoop(1)
        } while Date() < deadline
        return false
    }

    @MainActor
    private func value(of identifier: String, in app: XCUIApplication) -> String {
        let element = app.descendants(matching: .any)[identifier]
        guard element.exists else { return "<absent>" }
        return element.value as? String ?? "<nil>"
    }

    /// The provider detail is the caption directly below the lifecycle rows.
    @MainActor
    private func syncDetail(in app: XCUIApplication) -> String {
        let known = [
            "Synced", "Synchronisiert", "Not synced yet", "Noch nicht synchronisiert",
            "Syncing with CloudKit", "Wird mit CloudKit synchronisiert",
            "Changes remain pending; another sync is required",
            "Änderungen sind noch ausstehend; eine weitere Synchronisierung ist erforderlich",
            "Preparing sync zone", "Sync-Zone wird vorbereitet",
        ]
        for text in known where app.staticTexts[text].exists {
            return text
        }
        return "<none>"
    }

    @MainActor
    private func footerIssue(in app: XCUIApplication) -> String {
        for identifier in [
            "settings.sync.setup-issue", "settings.sync.configuration-missing",
            "settings.sync.setup-pending", "settings.sync.rotation-paused",
        ] {
            let element = app.descendants(matching: .any)[identifier]
            if element.exists {
                return "\(identifier)=\(element.label) [\(element.value ?? "")]"
            }
        }
        return "<none>"
    }

    // MARK: - Address input

    /// A physical keyboard drops characters when a whole URL is injected at
    /// once while the address suggestions re-render. Type one character at a
    /// time and wait for the visible field to confirm each one.
    @MainActor
    private func navigateOnDevice(to url: URL, in app: XCUIApplication) {
        openAddressEditor(in: app)
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        clearAddressEditor(field, in: app)
        let expected = url.absoluteString
        var typed = ""
        for character in expected {
            let target = typed + String(character)
            var confirmed = false
            for _ in 0..<3 {
                let current = fieldValue(field)
                if current == target { confirmed = true; break }
                guard current == typed else { break }
                field.typeText(String(character))
                confirmed = waitForFieldValue(field, target, timeout: 3)
                if confirmed { break }
            }
            guard confirmed else {
                XCTFail("Address input diverged after \(typed); field shows \(fieldValue(field)).")
                return
            }
            typed = target
        }
        transitions.append("\(timestamp()) address typed: \(fieldValue(field))")
        let navigate = app.buttons["browser.search.navigate"]
        XCTAssertTrue(waitForHittable(navigate, timeout: 5))
        navigate.tap()
        XCTAssertTrue(field.waitForNonExistence(timeout: 8))
        assertAddress(url, containsOrigin: origin(of: url), in: app)
    }

    @MainActor
    private func fieldValue(_ field: XCUIElement) -> String {
        let raw = field.value as? String ?? ""
        if let placeholder = field.placeholderValue, raw == placeholder { return "" }
        return raw
    }

    @MainActor
    private func waitForFieldValue(
        _ field: XCUIElement, _ expected: String, timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if fieldValue(field) == expected { return true }
            sleepRunLoop(0.1)
        } while Date() < deadline
        return fieldValue(field) == expected
    }

    // MARK: - Settings navigation

    @MainActor
    private func openSettings(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: 10))
        more.tap()
        let actionsList = app.descendants(matching: .any)["browser.actions.list"]
        XCTAssertTrue(actionsList.waitForExistence(timeout: 5))
        let settings = app.buttons["browser.actions.settings"]
        for _ in 0..<8 where !(settings.exists && settings.isHittable) {
            actionsList.swipeUp()
        }
        XCTAssertTrue(waitForHittable(settings, timeout: 3))
        settings.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["settings.form"].waitForExistence(timeout: 5)
        )
    }

    @MainActor
    private func closeSettings(in app: XCUIApplication) {
        let done = app.buttons["settings.done"]
        XCTAssertTrue(waitForHittable(done, timeout: 5))
        done.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["settings.form"].waitForNonExistence(timeout: 5)
        )
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        let form = app.descendants(matching: .any)["settings.form"]
        for _ in 0..<12 {
            if element.exists, element.isHittable { return }
            form.swipeUp()
        }
        XCTAssertTrue(element.waitForExistence(timeout: 2), "\(element) must exist.")
    }

    /// Keep the Sync section on screen for the evidence screenshot.
    @MainActor
    private func revealTop(in app: XCUIApplication) {
        reveal(app.descendants(matching: .any)["settings.sync.state"], in: app)
    }

    @MainActor
    private func tapSwitch(_ toggle: XCUIElement) {
        // SwiftUI rows expose the whole row; tap the switch knob itself.
        let inner = toggle.switches.firstMatch
        if inner.exists, inner.isHittable {
            inner.tap()
        } else {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.93, dy: 0.5)).tap()
        }
    }

    @MainActor
    private func wait(
        for element: XCUIElement,
        toHaveValue expected: String,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", expected),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    // MARK: - Evidence

    @MainActor
    private func recordSettingsTexts(in app: XCUIApplication) {
        let form = app.descendants(matching: .any)["settings.form"]
        let labels = form.staticTexts.allElementsBoundByIndex
            .filter { $0.exists && $0.isHittable }
            .map(\.label)
        record("settings-visible-texts", labels.joined(separator: "\n"))
    }

    @MainActor
    private func capture(_ name: String, _ app: XCUIApplication) {
        screenshotIndex += 1
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = String(format: "%02d-%@", screenshotIndex, name)
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func record(_ name: String, _ text: String) {
        let attachment = XCTAttachment(string: text)
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func attachTransitions() {
        record("sync-status-transitions", transitions.joined(separator: "\n"))
    }

    private func timestamp() -> String {
        ISO8601DateFormatter().string(from: Date())
    }

    private func sleepRunLoop(_ seconds: TimeInterval) {
        RunLoop.current.run(until: Date().addingTimeInterval(seconds))
    }
}
