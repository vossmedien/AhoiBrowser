import Foundation
import XCTest

final class MobileBrowserDownloadRealE2EUITests: MobileBrowserRealE2ETestCase {
    private let rowIdentifierPrefix = "browser.downloads.row."
    private let statusIdentifierPrefix = "browser.downloads.status."
    private let cancelIdentifierPrefix = "browser.downloads.cancel."

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
