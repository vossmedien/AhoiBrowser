import Foundation
import XCTest

final class MobileBrowserPrivateDataRealE2EUITests: MobileBrowserRealE2ETestCase {
    private let emptyState = "Normal marker: cookie absent, storage absent. "
        + "Private marker: cookie absent, storage absent."
    private let normalOnlyState = "Normal marker: cookie present, storage present. "
        + "Private marker: cookie absent, storage absent."
    private let privateOnlyState = "Normal marker: cookie absent, storage absent. "
        + "Private marker: cookie present, storage present."

    @MainActor
    func testNormalAndPrivateWebsiteDataStayIsolatedAcrossClearAndProcessDeath() async throws {
        let fixture = try await requireReachableFixture()
        let nonce = UUID().uuidString.lowercased()
        let normalToken = "normal-\(nonce)"
        let privateToken = "private-\(nonce)"
        let normalURL = fixture.url(path: "/privacy?private-data=\(normalToken)")
        let firstPrivateURL = fixture.url(
            path: "/privacy?private-data=\(privateToken)-one"
        )
        let secondPrivateURL = fixture.url(
            path: "/privacy?private-data=\(privateToken)-two"
        )
        let afterClearPrivateURL = fixture.url(
            path: "/privacy?private-data=\(privateToken)-after-clear"
        )
        let afterRestartPrivateURL = fixture.url(
            path: "/privacy?private-data=\(privateToken)-after-restart"
        )
        let app = coldLaunchApplication()
        normalizeInitialBrowserState(in: app)

        navigate(to: normalURL, in: app)
        assertPrivacyFixtureIsVisible(in: app)
        runMarkerControl("Clear markers in this session", in: app)
        assertMarkerState(emptyState, in: app)
        runMarkerControl("Set normal marker", in: app)
        assertMarkerState(normalOnlyState, in: app)

        createPrivateTab(in: app)
        navigatePrivately(to: firstPrivateURL, in: app)
        assertPrivacyFixtureIsVisible(in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            emptyState,
            in: app,
            message: "A newly created private website-data store must start empty."
        )
        runMarkerControl("Set private marker", in: app)
        assertMarkerState(privateOnlyState, in: app)

        createPrivateTab(in: app)
        navigatePrivately(to: secondPrivateURL, in: app)
        assertPrivacyFixtureIsVisible(in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            privateOnlyState,
            in: app,
            message: "Two open private tabs must share one ephemeral website-data store."
        )

        selectNormalTab(containing: normalToken, in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            normalOnlyState,
            in: app,
            message: "Private browsing must not modify the persistent normal store."
        )

        clearAllPrivateTabs(in: app)
        assertTabCountOne(in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            normalOnlyState,
            in: app,
            message: "Closing every private tab must leave normal cookies and storage intact."
        )

        createPrivateTab(in: app)
        navigatePrivately(to: afterClearPrivateURL, in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            emptyState,
            in: app,
            message: "A private session created after Close Private Tabs must use a fresh store."
        )
        runMarkerControl("Set private marker", in: app)
        assertMarkerState(privateOnlyState, in: app)

        relaunchExactCandidate(app)

        assertPrivateTabSwitcherIsEmptyAfterRestart(in: app)
        XCTAssertFalse(
            app.buttons["browser.address.private"].waitForExistence(timeout: 2),
            "Private tabs must not survive an app process restart."
        )
        assertTabCountOne(in: app)
        assertAddress(normalURL, containsOrigin: fixture.origin, in: app)
        assertPrivacyFixtureIsVisible(in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            normalOnlyState,
            in: app,
            message: "The normal first-party cookie and marker must survive process restoration."
        )

        assertVisibleHistoryProjection(
            contains: normalToken,
            excludes: privateToken,
            in: app
        )

        createPrivateTab(in: app)
        navigatePrivately(to: afterRestartPrivateURL, in: app)
        runMarkerControl("Inspect markers", in: app)
        assertMarkerState(
            emptyState,
            in: app,
            message: "The private marker must disappear with the terminated process."
        )

        selectNormalTab(containing: normalToken, in: app)
        runMarkerControl("Clear markers in this session", in: app)
        assertMarkerState(emptyState, in: app)
        clearAllPrivateTabs(in: app)
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
            predicate: NSPredicate(format: "value == %@", url.absoluteString),
            object: privateAddress
        )
        XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 8), .completed)
    }

    @MainActor
    private func assertPrivacyFixtureIsVisible(
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertTrue(
            app.webViews.staticTexts["Normal and private data isolation"]
                .waitForExistence(timeout: 8),
            file: file,
            line: line
        )
    }

    @MainActor
    private func runMarkerControl(
        _ label: String,
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let webView = app.webViews.firstMatch
        let control = webView.buttons[label]
        reveal(control, in: webView, file: file, line: line)
        control.tap()
    }

    @MainActor
    private func assertMarkerState(
        _ expected: String,
        in app: XCUIApplication,
        message: String = "The visible fixture marker state did not match.",
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let renderedState = app.webViews.descendants(matching: .any).matching(
            NSPredicate(
                format: "label CONTAINS %@ OR value CONTAINS %@",
                expected,
                expected
            )
        ).firstMatch
        reveal(
            renderedState,
            in: app.webViews.firstMatch,
            message: "\(message) Expected the rendered fixture state: \(expected)",
            file: file,
            line: line
        )
    }

    @MainActor
    private func reveal(
        _ element: XCUIElement,
        in webView: XCUIElement,
        message: String = "The fixture element must become visibly hittable.",
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertTrue(webView.waitForExistence(timeout: 5), file: file, line: line)

        // Start from a known document edge before moving toward controls that
        // WebKit may keep in its accessibility tree while they are offscreen.
        for _ in 0..<4 where !element.isHittable { webView.swipeDown() }
        for _ in 0..<8 where !element.isHittable { webView.swipeUp() }

        XCTAssertTrue(
            element.waitForExistence(timeout: 3),
            message,
            file: file,
            line: line
        )
        XCTAssertTrue(
            element.isHittable,
            "\(message) The accessibility node existed but remained offscreen.",
            file: file,
            line: line
        )
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
    private func selectNormalTab(containing token: String, in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 5))
        tabs.tap()
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 3))
        mode.coordinate(withNormalizedOffset: CGVector(dx: 0.25, dy: 0.5)).tap()

        let row = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS %@",
            "browser.tab-row.",
            token
        )).firstMatch
        XCTAssertTrue(row.waitForExistence(timeout: 4))
        row.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))
    }

    @MainActor
    private func clearAllPrivateTabs(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let labels = ["Close Private Tabs", "Private Tabs schließen"]
        let action = app.buttons.matching(NSPredicate(format: "label IN %@", labels))
            .firstMatch
        for _ in 0..<8 where !action.exists { app.swipeUp() }
        XCTAssertTrue(action.waitForExistence(timeout: 3))
        action.tap()

        let title = app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["Close all private tabs?", "Alle privaten Tabs schließen?"]
        )).firstMatch
        XCTAssertTrue(title.waitForExistence(timeout: 3))
        let confirmation = app.buttons.matching(NSPredicate(format: "label IN %@", labels))
            .firstMatch
        XCTAssertTrue(confirmation.waitForExistence(timeout: 3))
        confirmation.tap()
        XCTAssertTrue(title.waitForNonExistence(timeout: 3))
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 5))
    }

    @MainActor
    private func assertTabCountOne(
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 5), file: file, line: line)
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(
                format: "label IN %@",
                ["1 tab", "1 tabs", "1 Tab", "1 Tabs"]
            ),
            object: tabs
        )
        XCTAssertEqual(
            XCTWaiter.wait(for: [expectation], timeout: 5),
            .completed,
            file: file,
            line: line
        )
    }

    @MainActor
    private func assertVisibleHistoryProjection(
        contains normalToken: String,
        excludes privateToken: String,
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        openHistory(in: app, file: file, line: line)
        filterVisibleHistory(for: normalToken, in: app, file: file, line: line)

        let normalVisit = historyRows(in: app).matching(NSPredicate(
            format: "label CONTAINS %@",
            normalToken
        )).firstMatch
        XCTAssertTrue(
            normalVisit.waitForExistence(timeout: 5),
            "The normal navigation must reach the visible local-first history projection.",
            file: file,
            line: line
        )
        XCTAssertGreaterThan(
            historyRows(in: app).count,
            0,
            "The negative private-history query requires a proven positive control population.",
            file: file,
            line: line
        )
        dismissHistory(in: app, file: file, line: line)

        // Reopen a fresh presentation so the search query is reset through
        // real UI lifecycle, then filter the complete history projection. A
        // private row can no longer hide below a lazy/offscreen List boundary.
        openHistory(in: app, file: file, line: line)
        filterVisibleHistory(for: privateToken, in: app, file: file, line: line)
        XCTAssertTrue(
            waitForQueryCount(0, query: historyRows(in: app), timeout: 5),
            "Private destinations must remain absent from the UI projection used by Ahoi Sync.",
            file: file,
            line: line
        )
        dismissHistory(in: app, file: file, line: line)
    }

    @MainActor
    private func openHistory(
        in app: XCUIApplication,
        file: StaticString,
        line: UInt
    ) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5), file: file, line: line)
        more.tap()
        let history = app.buttons["browser.actions.history"]
        for _ in 0..<6 where !history.isHittable { app.swipeUp() }
        XCTAssertTrue(history.waitForExistence(timeout: 3), file: file, line: line)
        XCTAssertTrue(history.isHittable, file: file, line: line)
        history.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.history.sheet"]
                .waitForExistence(timeout: 3),
            file: file,
            line: line
        )
    }

    @MainActor
    private func filterVisibleHistory(
        for token: String,
        in app: XCUIApplication,
        file: StaticString,
        line: UInt
    ) {
        let search = app.searchFields["browser.history.search"]
        let fallbackSearch = app.searchFields.firstMatch
        let field = search.exists ? search : fallbackSearch
        if !field.exists { app.swipeDown() }
        XCTAssertTrue(
            field.waitForExistence(timeout: 3),
            "The real History presentation must expose its visible search field.",
            file: file,
            line: line
        )
        field.tap()
        field.typeText(token)
        let exactQuery = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", token),
            object: field
        )
        XCTAssertEqual(
            XCTWaiter.wait(for: [exactQuery], timeout: 3),
            .completed,
            "History filtering must use the exact unique query before row counts are accepted.",
            file: file,
            line: line
        )
    }

    @MainActor
    private func dismissHistory(
        in app: XCUIApplication,
        file: StaticString,
        line: UInt
    ) {
        let done = app.buttons["browser.history.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3), file: file, line: line)
        done.tap()
        XCTAssertTrue(
            done.waitForNonExistence(timeout: 3),
            file: file,
            line: line
        )
    }

    @MainActor
    private func normalizeInitialBrowserState(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 5))
        tabs.tap()

        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 3))

        selectTabSwitcherMode(privateBrowsing: true, using: mode)
        XCTAssertTrue(
            waitForPrivateTabProjection(in: app, timeout: 4),
            "The visible private segment must finish loading before normalization."
        )
        closeVisibleTabRows(until: 0, in: app)
        XCTAssertTrue(waitForQueryCount(0, query: tabRows(in: app), timeout: 3))
        XCTAssertTrue(privateEmptyState(in: app).waitForExistence(timeout: 3))

        selectTabSwitcherMode(privateBrowsing: false, using: mode)
        XCTAssertTrue(waitForAtLeastOneElement(in: tabCloseButtons(in: app), timeout: 3))
        closeVisibleTabRows(until: 1, in: app)

        guard let remainingRow = firstHittableElement(in: tabRows(in: app)) else {
            XCTFail("The normalized session must retain one visible normal tab.")
            return
        }
        remainingRow.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))

        // Close the last persisted tab through production UI. The browser's
        // normal replacement path creates the single fresh tab used below.
        tabs.tap()
        XCTAssertTrue(mode.waitForExistence(timeout: 3))
        selectTabSwitcherMode(privateBrowsing: false, using: mode)
        XCTAssertTrue(waitForQueryCount(1, query: tabCloseButtons(in: app), timeout: 3))
        guard let lastPersistedClose = firstHittableElement(in: tabCloseButtons(in: app)) else {
            XCTFail("The final persisted normal tab must expose a visible close control.")
            return
        }
        let closedIdentifier = lastPersistedClose.identifier
        lastPersistedClose.tap()
        XCTAssertTrue(app.buttons[closedIdentifier].waitForNonExistence(timeout: 3))
        XCTAssertTrue(waitForQueryCount(1, query: tabCloseButtons(in: app), timeout: 3))
        guard let freshRow = firstHittableElement(in: tabRows(in: app)) else {
            XCTFail("Closing the final normal tab must visibly create its fresh replacement.")
            return
        }
        freshRow.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))
        assertTabCountOne(in: app)
    }

    @MainActor
    private func assertPrivateTabSwitcherIsEmptyAfterRestart(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 5))
        tabs.tap()

        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 3))
        selectTabSwitcherMode(privateBrowsing: true, using: mode)
        XCTAssertTrue(
            waitForQueryCount(0, query: tabRows(in: app), timeout: 4),
            "The private tab switcher must contain zero rows after process restoration."
        )
        let emptyState = privateEmptyState(in: app)
        XCTAssertTrue(
            emptyState.waitForExistence(timeout: 4),
            "The visible private tab switcher must explain that the ephemeral population is empty."
        )
        let done = app.buttons["browser.tabs.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3))
        done.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))
    }

    @MainActor
    private func selectTabSwitcherMode(
        privateBrowsing: Bool,
        using control: XCUIElement
    ) {
        control.coordinate(withNormalizedOffset: CGVector(
            dx: privateBrowsing ? 0.75 : 0.25,
            dy: 0.5
        )).tap()
    }

    @MainActor
    private func closeVisibleTabRows(until expectedCount: Int, in app: XCUIApplication) {
        let closeButtons = tabCloseButtons(in: app)
        var attempts = 0
        while closeButtons.count > expectedCount, attempts < 80 {
            attempts += 1
            guard let close = firstHittableElement(in: closeButtons) else {
                XCTFail("Every visible tab row must retain a hittable close control.")
                return
            }
            let identifier = close.identifier
            close.tap()
            XCTAssertTrue(app.buttons[identifier].waitForNonExistence(timeout: 3))
        }
        XCTAssertEqual(closeButtons.count, expectedCount)
    }

    @MainActor
    private func tabCloseButtons(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-close."
        ))
    }

    @MainActor
    private func tabRows(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        ))
    }

    @MainActor
    private func historyRows(in app: XCUIApplication) -> XCUIElementQuery {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.history.row."
        ))
    }

    @MainActor
    private func privateEmptyState(in app: XCUIApplication) -> XCUIElement {
        app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["No Private Tabs", "Keine privaten Tabs"]
        )).firstMatch
    }

    @MainActor
    private func firstHittableElement(in query: XCUIElementQuery) -> XCUIElement? {
        (0..<query.count)
            .map { query.element(boundBy: $0) }
            .first(where: \.isHittable)
    }

    @MainActor
    private func waitForAtLeastOneElement(
        in query: XCUIElementQuery,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if query.count > 0 { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return query.count > 0
    }

    @MainActor
    private func waitForPrivateTabProjection(
        in app: XCUIApplication,
        timeout: TimeInterval
    ) -> Bool {
        let closeButtons = tabCloseButtons(in: app)
        let emptyState = privateEmptyState(in: app)
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if closeButtons.count > 0 || emptyState.exists { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return closeButtons.count > 0 || emptyState.exists
    }

    @MainActor
    private func waitForQueryCount(
        _ expected: Int,
        query: XCUIElementQuery,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if query.count == expected { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return query.count == expected
    }
}
