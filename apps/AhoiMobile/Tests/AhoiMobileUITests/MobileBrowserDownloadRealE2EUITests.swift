import Foundation
import XCTest

final class MobileBrowserDownloadRealE2EUITests: MobileBrowserRealE2ETestCase {
    private let rowIdentifierPrefix = "browser.downloads.row."
    private let statusIdentifierPrefix = "browser.downloads.status."
    private let cancelIdentifierPrefix = "browser.downloads.cancel."
    private let retryIdentifierPrefix = "browser.downloads.retry."

    @MainActor
    func testCompletedDownloadExposesQuickLookAndShareControls() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        navigate(to: fixture.url(path: "/download-upload"), in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["Download, pause/resume, upload and DnD"]
                .waitForExistence(timeout: 8)
        )
        clearFinishedDownloads(in: app)

        let download = webView.links["Download deterministic payload"]
        XCTAssertTrue(download.waitForExistence(timeout: 5))
        download.tap()
        openDownloads(in: app)

        let status = firstElement(withIdentifierPrefix: statusIdentifierPrefix, in: app)
        XCTAssertTrue(status.waitForExistence(timeout: 8))
        XCTAssertTrue(
            waitForLabel(in: ["Downloaded", "Geladen"], of: status, timeout: 20),
            "The deterministic payload must reach a visible completed state."
        )
        let identifierSuffix = try identifierSuffix(
            from: status,
            prefix: statusIdentifierPrefix
        )
        let savedFilenamePattern = "ahoi-range(-[0-9]+)?\\.bin"
        let row = app.descendants(matching: .any)[
            "browser.downloads.row.\(identifierSuffix)"
        ]
        XCTAssertTrue(row.staticTexts.matching(NSPredicate(
            format: "label MATCHES[c] %@",
            savedFilenamePattern
        )).firstMatch.waitForExistence(timeout: 3))
        let open = app.buttons["browser.downloads.open.\(identifierSuffix)"]
        let share = app.descendants(matching: .any)[
            "browser.downloads.share.\(identifierSuffix)"
        ]
        XCTAssertTrue(open.waitForExistence(timeout: 3))
        XCTAssertTrue(open.isHittable)
        XCTAssertTrue(share.waitForExistence(timeout: 3))
        XCTAssertTrue(share.isHittable)

        share.tap()
        let previewClose = app.buttons["QLOverlayDoneButtonAccessibilityIdentifier"]
        XCTAssertTrue(
            previewClose.waitForExistence(timeout: 8),
            "Sharing a file URL must enter Apple's native file-preview surface."
        )
        let previewShare = app.buttons["QLOverlayDefaultActionButtonAccessibilityIdentifier"]
        let localizedPreviewShare = app.buttons.matching(NSPredicate(
            format: "label IN %@",
            ["Share", "Teilen"]
        )).firstMatch
        let shareFromPreview = previewShare.waitForExistence(timeout: 3)
            ? previewShare
            : localizedPreviewShare
        XCTAssertTrue(shareFromPreview.waitForExistence(timeout: 3))
        XCTAssertTrue(shareFromPreview.isHittable)
        shareFromPreview.tap()

        let activity = app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier CONTAINS[c] %@",
            "Activity"
        )).firstMatch
        let nativeAction = app.descendants(matching: .any).matching(NSPredicate(
            format: "label IN %@",
            [
                "AirDrop", "Messages", "Nachrichten", "Mail", "Copy", "Kopieren",
                "Save to Files", "In Dateien sichern",
            ]
        )).firstMatch
        XCTAssertTrue(
            activity.waitForExistence(timeout: 8) || nativeAction.waitForExistence(timeout: 2),
            "Tapping Share must present the native activity surface, not merely expose a button."
        )
        dismissActivitySurface(activity, nativeAction: nativeAction, in: app)
        XCTAssertTrue(previewClose.waitForExistence(timeout: 3))
        previewClose.tap()
        XCTAssertTrue(previewClose.waitForNonExistence(timeout: 5))
        XCTAssertTrue(open.waitForExistence(timeout: 3))
        XCTAssertTrue(open.isHittable)

        open.tap()
        let previewFilenamePattern = ".*ahoi-range(-[0-9]+)?(\\.bin)?.*"
        let previewTitle = app.navigationBars.staticTexts.matching(NSPredicate(
            format: "label MATCHES[c] %@",
            previewFilenamePattern
        )).firstMatch
        let titledPreviewBar = app.navigationBars.matching(NSPredicate(
            format: "label MATCHES[c] %@ OR identifier MATCHES[c] %@",
            previewFilenamePattern,
            previewFilenamePattern
        )).firstMatch
        XCTAssertTrue(
            previewTitle.waitForExistence(timeout: 8) ||
                titledPreviewBar.waitForExistence(timeout: 2),
            "Opening a completed download must present Quick Look with its collision-safe filename."
        )
    }

    @MainActor
    func testCancelledThrottledDownloadStaysTerminalWithoutOpenOrShare() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        navigate(to: fixture.url(path: "/download-upload"), in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["Download, pause/resume, upload and DnD"]
                .waitForExistence(timeout: 8)
        )
        clearFinishedDownloads(in: app)

        let download = webView.links["Large throttled ZIP"]
        XCTAssertTrue(download.waitForExistence(timeout: 5))
        download.tap()
        openDownloads(in: app)

        let cancel = firstElement(withIdentifierPrefix: cancelIdentifierPrefix, in: app)
        XCTAssertTrue(
            cancel.waitForExistence(timeout: 6),
            "The deliberately throttled fixture must remain cancellable in the visible sheet."
        )
        let identifierSuffix = try identifierSuffix(
            from: cancel,
            prefix: cancelIdentifierPrefix
        )
        cancel.tap()

        let status = app.descendants(matching: .any)[
            "browser.downloads.status.\(identifierSuffix)"
        ]
        XCTAssertTrue(status.waitForExistence(timeout: 3))
        XCTAssertTrue(
            waitForLabel(in: ["Cancelled", "Abgebrochen"], of: status, timeout: 5),
            "Cancelling must visibly enter the terminal cancelled state."
        )
        assertCancelledStateRemainsStable(
            status: status,
            identifierSuffix: identifierSuffix,
            in: app,
            duration: 12
        )
    }

    @MainActor
    func testInterruptedNormalDownloadCanRetryWithinTheSameProcess() async throws {
        let fixture = try await requireReachableFixture()
        try await resetFixture(fixture)
        let app = coldLaunchApplication()
        navigate(to: fixture.url(path: "/download-upload"), in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["Download, pause/resume, upload and DnD"]
                .waitForExistence(timeout: 8)
        )
        clearFinishedDownloads(in: app)

        let download = webView.links["Disconnect-once ZIP"]
        XCTAssertTrue(download.waitForExistence(timeout: 5))
        download.tap()
        openDownloads(in: app)

        let retry = firstElement(withIdentifierPrefix: retryIdentifierPrefix, in: app)
        XCTAssertTrue(
            retry.waitForExistence(timeout: 30),
            "The intentionally interrupted normal download must expose a visible retry action."
        )
        XCTAssertTrue(retry.isHittable)
        let failedSuffix = try identifierSuffix(from: retry, prefix: retryIdentifierPrefix)
        let failedStatus = app.descendants(matching: .any)[
            "browser.downloads.status.\(failedSuffix)"
        ]
        XCTAssertTrue(failedStatus.waitForExistence(timeout: 3))
        XCTAssertTrue(
            waitForLabel(
                in: [
                    "The download was interrupted. Try again.",
                    "Der Download wurde unterbrochen. Versuche es erneut.",
                ],
                of: failedStatus,
                timeout: 5
            )
        )

        retry.tap()
        XCTAssertTrue(retry.waitForNonExistence(timeout: 5))

        let completedStatus = firstElement(withIdentifierPrefix: statusIdentifierPrefix, in: app)
        XCTAssertTrue(completedStatus.waitForExistence(timeout: 5))
        XCTAssertTrue(
            waitForLabel(in: ["Downloaded", "Geladen"], of: completedStatus, timeout: 40),
            "Retry must issue a fresh request in the same process and visibly complete."
        )
        let completedSuffix = try identifierSuffix(
            from: completedStatus,
            prefix: statusIdentifierPrefix
        )
        XCTAssertTrue(
            app.buttons["browser.downloads.open.\(completedSuffix)"]
                .waitForExistence(timeout: 3)
        )
        XCTAssertFalse(
            app.descendants(matching: .any)[
                "browser.downloads.retry.\(completedSuffix)"
            ].exists
        )
    }

    @MainActor
    func testNormalDownloadKeepsTheActiveCookieSessionWithoutExposingValues() async throws {
        let fixture = try await requireReachableFixture()
        try await resetFixture(fixture)
        let app = coldLaunchApplication()
        navigate(to: fixture.url(path: "/cookies/set"), in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["Synthetic first-party cookies set."]
                .waitForExistence(timeout: 8)
        )
        navigate(to: fixture.url(path: "/download-upload"), in: app)
        XCTAssertTrue(
            webView.staticTexts["Download, pause/resume, upload and DnD"]
                .waitForExistence(timeout: 8)
        )
        clearFinishedDownloads(in: app)

        let download = webView.links["Download deterministic payload"]
        XCTAssertTrue(download.waitForExistence(timeout: 5))
        download.tap()
        openDownloads(in: app)
        let status = firstElement(withIdentifierPrefix: statusIdentifierPrefix, in: app)
        XCTAssertTrue(status.waitForExistence(timeout: 8))
        XCTAssertTrue(
            waitForLabel(in: ["Downloaded", "Geladen"], of: status, timeout: 20)
        )

        let receipts = try await fixtureReceipts(fixture)
        XCTAssertTrue(
            receipts.contains { receipt in
                guard receipt["path"] as? String == "/download/deterministic.bin",
                      receipt["method"] as? String == "GET",
                      let signals = receipt["requestSignals"] as? [String: Any] else {
                    return false
                }
                return signals["cookiePresent"] as? Bool == true
            },
            "The production WKDownload request must retain the active WebKit cookie session; "
                + "the fixture receipt records presence only and never exposes cookie values."
        )
    }

    @MainActor
    func testPrivateActiveDownloadLeavesNoRecoveryRowAfterRelaunch() async throws {
        let fixture = try await requireReachableFixture()
        try await resetFixture(fixture)
        let app = coldLaunchApplication()
        createPrivateTab(in: app)
        navigatePrivately(to: fixture.url(path: "/download-upload"), in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["Download, pause/resume, upload and DnD"]
                .waitForExistence(timeout: 8)
        )
        clearFinishedDownloads(in: app)
        let download = webView.links["Large throttled ZIP"]
        XCTAssertTrue(download.waitForExistence(timeout: 5))
        download.tap()
        openDownloads(in: app)
        XCTAssertTrue(
            firstElement(withIdentifierPrefix: cancelIdentifierPrefix, in: app)
                .waitForExistence(timeout: 6),
            "The private download must be visibly active before process termination."
        )

        relaunchExactCandidate(app)
        createPrivateTab(in: app)
        openDownloads(in: app)

        let row = firstElement(withIdentifierPrefix: rowIdentifierPrefix, in: app)
        let empty = app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["No Downloads", "Keine Downloads"]
        )).firstMatch
        XCTAssertTrue(empty.waitForExistence(timeout: 5))
        XCTAssertTrue(row.waitForNonExistence(timeout: 5))
        let stableDeadline = Date().addingTimeInterval(3)
        repeat {
            XCTAssertFalse(
                row.exists,
                "Private request state must not reappear after the process boundary."
            )
            try await Task.sleep(for: .milliseconds(100))
        } while Date() < stableDeadline
    }

    @MainActor
    private func resetFixture(_ fixture: FixtureContext) async throws {
        var request = URLRequest(url: fixture.url(path: "/__fixture/reset"))
        request.httpMethod = "POST"
        request.timeoutInterval = 3
        let (_, response) = try await URLSession.shared.data(for: request)
        XCTAssertEqual(
            (response as? HTTPURLResponse)?.statusCode,
            200,
            "The disconnect-once counter must reset before the visible retry journey."
        )
    }

    @MainActor
    private func fixtureReceipts(_ fixture: FixtureContext) async throws -> [[String: Any]] {
        var request = URLRequest(url: fixture.url(path: "/__fixture/receipts"))
        request.timeoutInterval = 3
        let (data, response) = try await URLSession.shared.data(for: request)
        XCTAssertEqual((response as? HTTPURLResponse)?.statusCode, 200)
        let root = try XCTUnwrap(
            try JSONSerialization.jsonObject(with: data) as? [String: Any]
        )
        return try XCTUnwrap(root["receipts"] as? [[String: Any]])
    }

    @MainActor
    private func createPrivateTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let newPrivateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(newPrivateTab.waitForExistence(timeout: 3))
        newPrivateTab.tap()
        XCTAssertTrue(
            app.buttons["browser.address.private"].waitForExistence(timeout: 3)
                || app.buttons["browser.focus-voyage.search"].waitForExistence(timeout: 3)
        )
    }

    @MainActor
    private func navigatePrivately(to url: URL, in app: XCUIApplication) {
        let privateAddress = app.buttons["browser.address.private"]
        let focusSearch = app.buttons["browser.focus-voyage.search"]
        let editorControl: XCUIElement
        if privateAddress.waitForExistence(timeout: 2) {
            editorControl = privateAddress
        } else {
            XCTAssertTrue(focusSearch.waitForExistence(timeout: 3))
            editorControl = focusSearch
        }
        editorControl.tap()

        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        clearAddressEditor(field, in: app)
        enterExactAddress(url.absoluteString, into: field, in: app)
        let submit = app.buttons["browser.search.navigate"]
        XCTAssertTrue(submit.waitForExistence(timeout: 3))
        submit.tap()
        XCTAssertTrue(field.waitForNonExistence(timeout: 5))

        XCTAssertTrue(privateAddress.waitForExistence(timeout: 5))
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value CONTAINS %@", origin(of: url)),
            object: privateAddress
        )
        XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 8), .completed)
    }

    @MainActor
    private func dismissActivitySurface(
        _ activity: XCUIElement,
        nativeAction: XCUIElement,
        in app: XCUIApplication
    ) {
        let close = app.buttons.matching(NSPredicate(
            format: "label IN %@",
            ["Close", "Schließen", "Cancel", "Abbrechen"]
        )).firstMatch
        if close.waitForExistence(timeout: 2), close.isHittable {
            close.tap()
        } else if activity.exists {
            activity.swipeDown()
        } else {
            app.swipeDown()
        }
        XCTAssertTrue(
            activity.waitForNonExistence(timeout: 5)
                && nativeAction.waitForNonExistence(timeout: 2),
            "The native activity surface must dismiss back to the downloads sheet."
        )
    }

    @MainActor
    private func clearFinishedDownloads(in app: XCUIApplication) {
        openDownloads(in: app)
        let clear = app.buttons["browser.downloads.clear"]
        if clear.waitForExistence(timeout: 2) {
            clear.tap()
            XCTAssertTrue(
                firstElement(withIdentifierPrefix: rowIdentifierPrefix, in: app)
                    .waitForNonExistence(timeout: 3)
            )
        }
        let done = app.buttons["browser.downloads.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3))
        done.tap()
        XCTAssertTrue(done.waitForNonExistence(timeout: 3))
    }

    @MainActor
    private func openDownloads(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let downloads = app.buttons["browser.actions.downloads"]
        for _ in 0..<4 {
            if downloads.waitForExistence(timeout: 1), downloads.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(downloads.waitForExistence(timeout: 3))
        XCTAssertTrue(downloads.isHittable)
        downloads.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.downloads.sheet"]
                .waitForExistence(timeout: 5)
        )
    }

    @MainActor
    private func firstElement(
        withIdentifierPrefix prefix: String,
        in app: XCUIApplication
    ) -> XCUIElement {
        app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            prefix
        )).firstMatch
    }

    @MainActor
    private func identifierSuffix(from element: XCUIElement, prefix: String) throws -> String {
        guard element.identifier.hasPrefix(prefix) else {
            XCTFail("The download control must expose its stable production identifier.")
            throw DownloadRealE2EContractError.missingStableDownloadIdentifier
        }
        return String(element.identifier.dropFirst(prefix.count))
    }

    @MainActor
    private func waitForLabel(
        in expectedLabels: [String],
        of element: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "label IN %@", expectedLabels),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    @MainActor
    private func assertCancelledStateRemainsStable(
        status: XCUIElement,
        identifierSuffix: String,
        in app: XCUIApplication,
        duration: TimeInterval,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let open = app.buttons["browser.downloads.open.\(identifierSuffix)"]
        let share = app.descendants(matching: .any)[
            "browser.downloads.share.\(identifierSuffix)"
        ]
        let deadline = Date().addingTimeInterval(duration)
        repeat {
            XCTAssertTrue(
                ["Cancelled", "Abgebrochen"].contains(status.label),
                "A delayed WebKit callback must not overwrite the cancelled terminal state.",
                file: file,
                line: line
            )
            XCTAssertFalse(open.exists, file: file, line: line)
            XCTAssertFalse(share.exists, file: file, line: line)
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
    }
}

private enum DownloadRealE2EContractError: Error {
    case missingStableDownloadIdentifier
}
