import XCTest

/// ADR 0012 section 4, MOB-EXT-01 (spike): with `-AhoiWebExtensionSpike`
/// the bundled MV3 test extension (`Sources/AhoiMobileCore/
/// WebExtensionSpike`) injects its content script into a normal page and
/// its `declarativeNetRequest` rule blocks a matching script request.
///
/// On pages whose query contains `ahoi-spike-check` the content script
/// probes two scripts on the page's own origin that differ only in the
/// blocked path, and writes a visible report into the page. Without the
/// launch argument the runtime is off and no report appears.
final class MobileWebExtensionSpikeUITests: MobileBrowserUITestCase {
    private let spikeArgument = "-AhoiWebExtensionSpike"

    /// Focused visible prerequisite for the final normal-after-private journey.
    @MainActor
    func testPrivateCloseAddressEditorAfterNormalSelection() throws {
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument, "-AhoiSpikeReduceMotion"])
        defer { app.terminate() }
        openExamplePage(try checkURL("private-close-address"), in: app)
        app.buttons["browser.more"].tap()
        let create = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(waitForHittable(create, timeout: 5))
        create.tap()
        openExamplePage(try checkURL("private-close-entry"), in: app)
        showTabSwitcher(app)
        let close = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@", "browser.tab-close.")).firstMatch
        XCTAssertTrue(waitForHittable(close, timeout: 5))
        close.tap()
        XCTAssertTrue(app.descendants(matching: .any)["browser.tabs.list"].waitForNonExistence(timeout: 5))
        showTabSwitcher(app)
        let normal = switcherRow(containing: "private-close-address", in: app)
        XCTAssertTrue(waitForHittable(normal, timeout: 5))
        normal.tap()
        XCTAssertTrue(app.descendants(matching: .any)["browser.tabs.list"].waitForNonExistence(timeout: 5))
        XCTAssertTrue(waitForAddressValue(containing: "private-close-address", in: app))
        attachEvidence(app, "private-close-before-address")
        openExamplePage(try checkURL("private-close-address-final"), in: app)
        attachEvidence(app, "private-close-after-address")
    }

    @MainActor
    func testDefaultPopupRefreshCloseAndReopenPreservesStorage() throws {
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument, "-AhoiSpikeReduceMotion"])
        defer { app.terminate() }
        let token = UUID().uuidString.lowercased().prefix(8)
        openExamplePage(try checkURL("\(token)-start"), in: app)
        chooseSite("Allow this site for 180 seconds", in: app)
        openExamplePage(try checkURL("\(token)-allowed"), in: app)
        let report = try assertProbe(token: "\(token)-allowed", allowed: true, in: app)
        let visits = try XCTUnwrap(visitCount(in: report.label))
        attachEvidence(app, "popup-01-granted-page-and-storage")
        openSpikeAction("popup", in: app)
        let close = app.buttons["spike.popup.close"]
        XCTAssertTrue(close.waitForExistence(timeout: 8), "The public WebKit popup must appear.")
        XCTAssertFalse(app.buttons["browser.actions.done"].exists)
        let count = app.webViews.staticTexts.matching(NSPredicate(
            format: "label CONTAINS %@", "Ahoi Spike:"
        )).firstMatch
        XCTAssertTrue(count.waitForExistence(timeout: 5))
        XCTAssertTrue(count.label.contains("\(visits) visits"), count.label)
        let refresh = app.webViews.buttons["Refresh"]
        XCTAssertTrue(waitForHittable(refresh, timeout: 5))
        refresh.tap()
        XCTAssertTrue(app.webViews.staticTexts["Refresh 1"].waitForExistence(timeout: 5))
        attachEvidence(app, "popup-02-real-refresh")
        close.tap()
        XCTAssertTrue(close.waitForNonExistence(timeout: 5))
        openSpikeAction("popup", in: app)
        XCTAssertTrue(close.waitForExistence(timeout: 8))
        XCTAssertTrue(count.waitForExistence(timeout: 5))
        XCTAssertTrue(count.label.contains("\(visits) visits"), count.label)
        attachEvidence(app, "popup-03-reopen-same-storage")
        XCUIDevice.shared.press(.home)
        app.activate()
        XCTAssertTrue(close.waitForNonExistence(timeout: 5), "Scene invalidation must close the popup.")
        openExamplePage(try checkURL("\(token)-scene-suspended"), in: app)
        let suspended = try assertProbe(token: "\(token)-scene-suspended", allowed: true,
                                        blocking: false, in: app)
        XCTAssertGreaterThan(visitCount(in: suspended.label) ?? 0, visits)
        attachEvidence(app, "popup-scene-suspended-independent-page-probe")
        openSpikeAction("popup", in: app)
        XCTAssertTrue(close.waitForExistence(timeout: 8))
        attachEvidence(app, "popup-04-reopen-after-scene-invalidation")
        app.buttons["spike.popup.unload"].tap()
        XCTAssertTrue(close.waitForNonExistence(timeout: 5))
        openExamplePage(try checkURL("\(token)-unloaded"), in: app)
        _ = try assertProbe(token: "\(token)-unloaded", allowed: false, in: app)
        attachEvidence(app, "popup-05-context-unloaded-no-late-popup")
    }

    @MainActor
    func testSiteConsentDenyAllowOtherHostExpiryRevokeAndPrivate() throws {
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument, "-AhoiSpikeReduceMotion"])
        defer { app.terminate() }
        let token = UUID().uuidString.lowercased().prefix(8)
        openExamplePage(try checkURL("\(token)-initial"), in: app)
        _ = try assertProbe(token: "\(token)-initial", allowed: false, in: app)
        chooseSite("Deny this site", in: app)
        openExamplePage(try checkURL("\(token)-deny"), in: app)
        _ = try assertProbe(token: "\(token)-deny", allowed: false, in: app)
        attachEvidence(app, "consent-01-denied-independent-page-probe")
        chooseSite("Allow this site for 180 seconds", in: app)
        let grantedAt = Date()
        openExamplePage(try checkURL("\(token)-allow"), in: app)
        let allowed = try assertProbe(token: "\(token)-allow", allowed: true, in: app)
        let visits = try XCTUnwrap(visitCount(in: allowed.label))
        attachEvidence(app, "consent-02-allowed-independent-page-probe")
        openExamplePage(try XCTUnwrap(URL(string: "https://example.org/?ahoi-spike-check=\(token)-other")), in: app)
        _ = try assertProbe(token: "\(token)-other", allowed: false, in: app)
        attachEvidence(app, "consent-03-other-host-ungranted")
        // Observe the real Date expiry; no permission-state injection or clock override.
        let remaining = max(0, 182 - Date().timeIntervalSince(grantedAt))
        RunLoop.current.run(until: Date().addingTimeInterval(remaining))
        openExamplePage(try checkURL("\(token)-expired"), in: app)
        _ = try assertProbe(token: "\(token)-expired", allowed: false, in: app)
        attachEvidence(app, "consent-04-expired-independent-page-probe")
        chooseSite("Allow this site for 180 seconds", in: app)
        openExamplePage(try checkURL("\(token)-again"), in: app)
        let again = try assertProbe(token: "\(token)-again", allowed: true, in: app)
        // Preserve the SDK27 nonpersistent-reload failure and still collect the
        // independent revoke/private evidence below. This assertion remains fatal
        // to the test result; only immediate termination is suspended here.
        continueAfterFailure = true
        XCTAssertGreaterThan(visitCount(in: again.label) ?? 0, visits)
        continueAfterFailure = false
        chooseSite("Revoke this site", in: app)
        app.buttons["browser.more"].tap()
        let newTab = app.buttons["browser.actions.new-tab"]
        XCTAssertTrue(waitForHittable(newTab, timeout: 5))
        newTab.tap()
        openExamplePage(try checkURL("\(token)-revoked"), in: app)
        _ = try assertProbe(token: "\(token)-revoked", allowed: false, in: app)
        attachEvidence(app, "consent-05-revoked-independent-page-probe")
        chooseSite("Allow this site for 180 seconds", in: app)
        app.buttons["browser.more"].tap()
        let privateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(waitForHittable(privateTab, timeout: 5))
        privateTab.tap()
        openExamplePage(try checkURL("\(token)-private"), in: app)
        _ = try assertProbe(token: "\(token)-private", allowed: false, in: app)
        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.actions.done"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["browser.actions.spike.popup"].exists)
        XCTAssertFalse(app.buttons["browser.actions.spike.permission"].exists)
        attachEvidence(app, "consent-06-private-no-attachment-or-action")
        app.buttons["browser.actions.done"].tap()
        showTabSwitcher(app)
        let privateClose = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@", "browser.tab-close."
        )).firstMatch
        XCTAssertTrue(waitForHittable(privateClose, timeout: 5))
        privateClose.tap()
        // Ending the last private session dismisses its presentations. Reopen
        // the real switcher, which now follows the selected normal tab.
        XCTAssertTrue(app.descendants(matching: .any)["browser.tabs.list"].waitForNonExistence(timeout: 5))
        showTabSwitcher(app)
        let normal = switcherRow(containing: "\(token)-revoked", in: app)
        XCTAssertTrue(waitForHittable(normal, timeout: 5))
        normal.tap()
        XCTAssertTrue(app.descendants(matching: .any)["browser.tabs.list"].waitForNonExistence(timeout: 5))
        XCTAssertTrue(waitForAddressValue(containing: "\(token)-revoked", in: app))
        openExamplePage(try checkURL("\(token)-normal-after-private"), in: app)
        _ = try assertProbe(token: "\(token)-normal-after-private", allowed: true, in: app)
        attachEvidence(app, "consent-07-normal-grant-survives-private-close")
    }

    @MainActor
    private func openSpikeAction(_ action: String, in app: XCUIApplication) {
        app.buttons["browser.more"].tap()
        let button = app.buttons["browser.actions.spike.\(action)"]
        XCTAssertTrue(waitForHittable(button, timeout: 5))
        button.tap()
    }

    @MainActor
    private func chooseSite(_ choice: String, in app: XCUIApplication) {
        openSpikeAction("permission", in: app)
        let alert = app.alerts["Ahoi Spike · Site permission"]
        XCTAssertTrue(alert.waitForExistence(timeout: 5))
        attachEvidence(app, "consent-native-\(choice)")
        alert.buttons[choice].tap()
        XCTAssertTrue(alert.waitForNonExistence(timeout: 5))
    }

    @MainActor
    @discardableResult
    private func assertProbe(token: String, allowed: Bool, blocking: Bool? = nil,
                             in app: XCUIApplication) throws -> XCUIElement {
        let probe = app.webViews.descendants(matching: .any).matching(NSPredicate(
            format: "label CONTAINS %@ AND label CONTAINS %@", "Ahoi PageProbe", "token=\(token)"
        )).firstMatch
        XCTAssertTrue(probe.waitForExistence(timeout: Self.adr0012NetworkTimeout))
        let label = probe.label
        XCTAssertTrue(label.contains("control=loaded"), label)
        XCTAssertTrue(label.contains(allowed ? "content=1" : "content=absent"), label)
        XCTAssertTrue(label.contains((blocking ?? allowed) ? "rule=blocked" : "rule=not-blocked"), label)
        if !allowed { XCTAssertTrue(label.contains("visits=absent"), label) }
        return probe
    }

    @MainActor
    func testRuntimeStaysOffWithoutTheLaunchArgument() throws {
        let token = UUID().uuidString.lowercased().prefix(8)
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer { app.terminate() }

        openExamplePage(try checkURL("\(token)-off"), in: app)
        // Give a would-be content script the same time it gets above.
        RunLoop.current.run(until: Date().addingTimeInterval(4))
        XCTAssertFalse(
            spikeReport(in: app).exists,
            "Without the launch argument no extension may run."
        )
        attachEvidence(app, "spike-01-off-without-argument")
    }

    @MainActor
    func testFilesPickerCancellationDoesNotLoadExtension() throws {
        try requireFilesJourneyOptIn()
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument, "-AhoiSpikeReduceMotion"])
        defer { app.terminate() }
        openSpikeSettings(in: app)
        let load = app.buttons["settings.extensions.spike.import-folder"]
        load.tap()
        let picker = app.otherElements["Browse View (Picker)"]
        XCTAssertTrue(picker.waitForExistence(timeout: 8))
        attachEvidence(app, "spike-files-picker-before-cancel")
        attachTree(app, "spike-files-picker-tree")
        // iOS27 reports the presenting sheet's hidden Cancel behind Files.
        // Dismiss the visible native picker sheet through its top edge.
        picker.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.01))
            .press(forDuration: 0.1, thenDragTo: picker.coordinate(
                withNormalizedOffset: CGVector(dx: 0.5, dy: 0.9)
            ))
        XCTAssertTrue(picker.waitForNonExistence(timeout: 5))
        XCTAssertTrue(waitForHittable(load, timeout: 5))
        XCTAssertTrue(load.isEnabled)
        XCTAssertFalse(app.buttons["settings.extensions.spike.unload-folder"].exists)
        attachEvidence(app, "spike-files-cancel-no-import")
    }

    @MainActor
    func testFilesPickerLoadsAndUnloadsReviewedFixture() throws {
        try requireFilesJourneyOptIn()
        // The runner stages the five reviewed fixture files in this owned
        // simulator's Documents directory. Selection still goes through Files;
        // no launch argument, URL injection or runtime call imports it.
        let folder = try XCTUnwrap(
            ProcessInfo.processInfo.environment["AHOI_MOBILE_SPIKE_FILES_FOLDER"]
        )
        XCTAssertFalse(folder.isEmpty)
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument, "-AhoiSpikeReduceMotion"])
        defer { app.terminate() }
        openSpikeSettings(in: app)
        let load = app.buttons["settings.extensions.spike.import-folder"]
        load.tap()
        let picker = app.otherElements["Browse View (Picker)"]
        XCTAssertTrue(picker.waitForExistence(timeout: 8), "The native Files picker must be ready.")
        attachEvidence(app, "spike-files-picker-before-selection")
        attachTree(app, "spike-files-picker-before-selection-tree")

        let chosenFolder = app.cells["\(folder), Folder"].firstMatch
        if !chosenFolder.waitForExistence(timeout: 3) {
            let local = app.descendants(matching: .any).matching(NSPredicate(
                format: "label IN %@", ["On My iPhone", "Auf meinem iPhone"]
            )).firstMatch
            if !local.exists {
                let browse = app.buttons.matching(NSPredicate(
                    format: "label IN %@", ["Browse", "Durchsuchen"]
                )).firstMatch
                XCTAssertTrue(waitForHittable(browse, timeout: 5))
                browse.tap()
            }
            XCTAssertTrue(waitForHittable(local, timeout: 5))
            local.tap()
            let documents = app.staticTexts["AhoiBrowser"].firstMatch
            XCTAssertTrue(waitForHittable(documents, timeout: 5))
            documents.tap()
        }
        XCTAssertTrue(waitForHittable(chosenFolder, timeout: 5))
        chosenFolder.doubleTap()
        // The picker can still be displaying the parent while its provider
        // opens the folder. Open must confirm the fixture, never that parent.
        // Xcode27's Files navbar has a fixed identifier; the browsing root
        // carries the full current folder title even when the toolbar truncates it.
        let folderRoot = app.otherElements.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND identifier CONTAINS %@",
            "DOC.browsingRoot Source:", "Title: \(folder)"
        )).firstMatch
        let enteredFolder = folderRoot.waitForExistence(timeout: 5)
        attachTree(app, "spike-files-selected-folder-tree")
        XCTAssertTrue(enteredFolder, "Files must enter the reviewed fixture folder before Open.")
        let open = app.buttons["DOCPicker.actionButton"]
        XCTAssertTrue(waitForHittable(open, timeout: 5))
        attachEvidence(app, "spike-files-selected-folder")
        open.tap()

        let loaded = app.staticTexts.matching(NSPredicate(
            format: "identifier == %@ AND label == %@",
            "settings.extensions.spike.files-status", "Unpacked test extension loaded"
        )).firstMatch
        XCTAssertTrue(loaded.waitForExistence(timeout: 8))
        XCTAssertFalse(load.isEnabled)
        let unload = app.buttons["settings.extensions.spike.unload-folder"]
        XCTAssertTrue(waitForHittable(unload, timeout: 5))
        attachEvidence(app, "spike-files-context-loaded")
        unload.tap()
        let unloaded = app.staticTexts.matching(NSPredicate(
            format: "identifier == %@ AND label == %@",
            "settings.extensions.spike.files-status", "Files test extension unloaded"
        )).firstMatch
        XCTAssertTrue(unloaded.waitForExistence(timeout: 5))
        XCTAssertTrue(load.isEnabled)
        XCTAssertFalse(unload.exists)
        attachEvidence(app, "spike-files-context-unloaded")
    }

    @MainActor
    private func openSpikeSettings(in app: XCUIApplication) {
        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.actions.done"].waitForExistence(timeout: 4))
        let settings = app.buttons["browser.actions.settings"]
        reveal(settings, in: app)
        settings.tap()
        XCTAssertTrue(app.buttons["settings.done"].waitForExistence(timeout: 5))
        let load = app.buttons["settings.extensions.spike.import-folder"]
        reveal(load, in: app)
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        // Finite settings list, matching the existing native settings journeys.
        for _ in 0..<10 {
            if element.exists && element.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(waitForHittable(element, timeout: 3))
    }

    private func requireFilesJourneyOptIn() throws {
        guard ProcessInfo.processInfo.environment["AHOI_MOBILE_REAL_E2E"] == "1" else {
            throw XCTSkip("Files journeys need the exact-candidate E2E binding and owned device.")
        }
    }

    private func checkURL(_ value: String) throws -> URL {
        try XCTUnwrap(URL(
            string: "https://example.com/?ahoi-spike-check=\(value)"
        ))
    }

    @MainActor
    private func spikeReport(
        in app: XCUIApplication,
        containing text: String = "Ahoi Spike report"
    ) -> XCUIElement {
        app.webViews.descendants(matching: .any).matching(NSPredicate(
            format: "label CONTAINS %@ AND label CONTAINS %@",
            "Ahoi Spike report", text
        )).firstMatch
    }

    private func visitCount(in label: String) -> Int? {
        guard let range = label.range(of: "visits=") else { return nil }
        return Int(label[range.upperBound...].prefix { $0.isNumber })
    }
}
