import XCTest

/// Link Peek (Master `WORKFLOW-02`, Mobile goal "Sichtbare Browserjourneys"):
/// a link is previewed explicitly from its long-press actions, closing the
/// preview leaves the source page and tab population unchanged, and adopting
/// it creates exactly one normal tab with the previewed destination.
///
/// The source is the deterministic local fixture page; its link target is the
/// public `https://example.com/?ahoi-link-actions=1` (HTTP 200), so the
/// preview loads a real network page in its own WebKit page.
final class MobileLinkPeekUITests: MobileBrowserUITestCase {
    private let stateTimeout: TimeInterval = 10
    private let networkTimeout: TimeInterval = 20

    @MainActor
    func testPeekClosesToUnchangedSourceAndAdoptsAsOneNormalTab() throws {
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer { app.terminate() }
        let sourceHeading = app.webViews.staticTexts["Ahoi fixture page"]
        XCTAssertTrue(sourceHeading.waitForExistence(timeout: stateTimeout))
        let address = app.buttons["browser.address"]
        XCTAssertTrue(waitForAddress(address, containing: "fixture.ahoibrowser.test"))
        let tabs = app.buttons["browser.tabs"]
        let tabsBefore = try XCTUnwrap(tabCount(of: tabs), "Tab count must be readable.")

        // 1. Peek, then go back to the page.
        openPeek(in: app)
        let origin = app.descendants(matching: .any)["browser.link-preview.origin"]
        XCTAssertTrue(origin.waitForExistence(timeout: stateTimeout))
        XCTAssertTrue(
            origin.label.contains("example.com"),
            "The preview must name the destination origin; got '\(origin.label)'."
        )
        XCTAssertTrue(
            origin.label.contains("fixture.ahoibrowser.test"),
            "The preview must name the source origin; got '\(origin.label)'."
        )
        let destination = exampleDomainPage(in: app)
        let loaded = destination.waitForExistence(timeout: networkTimeout)
        if !loaded {
            attach(app, "peek-destination-missing")
            let tree = XCTAttachment(string: app.debugDescription)
            tree.name = "peek-destination-missing-tree"
            tree.lifetime = .keepAlways
            add(tree)
        }
        XCTAssertTrue(loaded, "The preview must load the destination in its own page.")
        attach(app, "peek-open")
        let back = app.buttons["browser.link-preview.back"]
        XCTAssertTrue(waitForHittable(back, timeout: stateTimeout))
        back.tap()
        XCTAssertTrue(origin.waitForNonExistence(timeout: stateTimeout))
        XCTAssertTrue(sourceHeading.waitForExistence(timeout: stateTimeout))
        XCTAssertFalse(destination.exists, "Closing must discard the preview page.")
        XCTAssertTrue(waitForAddress(address, containing: "fixture.ahoibrowser.test"))
        XCTAssertEqual(tabCount(of: tabs), tabsBefore, "A closed preview must not create a tab.")
        attach(app, "peek-closed-source-unchanged")

        // 2. Peek again and adopt it as a tab.
        openPeek(in: app)
        XCTAssertTrue(origin.waitForExistence(timeout: stateTimeout))
        XCTAssertTrue(destination.waitForExistence(timeout: networkTimeout))
        let adopt = app.buttons["browser.link-preview.open-tab"]
        XCTAssertTrue(waitForHittable(adopt, timeout: stateTimeout))
        XCTAssertTrue(adopt.isEnabled, "A loaded preview must be adoptable.")
        adopt.tap()
        XCTAssertTrue(origin.waitForNonExistence(timeout: stateTimeout))
        XCTAssertTrue(
            waitForTabCount(tabsBefore + 1, of: tabs),
            "Adopting must add exactly one tab; tabs '\(tabs.label)'."
        )
        XCTAssertTrue(
            waitForAddress(address, containing: "example.com"),
            "The adopted tab must be selected with the previewed destination."
        )
        XCTAssertTrue(
            exampleDomainPage(in: app).waitForExistence(timeout: stateTimeout),
            "The adopted tab must show the already loaded preview page."
        )
        XCTAssertFalse(
            app.buttons["browser.address.private"].exists,
            "A preview from a normal tab must be adopted as a normal tab."
        )
        attach(app, "peek-adopted-as-tab")

        // The source tab still exists next to the adopted one.
        tabs.tap()
        let sourceRow = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND (label CONTAINS[c] %@ OR label CONTAINS[c] %@)",
            "browser.tab-row.", "Ahoi Fixture", "fixture.ahoibrowser.test"
        )).firstMatch
        XCTAssertTrue(
            sourceRow.waitForExistence(timeout: stateTimeout),
            "The source tab must remain in the switcher after adoption."
        )
        attach(app, "peek-switcher-after-adopt")
        let done = app.buttons["browser.tabs.done"]
        XCTAssertTrue(waitForHittable(done, timeout: stateTimeout))
        done.tap()
    }

    @MainActor
    private func openPeek(in app: XCUIApplication) {
        let link = app.webViews.links["Open Ahoi link actions"]
        XCTAssertTrue(link.waitForExistence(timeout: stateTimeout))
        link.press(forDuration: 1.2)
        let preview = app.buttons["browser.link-actions.preview"]
        XCTAssertTrue(
            waitForHittable(preview, timeout: stateTimeout),
            "Long-press must offer the explicit preview action."
        )
        preview.tap()
    }

    @MainActor
    private func waitForAddress(_ address: XCUIElement, containing text: String) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value CONTAINS %@", text),
            object: address
        )
        return XCTWaiter.wait(for: [expectation], timeout: stateTimeout) == .completed
    }

    @MainActor
    private func tabCount(of tabs: XCUIElement) -> Int? {
        if let value = tabs.value as? String,
           let count = Int(value.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return count
        }
        let digits = tabs.label.prefix { $0.isNumber }
        return Int(digits)
    }

    @MainActor
    private func waitForTabCount(_ expected: Int, of tabs: XCUIElement) -> Bool {
        let deadline = Date().addingTimeInterval(stateTimeout)
        repeat {
            if tabCount(of: tabs) == expected { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
        return tabCount(of: tabs) == expected
    }

    @MainActor
    private func attach(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
