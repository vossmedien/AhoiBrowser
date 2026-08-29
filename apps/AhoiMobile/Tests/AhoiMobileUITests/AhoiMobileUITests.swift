import XCTest

final class AhoiMobileUITests: XCTestCase {
    @MainActor
    func testLocalFixtureAndPrivateTabLifecycle() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

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
        app.launchArguments = []
        app.launch()
        XCTAssertFalse(
            app.buttons["browser.address.private"].waitForExistence(timeout: 1),
            "Private tabs must never survive process restart."
        )
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 5))
    }

    @MainActor
    func testUnsafeSchemeIsExplainedAndRejected() throws {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"]
        app.launch()

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
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestOffline"]
        app.launch()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.page-failure"].waitForExistence(timeout: 8)
        )
        XCTAssertTrue(app.descendants(matching: .any)["browser.retry"].exists)
    }
}
