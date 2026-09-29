import XCTest

/// Saved-page Home Address (Master "Gespeicherte Ausgangsadresse",
/// `WORKFLOW-02`): a saved page keeps a fixed Home Address next to its current
/// URL. Browsing changes only the current URL, "Go to Home Address" returns,
/// "Use Current Page as Home Address" changes it explicitly, and both URL
/// meanings survive a process relaunch.
///
/// Uses public `https://example.com/?…` pages (HTTP 200) with a per-run token
/// instead of the local HTTPS fixture server of the real-E2E suite.
final class MobileSavedPageHomeUITests: MobileBrowserUITestCase {
    private let stateTimeout: TimeInterval = 10
    private let networkTimeout: TimeInterval = 20
    private let atHomeLabels = ["An der Ausgangsadresse", "At Home Address"]
    private let awayLabels = ["Nicht an der Ausgangsadresse", "Away from Home Address"]

    @MainActor
    func testSavedPageHomeAddressReturnSetAndRestore() throws {
        let token = UUID().uuidString.lowercased().prefix(8)
        let firstHome = try XCTUnwrap(URL(string: "https://example.com/?ahoi-home=\(token)"))
        let away = try XCTUnwrap(URL(string: "https://example.com/?ahoi-away=\(token)"))
        let app = launchExactCandidate(arguments: [])
        defer { app.terminate() }

        // A fresh normal tab keeps the saved page independent of earlier runs.
        createNormalTab(in: app)
        openPage(firstHome, in: app)
        saveSelectedPage(to: "Inbox", in: app)
        let homeState = app.staticTexts["browser.actions.home-state"]
        XCTAssertTrue(
            openActions(revealing: homeState, in: app),
            "Saving must expose the saved page's Home Address rows."
        )
        XCTAssertTrue(waitForLabel(of: homeState, in: atHomeLabels))
        let help = app.staticTexts["browser.actions.home-help"]
        XCTAssertTrue(help.exists)
        XCTAssertTrue(
            help.label.hasPrefix("Die Ausgangsadresse ist der feste Startpunkt") ||
                help.label.hasPrefix("A Home Address is this saved page"),
            "Unexpected Home help: '\(help.label)'."
        )
        XCTAssertFalse(
            app.buttons["browser.actions.home-return"].isEnabled,
            "At the Home Address there is nowhere to return to."
        )
        attach(app, "home-at-home-after-save")
        closeActions(in: app)

        // Browsing away changes only the current URL.
        openPage(away, in: app)
        XCTAssertTrue(openActions(revealing: homeState, in: app))
        XCTAssertTrue(waitForLabel(of: homeState, in: awayLabels))
        attach(app, "home-away")
        let goHome = app.buttons["browser.actions.home-return"]
        XCTAssertTrue(waitForHittable(goHome, timeout: stateTimeout))
        XCTAssertTrue(goHome.isEnabled)
        XCTAssertTrue(waitForStableFrame(of: goHome))
        goHome.tap()
        assertAddress(firstHome, containsOrigin: "example.com", in: app)
        XCTAssertTrue(openActions(revealing: homeState, in: app))
        XCTAssertTrue(waitForLabel(of: homeState, in: atHomeLabels))
        closeActions(in: app)

        // Explicitly make the away page the new Home Address.
        openPage(away, in: app)
        waitForPageLoaded(in: app)
        XCTAssertTrue(openActions(revealing: homeState, in: app))
        XCTAssertTrue(waitForLabel(of: homeState, in: awayLabels))
        let setHome = app.buttons["browser.actions.home-set"]
        XCTAssertTrue(waitForHittable(setHome, timeout: stateTimeout))
        XCTAssertTrue(waitForEnabled(setHome), "A loaded away page can become the Home Address.")
        XCTAssertTrue(waitForStableFrame(of: setHome))
        setHome.tap()
        XCTAssertTrue(
            waitForLabel(of: homeState, in: atHomeLabels),
            "The current page must now be the Home Address."
        )
        attach(app, "home-set-to-current")
        closeActions(in: app)

        // Leave the new Home so current and Home differ across the relaunch.
        openPage(firstHome, in: app)
        XCTAssertTrue(openActions(revealing: homeState, in: app))
        XCTAssertTrue(waitForLabel(of: homeState, in: awayLabels))
        closeActions(in: app)

        relaunchExactCandidate(app)
        assertAddress(firstHome, containsOrigin: "example.com", in: app)
        XCTAssertTrue(openActions(revealing: homeState, in: app))
        XCTAssertTrue(
            waitForLabel(of: homeState, in: awayLabels),
            "The relaunched tab must keep its current URL apart from its Home Address."
        )
        attach(app, "home-away-after-relaunch")
        let restoredGoHome = app.buttons["browser.actions.home-return"]
        XCTAssertTrue(waitForHittable(restoredGoHome, timeout: stateTimeout))
        XCTAssertTrue(waitForStableFrame(of: restoredGoHome))
        restoredGoHome.tap()
        assertAddress(away, containsOrigin: "example.com", in: app)
        XCTAssertTrue(openActions(revealing: homeState, in: app))
        XCTAssertTrue(
            waitForLabel(of: homeState, in: atHomeLabels),
            "The explicitly set Home Address must survive the relaunch."
        )
        attach(app, "home-returned-after-relaunch")
        closeActions(in: app)
    }

    // MARK: - Helpers

    @MainActor
    private func openPage(_ url: URL, in app: XCUIApplication) {
        // `isHittable` is a false signal for the address over a loaded page
        // (79828c0); the editor field proves the tap landed.
        let address = app.buttons["browser.address"]
        XCTAssertTrue(address.waitForExistence(timeout: stateTimeout))
        address.tap()
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(waitForHittable(field, timeout: stateTimeout))
        clearAddressEditor(field, in: app)
        enterExactAddress(url.absoluteString, into: field, in: app)
        let go = app.buttons["browser.search.navigate"]
        XCTAssertTrue(waitForHittable(go, timeout: stateTimeout))
        go.tap()
        XCTAssertTrue(field.waitForNonExistence(timeout: stateTimeout))
        assertAddress(url, containsOrigin: "example.com", in: app)
        waitForPageLoaded(in: app)
    }

    @MainActor
    private func waitForPageLoaded(in app: XCUIApplication) {
        XCTAssertTrue(
            exampleDomainPage(in: app)
                .waitForExistence(timeout: networkTimeout),
            "The public example.com page must load."
        )
    }

    @MainActor
    private func createNormalTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: stateTimeout))
        more.tap()
        let newTab = app.buttons["browser.actions.new-tab"]
        XCTAssertTrue(waitForHittable(newTab, timeout: stateTimeout))
        newTab.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.header"]
                .waitForExistence(timeout: stateTimeout)
        )
    }

    @MainActor
    private func saveSelectedPage(to workspace: String, in app: XCUIApplication) {
        let save = app.buttons["browser.actions.save-to-workspace"]
        let destination = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label == %@",
            "browser.actions.save-to-workspace.", workspace
        )).firstMatch
        var opened = false
        for _ in 0..<3 where !opened {
            XCTAssertTrue(openActions(revealing: save, in: app))
            // A tap during scroll momentum only stops the list; wait until
            // the menu row is at rest before opening it.
            XCTAssertTrue(waitForStableFrame(of: save))
            save.tap()
            opened = destination.waitForExistence(timeout: 5)
        }
        XCTAssertTrue(opened, "The Save to Workspace menu must offer '\(workspace)'.")
        // UIMenu rows report `hittable == false` although they are on screen;
        // XCUI's own element tap still activates them.
        XCTAssertTrue(waitForStableFrame(of: destination))
        destination.tap()
        XCTAssertTrue(destination.waitForNonExistence(timeout: stateTimeout))
        closeActions(in: app)
    }

    @MainActor
    private func waitForStableFrame(of element: XCUIElement) -> Bool {
        var previous = element.frame
        let deadline = Date().addingTimeInterval(stateTimeout)
        while Date() < deadline {
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
            let current = element.frame
            if current == previous, !current.isEmpty { return true }
            previous = current
        }
        return false
    }

    /// Opens the actions sheet and scrolls until `element` is hittable. Saved
    /// state settles asynchronously, so a missing row reopens the sheet.
    @MainActor
    private func openActions(revealing element: XCUIElement, in app: XCUIApplication) -> Bool {
        for _ in 0..<4 {
            closeActions(in: app)
            let more = app.buttons["browser.more"]
            XCTAssertTrue(waitForHittable(more, timeout: stateTimeout))
            more.tap()
            let sheetReady = app.buttons["browser.actions.done"]
            XCTAssertTrue(sheetReady.waitForExistence(timeout: stateTimeout))
            if element.waitForExistence(timeout: 2), element.isHittable { return true }
            // Drag the sheet's list itself: a swipe that starts on an action
            // row can activate it under load (Reader was opened that way).
            // Lazy rows leave the tree once scrolled, so fix the list frame once.
            let list = app.collectionViews["browser.actions.list"]
            guard list.waitForExistence(timeout: stateTimeout) else { continue }
            let frame = list.frame
            let origin = app.coordinate(withNormalizedOffset: .zero)
            for _ in 0..<6 {
                let x = frame.maxX - 8
                let start = origin.withOffset(CGVector(dx: x, dy: frame.minY + frame.height * 0.8))
                let end = origin.withOffset(CGVector(dx: x, dy: frame.minY + frame.height * 0.3))
                start.press(forDuration: 0.05, thenDragTo: end)
                if element.waitForExistence(timeout: 1), element.isHittable { return true }
            }
        }
        return false
    }

    @MainActor
    private func closeActions(in app: XCUIApplication) {
        let done = app.buttons["browser.actions.done"]
        guard done.waitForExistence(timeout: 1) else { return }
        if waitForHittable(done, timeout: stateTimeout) { done.tap() }
        XCTAssertTrue(done.waitForNonExistence(timeout: stateTimeout))
    }

    @MainActor
    private func waitForLabel(of element: XCUIElement, in labels: [String]) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "label IN %@", labels),
            object: element
        )
        let completed = XCTWaiter.wait(for: [expectation], timeout: stateTimeout) == .completed
        if !completed {
            XCTFail("Expected one of \(labels), got '\(element.label)'.")
        }
        return completed
    }

    @MainActor
    private func waitForEnabled(_ element: XCUIElement) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "enabled == true"),
            object: element
        )
        return XCTWaiter.wait(for: [expectation], timeout: stateTimeout) == .completed
    }

    @MainActor
    private func attach(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
