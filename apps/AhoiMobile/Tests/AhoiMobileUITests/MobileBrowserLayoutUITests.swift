import XCTest

final class MobileBrowserLayoutUITests: XCTestCase {
    @MainActor
    func testFocusVoyageAndHarborDeckKeepAllCoreControlsReachable() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.actions.new-tab"].waitForExistence(timeout: 3))
        app.buttons["browser.actions.new-tab"].tap()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.header"]
                .waitForExistence(timeout: 3)
        )
        XCTAssertTrue(app.buttons["browser.focus-voyage.search"].exists)
        XCTAssertTrue(app.buttons["browser.back"].exists)
        XCTAssertTrue(app.buttons["browser.forward"].exists)
        XCTAssertTrue(app.buttons["browser.reload-stop"].exists)
        XCTAssertTrue(app.buttons["browser.tabs"].exists)
        XCTAssertTrue(app.buttons["browser.more"].exists)
    }

    @MainActor
    func testPrivateFocusVoyageDoesNotExposeNormalJourneySections() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        XCTAssertTrue(app.buttons["browser.more"].waitForExistence(timeout: 8))
        app.buttons["browser.more"].tap()
        XCTAssertTrue(app.buttons["browser.new-private-tab"].waitForExistence(timeout: 3))
        app.buttons["browser.new-private-tab"].tap()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 3)
        )
        XCTAssertTrue(app.buttons["browser.address.private"].exists)
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private-explanation"].exists
        )
        XCTAssertFalse(app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.focus-voyage.item."
        )).firstMatch.exists)
    }

    @MainActor
    func testHarborDeckCollapsesOnPageScrollAndRestoresOnReverseScroll() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        let webView = app.webViews.firstMatch
        XCTAssertTrue(workspace.waitForExistence(timeout: 8))
        XCTAssertTrue(webView.waitForExistence(timeout: 3))

        webView.swipeUp()
        XCTAssertTrue(workspace.waitForNonExistence(timeout: 3))
        XCTAssertTrue(app.buttons["browser.address"].exists)
        XCTAssertTrue(app.buttons["browser.tabs"].exists)
        XCTAssertTrue(app.buttons["browser.more"].exists)

        webView.swipeDown()
        XCTAssertTrue(workspace.waitForExistence(timeout: 3))
    }

    @MainActor
    func testWorkspaceCanvasExposesPersistentCommandSidebarOnIPad() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()
        try XCTSkipIf(app.frame.width < 700, "Regular-width Workspace Canvas is iPad-only.")

        XCTAssertTrue(app.buttons["browser.sidebar.command"].waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["browser.sidebar.new-tab"].exists)
    }
}
