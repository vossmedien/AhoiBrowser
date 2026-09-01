import Foundation
import XCTest

final class MobileBrowserRealE2EUITests: MobileBrowserRealE2ETestCase {

    @MainActor
    func testColdLaunchAddressAndStrictHTTPSOriginAreVisible() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()

        navigate(to: fixture.baseURL, in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(webView.waitForExistence(timeout: 8))
        XCTAssertTrue(
            webView.staticTexts["AhoiBrowser local HTTPS E2E fixture"]
                .waitForExistence(timeout: 8),
            "The exact HTTPS fixture page must render in WebKit."
        )
        assertAddress(
            fixture.baseURL,
            containsOrigin: fixture.origin,
            in: app
        )
    }

    @MainActor
    func testNavigationBackForwardAndReloadUseVisibleBrowserControls() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        navigate(to: fixture.url(path: "/navigation"), in: app)

        let webView = app.webViews.firstMatch
        let navigationTitle = webView.staticTexts["Redirect and popup controls"]
        XCTAssertTrue(navigationTitle.waitForExistence(timeout: 8))

        let redirect = webView.links["Same-origin redirect"]
        XCTAssertTrue(redirect.waitForExistence(timeout: 3))
        redirect.tap()
        let popupTitle = webView.staticTexts["Synthetic popup"]
        XCTAssertTrue(popupTitle.waitForExistence(timeout: 8))

        let back = app.buttons["browser.back"]
        XCTAssertTrue(back.isEnabled)
        back.tap()
        XCTAssertTrue(navigationTitle.waitForExistence(timeout: 8))

        let forward = app.buttons["browser.forward"]
        XCTAssertTrue(forward.isEnabled)
        forward.tap()
        XCTAssertTrue(popupTitle.waitForExistence(timeout: 8))

        let reload = app.buttons["browser.reload-stop"]
        XCTAssertTrue(reload.waitForExistence(timeout: 3))
        XCTAssertTrue(reload.isHittable)
        reload.tap()
        XCTAssertTrue(popupTitle.waitForExistence(timeout: 8))
        assertAddress(
            fixture.url(path: "/popup?from=same"),
            containsOrigin: fixture.origin,
            in: app
        )
    }

    @MainActor
    func testSlowNavigationCanBeStoppedWhenFixturePublishesAControl() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        navigate(to: fixture.baseURL, in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["AhoiBrowser local HTTPS E2E fixture"]
                .waitForExistence(timeout: 8)
        )
        let slowControl = webView.links.matching(
            NSPredicate(format: "label CONTAINS[c] %@", "slow")
        ).firstMatch
        XCTAssertTrue(
            slowControl.waitForExistence(timeout: 3),
            "The contract-v2 HTTPS fixture must publish its slow-document control."
        )

        slowControl.tap()
        let reloadOrStop = app.buttons["browser.reload-stop"]
        XCTAssertTrue(
            waitForLabelContaining("Stop", of: reloadOrStop, timeout: 3),
            "A published slow route must expose the stop-loading state."
        )
        reloadOrStop.tap()
        XCTAssertTrue(
            waitForLabelNotContaining("Stop", of: reloadOrStop, timeout: 5),
            "Stopping must return the shared control to its non-loading state."
        )
    }

    @MainActor
    func testTabCreateSwitchRenameCloseUndoAndProcessRestore() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        let token = UUID().uuidString.lowercased()
        let tabURL = fixture.url(path: "/navigation?mobile-real-e2e=\(token)")
        let renamedTitle = "Restored voyage \(token.prefix(8))"

        createNormalTab(in: app)
        navigate(to: tabURL, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )

        openTabSwitcher(in: app)
        var row = tabRow(containing: tabURL.absoluteString, in: app)
        XCTAssertTrue(row.waitForExistence(timeout: 5))
        let rowSuffix = try tabIdentifierSuffix(from: row)
        let edit = app.buttons["browser.tabs.edit"]
        XCTAssertTrue(edit.waitForExistence(timeout: 3))
        edit.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.reorder-list"]
                .waitForExistence(timeout: 3)
        )
        let rename = app.buttons["browser.tab-rename.\(rowSuffix)"]
        XCTAssertTrue(rename.waitForExistence(timeout: 3))
        rename.tap()

        let renameField = app.textFields["browser.tabs.rename.field"]
        XCTAssertTrue(renameField.waitForExistence(timeout: 3))
        try replaceText(in: renameField, with: renamedTitle, app: app)
        app.buttons["browser.tabs.rename.save"].tap()
        XCTAssertTrue(renameField.waitForNonExistence(timeout: 3))
        row = app.descendants(matching: .any)["browser.tab-row.\(rowSuffix)"]
        XCTAssertTrue(row.waitForExistence(timeout: 4), "Rename must preserve the exact tab identity.")
        XCTAssertTrue(
            waitForLabelContaining(renamedTitle, of: row, timeout: 4),
            "The preserved tab row must visibly publish its new title."
        )
        XCTAssertFalse(
            app.buttons["browser.tabs.undo-close"].exists,
            "The rename action must never fall through to the adjacent close control."
        )
        app.buttons["browser.tabs.done"].tap()

        createNormalTab(in: app)
        openTabSwitcher(in: app)
        row = tabRow(containing: renamedTitle, in: app)
        XCTAssertTrue(row.waitForExistence(timeout: 4))
        row.tap()
        assertAddress(tabURL, containsOrigin: fixture.origin, in: app)

        openTabSwitcher(in: app)
        row = tabRow(containing: renamedTitle, in: app)
        XCTAssertTrue(row.waitForExistence(timeout: 4))
        let selectedSuffix = try tabIdentifierSuffix(from: row)
        let close = app.buttons["browser.tab-close.\(selectedSuffix)"]
        XCTAssertTrue(close.waitForExistence(timeout: 3))
        close.tap()
        XCTAssertTrue(row.waitForNonExistence(timeout: 3))

        let undo = app.buttons["browser.tabs.undo-close"]
        XCTAssertTrue(undo.waitForExistence(timeout: 3))
        undo.tap()
        assertAddress(tabURL, containsOrigin: fixture.origin, in: app)

        relaunchExactCandidate(app)
        assertAddress(tabURL, containsOrigin: fixture.origin, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8),
            "The restored tab must reload its real HTTPS document after process termination."
        )

        openTabSwitcher(in: app)
        row = tabRow(containing: renamedTitle, in: app)
        XCTAssertTrue(
            row.waitForExistence(timeout: 4),
            "The user-visible tab title must survive process restoration."
        )
        let restoredSuffix = try tabIdentifierSuffix(from: row)
        app.buttons["browser.tab-close.\(restoredSuffix)"].tap()
        app.buttons["browser.tabs.done"].tap()
    }

    @MainActor
    func testJavaScriptConfirmAndPromptAcceptCancelThroughFixtureControls() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        navigate(to: fixture.url(path: "/injection"), in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["CSS, LESS, SASS and JavaScript injection controls"]
                .waitForExistence(timeout: 8)
        )
        let confirmControl = webView.buttons["Run confirm control"]
        let promptControl = webView.buttons["Run prompt control"]
        let accept = app.buttons["browser.dialog.accept"].firstMatch
        let cancel = app.buttons["browser.dialog.cancel"].firstMatch
        XCTAssertTrue(confirmControl.waitForExistence(timeout: 3))
        XCTAssertTrue(promptControl.waitForExistence(timeout: 3))

        confirmControl.tap()
        XCTAssertTrue(accept.waitForExistence(timeout: 3))
        accept.tap()
        XCTAssertTrue(webView.staticTexts["Confirm accepted"].waitForExistence(timeout: 3))

        confirmControl.tap()
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        cancel.tap()
        XCTAssertTrue(webView.staticTexts["Confirm cancelled"].waitForExistence(timeout: 3))

        promptControl.tap()
        XCTAssertTrue(accept.waitForExistence(timeout: 3))
        // iOS 26 exposes SwiftUI alert text fields through the native alert,
        // but does not preserve their SwiftUI accessibility identifiers.
        let promptField = app.alerts.firstMatch.textFields.firstMatch
        XCTAssertTrue(promptField.waitForExistence(timeout: 3))
        XCTAssertEqual(promptField.value as? String, "default")
        try replaceText(in: promptField, with: "harbor", app: app)
        accept.tap()
        XCTAssertTrue(webView.staticTexts["Prompt accepted harbor"].waitForExistence(timeout: 3))

        promptControl.tap()
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        cancel.tap()
        XCTAssertTrue(webView.staticTexts["Prompt cancelled"].waitForExistence(timeout: 3))
    }

    @MainActor
    func testExternalMailPromptCanBeCancelledThroughFixtureControl() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        let injectionURL = fixture.url(path: "/injection")
        navigate(to: injectionURL, in: app)

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["CSS, LESS, SASS and JavaScript injection controls"]
                .waitForExistence(timeout: 8)
        )

        let mailLink = webView.links["Open mail app"]
        XCTAssertTrue(mailLink.waitForExistence(timeout: 3))
        mailLink.tap()

        let cancel = app.buttons["browser.external.cancel"].firstMatch
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 1))
        let renderedMessage = alert.staticTexts.allElementsBoundByIndex
            .map(\.label)
            .joined(separator: " ")
        XCTAssertTrue(renderedMessage.contains(fixture.securityOrigin))
        XCTAssertTrue(renderedMessage.lowercased().contains("mailto"))
        cancel.tap()
        XCTAssertTrue(alert.waitForNonExistence(timeout: 3))
        assertAddress(injectionURL, containsOrigin: fixture.origin, in: app)
    }

    @MainActor
    func testUnknownClickedPageSchemeIsBlockedWithoutLeavingThePage() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        let injectionURL = fixture.url(path: "/injection")
        navigate(to: injectionURL, in: app)

        let webView = app.webViews.firstMatch
        let fixtureTitle = webView.staticTexts[
            "CSS, LESS, SASS and JavaScript injection controls"
        ]
        XCTAssertTrue(fixtureTitle.waitForExistence(timeout: 8))

        let blockedLink = webView.links["Open blocked Ahoi scheme"]
        XCTAssertTrue(blockedLink.waitForExistence(timeout: 3))
        blockedLink.tap()

        let dismiss = app.buttons["browser.error.dismiss"].firstMatch
        XCTAssertTrue(dismiss.waitForExistence(timeout: 3))
        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 1))
        // Native SwiftUI alerts preserve the visible message but iOS 26 does
        // not propagate an identifier applied to their message Text.
        let renderedMessages = Set(alert.staticTexts.allElementsBoundByIndex.map(\.label))
        let expectedMessages: Set<String> = [
            "AhoiBrowser only opens safe web links and approved app links.",
            "AhoiBrowser öffnet nur sichere Weblinks und freigegebene App-Links.",
        ]
        XCTAssertFalse(
            renderedMessages.isDisjoint(with: expectedMessages),
            "The blocked-scheme alert must explain the safe-link boundary."
        )
        XCTAssertEqual(app.state, .runningForeground)
        XCTAssertFalse(app.buttons["browser.external.cancel"].waitForExistence(timeout: 1))
        XCTAssertFalse(app.buttons["browser.external.open"].waitForExistence(timeout: 1))
        assertAddress(injectionURL, containsOrigin: fixture.origin, in: app)

        dismiss.tap()
        XCTAssertTrue(dismiss.waitForNonExistence(timeout: 3))
        XCTAssertTrue(fixtureTitle.waitForExistence(timeout: 3))
        XCTAssertTrue(blockedLink.waitForExistence(timeout: 3))
        assertAddress(injectionURL, containsOrigin: fixture.origin, in: app)
    }

    @MainActor
    func testHTTPClientAndServerFailuresRecoverThroughVisibleRetry() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        let expectedTitles = [
            404: ["Page Not Available", "Seite nicht verfügbar"],
            500: ["Website Error", "Website-Fehler"],
        ]

        for status in [404, 500] {
            let token = UUID().uuidString.lowercased()
            let failureURL = fixture.url(
                path: "/failure/recover-once?status=\(status)&token=\(token)"
            )
            navigate(to: failureURL, in: app)

            let failure = app.descendants(matching: .any)["browser.page-failure"]
            XCTAssertTrue(
                failure.waitForExistence(timeout: 8),
                "HTTP \(status) must replace web content with the visible failure surface."
            )
            let title = failure.descendants(matching: .any).matching(NSPredicate(
                format: "label IN %@",
                expectedTitles[status] ?? []
            )).firstMatch
            XCTAssertTrue(title.waitForExistence(timeout: 3))
            assertAddress(failureURL, containsOrigin: fixture.origin, in: app)

            let retry = app.buttons["browser.retry"]
            XCTAssertTrue(retry.waitForExistence(timeout: 3))
            XCTAssertTrue(retry.isHittable)
            retry.tap()

            let recovery = app.webViews.staticTexts.matching(NSPredicate(
                format: "label CONTAINS[c] %@",
                "HTTP recovery complete"
            )).firstMatch
            XCTAssertTrue(
                recovery.waitForExistence(timeout: 8),
                "Retry must visibly render the fixture's recovered HTTP document."
            )
            XCTAssertTrue(failure.waitForNonExistence(timeout: 3))
            assertAddress(failureURL, containsOrigin: fixture.origin, in: app)
        }
    }

    @MainActor
    func testDNSFailureHasItsOwnVisibleRecoveryState() async throws {
        _ = try await requireReachableFixture()
        let app = coldLaunchApplication()
        let token = UUID().uuidString.lowercased()
        let missingURL = try XCTUnwrap(URL(
            string: "https://missing-\(token).invalid/"
        ))

        navigate(to: missingURL, in: app)

        let failure = app.descendants(matching: .any)["browser.page-failure"]
        XCTAssertTrue(failure.waitForExistence(timeout: 12))
        let title = failure.descendants(matching: .any).matching(NSPredicate(
            format: "label IN %@",
            ["Website Not Found", "Website nicht gefunden"]
        )).firstMatch
        XCTAssertTrue(title.waitForExistence(timeout: 3))
        XCTAssertTrue(app.buttons["browser.retry"].isHittable)
        assertAddress(missingURL, containsOrigin: origin(of: missingURL), in: app)
    }

    @MainActor
    func testSubframeHTTPFailuresDoNotReplaceTheMainDocument() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        let navigationURL = fixture.url(path: "/navigation")

        navigate(to: navigationURL, in: app)

        let webView = app.webViews.firstMatch
        let navigationTitle = webView.staticTexts["Redirect and popup controls"]
        XCTAssertTrue(navigationTitle.waitForExistence(timeout: 8))
        assertWebElementBecomesVisible(
            webView.staticTexts["Subframe HTTP 404 loaded."],
            in: webView,
            message: "The 404 iframe body must remain visibly scoped to its frame."
        )
        assertWebElementBecomesVisible(
            webView.staticTexts["Subframe HTTP 500 loaded."],
            in: webView,
            message: "The 500 iframe body must remain visibly scoped to its frame."
        )
        XCTAssertFalse(
            app.descendants(matching: .any)["browser.page-failure"].exists,
            "A subframe HTTP error must not replace the main browser surface."
        )
        XCTAssertTrue(navigationTitle.exists)
        assertAddress(navigationURL, containsOrigin: fixture.origin, in: app)
    }

    @MainActor
    func testLinkDNSFailureRetainsDestinationForRetryAddressAndTitle() async throws {
        let fixture = try await requireReachableFixture()
        let expectedURL = try XCTUnwrap(URL(
            string: "https://missing-link.ahoibrowser.invalid/path"
        ))
        let app = coldLaunchApplication()

        exerciseDNSFailureJourney(
            fixture: fixture,
            linkLabel: "Link DNS failure",
            expectedURL: expectedURL,
            in: app
        )
    }

    @MainActor
    func testRedirectDNSFailureRetainsFinalDestinationForRetryAddressAndTitle() async throws {
        let fixture = try await requireReachableFixture()
        let expectedURL = try XCTUnwrap(URL(
            string: "https://missing-redirect.ahoibrowser.invalid/final"
        ))
        let app = coldLaunchApplication()

        exerciseDNSFailureJourney(
            fixture: fixture,
            linkLabel: "Redirect DNS failure",
            expectedURL: expectedURL,
            in: app
        )
    }

    @MainActor
    func testPrivateTabsCanBeReorderedThroughTheVisibleDragHandle() async throws {
        let app = coldLaunchApplication()
        for _ in 0..<3 { createPrivateTab(in: app) }
        openTabSwitcher(in: app)

        let rows = privateTabRows(in: app)
        XCTAssertTrue(waitForCount(3, of: rows, timeout: 5))
        let before = tabRowIdentifiersInVisualOrder(in: app)
        XCTAssertEqual(before.count, 3)
        XCTAssertEqual(Set(before).count, 3)

        let edit = app.buttons["browser.tabs.edit"]
        XCTAssertTrue(edit.waitForExistence(timeout: 3))
        edit.tap()
        let reorderList = app.descendants(matching: .any)["browser.tabs.reorder-list"]
        XCTAssertTrue(reorderList.waitForExistence(timeout: 3))

        let orderedRows = privateTabRows(in: app).allElementsBoundByIndex.sorted {
            $0.frame.minY < $1.frame.minY
        }
        guard orderedRows.count == 3 else {
            XCTFail("The private switcher must expose exactly three reorderable tab rows.")
            return
        }
        let sourceRow = orderedRows[0]
        let targetRow = orderedRows[2]
        let applicationOrigin = app.coordinate(withNormalizedOffset: CGVector(dx: 0, dy: 0))
        let gripX = reorderList.frame.maxX - 18
        let source = applicationOrigin.withOffset(CGVector(
            dx: gripX,
            dy: sourceRow.frame.midY
        ))
        let target = applicationOrigin.withOffset(CGVector(
            dx: gripX,
            dy: targetRow.frame.maxY - 4
        ))
        source.press(forDuration: 0.8, thenDragTo: target)

        guard let after = waitForChangedTabOrder(
            from: before,
            expectedIDs: Set(before),
            in: app,
            timeout: 5
        ) else {
            XCTFail("Dragging the native private-tab reorder handle did not change row order.")
            return
        }
        XCTAssertEqual(Set(after), Set(before))
        XCTAssertNotEqual(after, before)
        XCTAssertNotEqual(after.first, before.first)
    }

    @MainActor
    func testUnsafeSchemeMatrixIsRejectedThroughAddressUI() throws {
        let app = coldLaunchApplication()
        let unsafeInputs = [
            "javascript:alert(1)",
            "data:text/html,unsafe",
            "file:///etc/passwd",
            "about:blank",
            "chrome://settings",
        ]

        openAddressEditor(in: app)
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        for unsafeInput in unsafeInputs {
            clearAddressEditor(field, in: app)
            XCTAssertTrue(
                app.descendants(matching: .any)["browser.error.message"]
                    .waitForNonExistence(timeout: 2)
            )
            field.tap()
            field.typeText(unsafeInput)
            let navigate = app.buttons["browser.search.navigate"]
            XCTAssertTrue(navigate.waitForExistence(timeout: 2))
            navigate.tap()
            XCTAssertTrue(
                app.descendants(matching: .any)["browser.error.message"]
                    .waitForExistence(timeout: 3),
                "\(unsafeInput) must fail closed in the visible address UI."
            )
            XCTAssertTrue(field.exists, "Rejected input must keep the address editor open.")
            XCTAssertEqual(field.value as? String, unsafeInput)
        }
    }
    @MainActor
    private func exerciseDNSFailureJourney(
        fixture: FixtureContext,
        linkLabel: String,
        expectedURL: URL,
        in app: XCUIApplication
    ) {
        let navigationURL = fixture.url(path: "/navigation")
        navigate(to: navigationURL, in: app)

        let webView = app.webViews.firstMatch
        let previousTitle = "Redirect and popup controls"
        XCTAssertTrue(webView.staticTexts[previousTitle].waitForExistence(timeout: 8))
        let link = webView.links[linkLabel]
        XCTAssertTrue(link.waitForExistence(timeout: 5))
        link.tap()

        let failure = app.descendants(matching: .any)["browser.page-failure"]
        XCTAssertTrue(
            failure.waitForExistence(timeout: 15),
            "The deterministic .invalid destination must reach Ahoi's DNS recovery surface."
        )
        assertDNSFailureTitle(in: failure)
        assertAddress(
            expectedURL,
            containsOrigin: origin(of: expectedURL),
            in: app
        )
        assertFailedTabTitle(
            expectedHost: expectedURL.host() ?? expectedURL.absoluteString,
            staleTitle: previousTitle,
            in: app
        )

        let retry = app.buttons["browser.retry"]
        XCTAssertTrue(retry.waitForExistence(timeout: 3))
        XCTAssertTrue(retry.isHittable)
        retry.tap()
        let retrying = app.descendants(matching: .any)["browser.page-retrying"]
        XCTAssertTrue(
            retrying.exists,
            "Retry must visibly acknowledge the new navigation attempt."
        )
        XCTAssertTrue(
            retrying.waitForNonExistence(timeout: 5),
            "Retry feedback must settle into the new navigation result."
        )
        XCTAssertTrue(
            failure.waitForExistence(timeout: 15),
            "Retry must keep targeting the failed DNS destination instead of reloading the previous document."
        )
        assertDNSFailureTitle(in: failure)
        assertAddress(
            expectedURL,
            containsOrigin: origin(of: expectedURL),
            in: app
        )
        assertFailedTabTitle(
            expectedHost: expectedURL.host() ?? expectedURL.absoluteString,
            staleTitle: previousTitle,
            in: app
        )
    }

    @MainActor
    private func assertDNSFailureTitle(
        in failure: XCUIElement,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let title = failure.descendants(matching: .any).matching(NSPredicate(
            format: "label IN %@",
            ["Website Not Found", "Website nicht gefunden"]
        )).firstMatch
        XCTAssertTrue(title.waitForExistence(timeout: 3), file: file, line: line)
    }

    @MainActor
    private func assertWebElementBecomesVisible(
        _ element: XCUIElement,
        in webView: XCUIElement,
        message: String,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertTrue(element.waitForExistence(timeout: 8), message, file: file, line: line)
        for _ in 0..<8 where !element.isHittable {
            webView.swipeUp()
        }
        XCTAssertTrue(element.isHittable, message, file: file, line: line)
    }

    @MainActor
    private func assertFailedTabTitle(
        expectedHost: String,
        staleTitle: String,
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        openTabSwitcher(in: app)
        let row = tabRow(containing: expectedHost, in: app)
        XCTAssertTrue(row.waitForExistence(timeout: 5), file: file, line: line)
        let rowTexts = row.descendants(matching: .staticText)
        XCTAssertTrue(
            rowTexts[expectedHost].waitForExistence(timeout: 3),
            "The failed tab's document title must fall back to its destination host.",
            file: file,
            line: line
        )
        XCTAssertFalse(
            rowTexts[staleTitle].exists,
            "The failed tab must not retain the previous document's ordinary title.",
            file: file,
            line: line
        )
        let done = app.buttons["browser.tabs.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3), file: file, line: line)
        done.tap()
        XCTAssertTrue(
            app.buttons["browser.retry"].waitForExistence(timeout: 3),
            file: file,
            line: line
        )
    }

    @MainActor
    private func createNormalTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let newTab = app.buttons["browser.actions.new-tab"]
        XCTAssertTrue(newTab.waitForExistence(timeout: 3))
        newTab.tap()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 3))
    }

    @MainActor
    private func createPrivateTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let newTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(newTab.waitForExistence(timeout: 3))
        newTab.tap()
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 3))
    }

    @MainActor
    private func openTabSwitcher(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 5))
        tabs.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.list"]
                .waitForExistence(timeout: 3)
        )
    }

    @MainActor
    private func tabRow(containing text: String, in app: XCUIApplication) -> XCUIElement {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.tab-row.",
            text
        )).firstMatch
    }

    @MainActor
    private func tabIdentifierSuffix(from row: XCUIElement) throws -> String {
        let prefix = "browser.tab-row."
        guard row.identifier.hasPrefix(prefix) else {
            XCTFail("The tab row must expose its stable production identifier.")
            throw RealE2EContractError.missingStableTabIdentifier
        }
        return String(row.identifier.dropFirst(prefix.count))
    }

    @MainActor
    private func privateTabRows(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        ))
    }

    @MainActor
    private func tabRowIdentifiersInVisualOrder(in app: XCUIApplication) -> [String] {
        privateTabRows(in: app).allElementsBoundByIndex
            .filter { $0.exists && !$0.frame.isEmpty }
            .sorted { $0.frame.minY < $1.frame.minY }
            .map(\.identifier)
    }

    @MainActor
    private func waitForCount(
        _ expectedCount: Int,
        of query: XCUIElementQuery,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if query.count == expectedCount { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
        return query.count == expectedCount
    }

    @MainActor
    private func waitForChangedTabOrder(
        from original: [String],
        expectedIDs: Set<String>,
        in app: XCUIApplication,
        timeout: TimeInterval
    ) -> [String]? {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            let current = tabRowIdentifiersInVisualOrder(in: app)
            if current != original, Set(current) == expectedIDs {
                return current
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
        return nil
    }

    @MainActor
    private func replaceText(
        in element: XCUIElement,
        with value: String,
        app _: XCUIApplication
    ) throws {
        element.tap()
        element.typeKey("a", modifierFlags: .command)
        element.typeText(value)
        if let renderedValue = element.value as? String,
           renderedValue != value {
            // Native SwiftUI alert fields ignore Command-A on iOS 26 even
            // though ordinary app text fields honor it. Clear the observed
            // value explicitly before retrying the replacement.
            element.typeText(String(
                repeating: XCUIKeyboardKey.delete.rawValue,
                count: renderedValue.count
            ))
            element.typeText(value)
        }
        XCTAssertEqual(element.value as? String, value)
    }

    @MainActor
    private func waitForLabelContaining(
        _ fragment: String,
        of element: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "label CONTAINS[c] %@", fragment),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    @MainActor
    private func waitForLabelNotContaining(
        _ fragment: String,
        of element: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "NOT label CONTAINS[c] %@", fragment),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

}

private enum RealE2EContractError: Error {
    case missingStableTabIdentifier
}
