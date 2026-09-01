import XCTest

final class MobileBrowserPrivacyPermissionUITests: XCTestCase {
    private let fixtureOrigin = "https://fixture.ahoibrowser.test"

    @MainActor
    func testWebsitePermissionKindsExposeOriginAndAllExplicitChoices() throws {
        let app = launchFixture()
        let webView = app.webViews.firstMatch
        XCTAssertTrue(webView.staticTexts["Ahoi fixture page"].waitForExistence(timeout: 8))

        let requests: [(label: String, expectedTokenGroups: [[String]])] = [
            ("Request camera permission", [["camera", "kamera"]]),
            ("Request microphone permission", [["microphone", "mikrofon"]]),
            (
                "Request camera and microphone permission",
                [["camera", "kamera"], ["microphone", "mikrofon"]]
            ),
            ("Request motion permission", [["motion", "bewegung"]]),
        ]

        for request in requests {
            let control = webView.buttons[request.label]
            reveal(control, in: webView)
            control.tap()
            assertPermissionPrompt(
                in: app,
                containsAll: request.expectedTokenGroups
            )

            app.buttons["browser.permission.cancel"].firstMatch.tap()
            XCTAssertTrue(
                app.buttons["browser.permission.allow"].firstMatch
                    .waitForNonExistence(timeout: 3)
            )

            // The same origin can deliberately ask again after a cancelled
            // one-shot decision; no stale request may consume the retry.
            control.tap()
            assertPermissionPrompt(
                in: app,
                containsAll: request.expectedTokenGroups
            )

            app.buttons["browser.permission.deny"].firstMatch.tap()
            XCTAssertTrue(
                app.buttons["browser.permission.allow"].firstMatch
                    .waitForNonExistence(timeout: 3)
            )
        }
    }

    @MainActor
    func testPermissionAllowResolvesOnlyTheRenderedOriginBoundRequest() throws {
        let app = launchFixture()
        let webView = app.webViews.firstMatch
        let microphone = webView.buttons["Request microphone permission"]
        reveal(microphone, in: webView)
        microphone.tap()
        assertPermissionPrompt(
            in: app,
            containsAll: [["microphone", "mikrofon"]]
        )

        app.buttons["browser.permission.allow"].firstMatch.tap()
        XCTAssertTrue(
            app.buttons["browser.permission.allow"].firstMatch
                .waitForNonExistence(timeout: 3),
            "The Ahoi permission prompt must resolve before any OS-owned TCC prompt takes over."
        )
        app.terminate()
    }

    @MainActor
    func testFileInputCanBeCancelledAndRequestedAgainWithoutPathDisclosure() throws {
        let app = launchFixture()
        let webView = app.webViews.firstMatch
        let fileInput = webView.buttons["Choose a fixture file"]
        reveal(fileInput, in: webView)

        for _ in 0..<2 {
            fileInput.tap()
            let cancel = app.buttons["browser.file_input.cancel"].firstMatch
            XCTAssertTrue(cancel.waitForExistence(timeout: 3))
            let message = renderedAlertText(in: app)
            XCTAssertTrue(message.contains(fixtureOrigin))
            XCTAssertFalse(message.contains("/private/"))
            XCTAssertFalse(message.contains("/var/mobile/"))
            cancel.tap()
            XCTAssertTrue(cancel.waitForNonExistence(timeout: 3))
        }
        let result = webView.descendants(matching: .any)["File selection result"]
        XCTAssertTrue(waitForText("No file selected.", in: result, timeout: 3))
    }

    /// Integration seam: the native document picker cannot be populated from
    /// another process deterministically. This still exercises the production
    /// staging, presenter continuation, and WebKit FileList result end to end.
    @MainActor
    func testIntegrationSelectedFileIsStagedAndReturnedToWebKit() throws {
        let app = launchFixture(extraArguments: ["-AhoiUITestFileSelection"])
        let webView = app.webViews.firstMatch
        let fileInput = webView.buttons["Choose a fixture file"]
        reveal(fileInput, in: webView)
        fileInput.tap()

        let choose = app.buttons["browser.file_input.choose"].firstMatch
        XCTAssertTrue(choose.waitForExistence(timeout: 3))
        choose.tap()
        let result = webView.descendants(matching: .any)["File selection result"]
        let expectedResult = "Selected file: ahoi-upload-fixture.txt · 20 bytes."
        XCTAssertTrue(
            waitForText(expectedResult, in: result, timeout: 8),
            "WebKit must receive the app-owned staged selection and materialize its FileList."
        )
        let rendered = result.label + " " + ((result.value as? String) ?? "")
        XCTAssertFalse(rendered.contains("/private/"))
        XCTAssertFalse(rendered.contains("/var/mobile/"))
        XCTAssertFalse(rendered.contains("AhoiBrowserFileInput"))
    }

    @MainActor
    func testExternalHandoffShowsOriginAndMaskedQueryFreeTarget() throws {
        let app = launchFixture()
        let webView = app.webViews.firstMatch
        let mail = webView.links["Open privacy-sensitive mail app"]
        reveal(mail, in: webView)
        mail.tap()

        let cancel = app.buttons["browser.external.cancel"].firstMatch
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        let message = renderedAlertText(in: app)
        XCTAssertTrue(message.contains(fixtureOrigin))
        XCTAssertTrue(message.contains("mailto:•••@example.com"))
        XCTAssertFalse(message.contains("browser-test"))
        XCTAssertFalse(message.contains("ahoi-secret"))
        XCTAssertFalse(message.contains("subject="))
        XCTAssertFalse(message.contains("body="))
        XCTAssertFalse(message.contains("?"))

        cancel.tap()
        XCTAssertTrue(cancel.waitForNonExistence(timeout: 3))
        XCTAssertEqual(app.state, .runningForeground)
    }

    @MainActor
    private func launchFixture(extraArguments: [String] = []) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-AhoiUITestFixture"] + extraArguments
        app.launch()
        return app
    }

    @MainActor
    private func assertPermissionPrompt(
        in app: XCUIApplication,
        containsAll expectedTokenGroups: [[String]],
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let allow = app.buttons["browser.permission.allow"].firstMatch
        let deny = app.buttons["browser.permission.deny"].firstMatch
        let cancel = app.buttons["browser.permission.cancel"].firstMatch
        XCTAssertTrue(allow.waitForExistence(timeout: 3), file: file, line: line)
        XCTAssertTrue(deny.exists, file: file, line: line)
        XCTAssertTrue(cancel.exists, file: file, line: line)

        let message = renderedAlertText(in: app).lowercased()
        XCTAssertTrue(message.contains(fixtureOrigin), file: file, line: line)
        XCTAssertTrue(
            expectedTokenGroups.allSatisfy { group in
                group.contains(where: message.contains)
            },
            "The prompt did not name its requested capability: \(message)",
            file: file,
            line: line
        )
    }

    @MainActor
    private func renderedAlertText(in app: XCUIApplication) -> String {
        app.alerts.firstMatch.staticTexts.allElementsBoundByIndex
            .map(\.label)
            .joined(separator: " ")
    }

    @MainActor
    private func waitForText(
        _ expected: String,
        in element: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        guard element.waitForExistence(timeout: timeout) else { return false }
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(
                format: "label == %@ OR value == %@",
                expected,
                expected
            ),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in webView: XCUIElement) {
        for _ in 0..<8 where !element.isHittable {
            webView.swipeUp()
        }
        XCTAssertTrue(element.waitForExistence(timeout: 3))
        XCTAssertTrue(element.isHittable)
    }
}
