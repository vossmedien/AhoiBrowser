import Foundation
import XCTest

final class MobileSyncRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testRealNavigationPublishesEncryptedNormalTabAndHistory() async throws {
        let fixture = try await requireReachableFixture()
        let app = launchSyncProjection()
        navigate(
            to: fixture.url(path: "/navigation?sync-visible=normal"),
            in: app
        )
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )

        openSettings(in: app)
        assertEvidence("settings.sync.evidence.current-session-tabs", equals: "1", in: app)
        assertEvidence("settings.sync.evidence.outbox-tabs", equals: "1", in: app)
        assertEvidence("settings.sync.evidence.history", minimum: 1, in: app)
        assertEvidence("settings.sync.evidence.pending", minimum: 4, in: app)
        assertEvidence("settings.sync.evidence.encrypted", equals: "verified", in: app)
        assertEvidence("settings.sync.evidence.denied", equals: "0", in: app)
    }

    @MainActor
    func testPrivateTabsStayOutsideRepositoryAndEncryptedOutbox() {
        let app = launchSyncProjection(extraArguments: [
            "-AhoiUITestNormalTabCount", "1",
            "-AhoiUITestPrivateTabCount", "2",
            "-AhoiUITestSelectPrivate",
        ])
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 5))
        XCTAssertTrue(
            ["2 tabs", "2 Tabs"].contains(app.buttons["browser.tabs"].label)
        )

        openSettings(in: app)
        assertEvidence("settings.sync.evidence.current-session-tabs", equals: "1", in: app)
        assertEvidence("settings.sync.evidence.outbox-tabs", equals: "1", in: app)
        assertEvidence("settings.sync.evidence.encrypted", equals: "verified", in: app)
        assertEvidence("settings.sync.evidence.denied", equals: "0", in: app)
    }

    @MainActor
    func testStagedRemoteTitleWinsThroughProductionFieldMerge() {
        let app = launchSyncProjection(extraArguments: ["-AhoiUITestSyncConflict"])
        openSettings(in: app)
        assertEvidence("settings.sync.evidence.conflict", equals: "resolved", in: app)
        assertEvidence("settings.sync.evidence.current-session-tabs", equals: "1", in: app)
        assertEvidence("settings.sync.evidence.outbox-tabs", equals: "1", in: app)
        assertEvidence("settings.sync.evidence.denied", equals: "0", in: app)
    }

    @MainActor
    private func launchSyncProjection(
        extraArguments: [String] = []
    ) -> XCUIApplication {
        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture",
            "-AhoiUITestSyncProjection",
            "-AhoiMobile.Sync.Enabled", "YES",
        ] + extraArguments)
        XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
        return app
    }

    @MainActor
    private func openSettings(in app: XCUIApplication) {
        app.buttons["browser.more"].tap()
        let settings = app.buttons["browser.actions.settings"]
        let actionsList = app.descendants(matching: .any)["browser.actions.list"]
        XCTAssertTrue(actionsList.waitForExistence(timeout: 4))
        reveal(settings, in: actionsList)
        XCTAssertTrue(settings.waitForExistence(timeout: 3))
        settings.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["settings.form"]
                .waitForExistence(timeout: 4)
        )
    }

    @MainActor
    private func assertEvidence(
        _ identifier: String,
        equals expected: String,
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let element = revealEvidence(identifier, in: app)
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", expected),
            object: element
        )
        XCTAssertEqual(
            XCTWaiter.wait(for: [expectation], timeout: 8),
            .completed,
            "Expected \(identifier) to expose \(expected); got \(element.value ?? "<nil>").",
            file: file,
            line: line
        )
    }

    @MainActor
    private func assertEvidence(
        _ identifier: String,
        minimum: Int,
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let element = revealEvidence(identifier, in: app)
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate { object, _ in
                guard let element = object as? XCUIElement,
                      let value = element.value as? String,
                      let count = Int(value) else { return false }
                return count >= minimum
            },
            object: element
        )
        XCTAssertEqual(
            XCTWaiter.wait(for: [expectation], timeout: 8),
            .completed,
            "Expected \(identifier) >= \(minimum); got \(element.value ?? "<nil>").",
            file: file,
            line: line
        )
    }

    @MainActor
    private func revealEvidence(
        _ identifier: String,
        in app: XCUIApplication
    ) -> XCUIElement {
        let element = app.descendants(matching: .any)[identifier]
        let form = app.descendants(matching: .any)["settings.form"]
        XCTAssertTrue(form.waitForExistence(timeout: 4))
        reveal(element, in: form)
        return element
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in container: XCUIElement) {
        for _ in 0..<10 {
            if element.exists, element.isHittable { return }
            container.swipeUp()
        }
        XCTAssertTrue(element.waitForExistence(timeout: 2))
        XCTAssertTrue(
            element.isHittable,
            "The requested element must become visibly reachable after scrolling."
        )
    }
}
