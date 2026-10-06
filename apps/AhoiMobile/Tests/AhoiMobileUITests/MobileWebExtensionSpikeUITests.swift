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

    @MainActor
    func testBundledExtensionInjectsContentScriptAndBlocksByRule() throws {
        let token = UUID().uuidString.lowercased().prefix(8)
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture", spikeArgument,
        ])
        defer { app.terminate() }

        let first = try checkURL("\(token)-1")
        openExamplePage(first, in: app)
        let report = spikeReport(in: app)
        XCTAssertTrue(
            report.waitForExistence(timeout: Self.adr0012NetworkTimeout),
            "The extension's content script must write its report."
        )
        if !report.exists { attachTree(app, "spike-report-missing") }
        let label = report.label
        XCTAssertTrue(label.contains("content-script=ran"), label)
        XCTAssertTrue(
            label.contains("control=loaded"),
            "The unblocked control script must load: \(label)"
        )
        XCTAssertTrue(
            label.contains("rule=blocked"),
            "The declarativeNetRequest rule must block its script: \(label)"
        )
        attachEvidence(app, "spike-01-content-script-and-rule")

        // Extension storage keeps its count within the session.
        let second = try checkURL("\(token)-2")
        openExamplePage(second, in: app)
        let again = spikeReport(in: app, containing: "visits=")
        XCTAssertTrue(again.waitForExistence(
            timeout: Self.adr0012NetworkTimeout
        ))
        let visits = visitCount(in: again.label)
        XCTAssertNotNil(visits, again.label)
        XCTAssertGreaterThanOrEqual(
            visits ?? 0, 2,
            "storage.local must count across page loads: \(again.label)"
        )
        attachEvidence(app, "spike-01-storage-second-visit")
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
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument])
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
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture", spikeArgument])
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
