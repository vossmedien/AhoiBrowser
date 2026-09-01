import Foundation
import XCTest

/// Visible closure journeys that deliberately exercise product controls. Tests
/// named Integration document DEBUG launch seams and are not release acceptance.
/// No test reaches into `MobileBrowserController` or companion model state.
final class MobileBrowserClosureRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testIntegrationConfiguredFixtureSearchProviderRendersVisibleResult() async throws {
        let fixture = try await requireReachableFixture()
        let token = UUID().uuidString.lowercased().prefix(8)
        let phrase = "ahoi fixture voyage \(token)"
        let providerName = "Ahoi Fixture Search Provider"
        let providerTemplate = "\(fixture.securityOrigin)/navigation?fixtureSearch=%@"
        let expectedSearchURL = try searchURL(
            template: providerTemplate,
            phrase: phrase
        )
        let app = launchFixture(extraArguments: [
            "-AhoiUITestSearchProviderTemplate", providerTemplate,
            "-AhoiUITestSearchProviderName", providerName,
        ])

        openAddressEditor(in: app)
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        clearAddressEditor(field, in: app)
        enterExactAddress(phrase, into: field, in: app)

        let submit = app.buttons["browser.search.navigate"]
        XCTAssertTrue(submit.waitForExistence(timeout: 3))
        XCTAssertTrue(
            app.staticTexts[providerName].waitForExistence(timeout: 3),
            "The configured provider must be named on the visible search action."
        )
        submit.tap()

        let webView = app.webViews.firstMatch
        XCTAssertTrue(
            webView.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )
        assertAddress(expectedSearchURL, containsOrigin: fixture.origin, in: app)

        let renderedPhrase = webView.staticTexts["Search phrase: \(phrase)"]
        reveal(renderedPhrase, in: webView)
        let result = webView.links["Open fixture search result"]
        reveal(result, in: webView)
        result.tap()

        let resultURL = fixture.url(path: "/popup?from=fixture-search")
        XCTAssertTrue(
            webView.staticTexts["Synthetic popup"].waitForExistence(timeout: 8),
            "A real result click must navigate the selected WebKit page."
        )
        assertAddress(resultURL, containsOrigin: fixture.origin, in: app)
        attachScreenshot(named: "Configured provider result navigation", of: app)
    }

    @MainActor
    func testVisibleSearchProviderSelectionPersistsAcrossRelaunch() async throws {
        _ = try await requireReachableFixture()
        let app = launchExactCandidate(arguments: [])
        openSettings(in: app)

        let picker = app.descendants(matching: .any)["settings.search.engine"]
        XCTAssertTrue(picker.waitForExistence(timeout: 5))
        let original = try XCTUnwrap(
            selectedSearchEngineName(from: picker),
            "Settings must visibly expose its current built-in search provider."
        )
        let target = original == "Bing" ? "Google" : "Bing"
        defer {
            if app.state != .runningForeground { app.activate() }
            dismissAddressEditorIfPresent(in: app)
            if !app.buttons["settings.done"].exists {
                openSettings(in: app)
            }
            let cleanupPicker = app.descendants(matching: .any)["settings.search.engine"]
            selectSearchEngine(named: original, with: cleanupPicker, in: app)
            closeSettings(in: app)
            app.terminate()
        }

        selectSearchEngine(named: target, with: picker, in: app)
        closeSettings(in: app)
        relaunchExactCandidate(app)

        openSettings(in: app)
        let restoredPicker = app.descendants(matching: .any)["settings.search.engine"]
        XCTAssertTrue(restoredPicker.waitForExistence(timeout: 5))
        XCTAssertEqual(
            selectedSearchEngineName(from: restoredPicker),
            target,
            "The provider selected through Settings must survive a real process relaunch."
        )
        closeSettings(in: app)

        openAddressEditor(in: app)
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        clearAddressEditor(field, in: app)
        enterExactAddress("visible provider persistence", into: field, in: app)
        XCTAssertTrue(
            app.staticTexts[target].waitForExistence(timeout: 4),
            "The persisted provider must visibly name the address action after relaunch."
        )
        dismissAddressEditorIfPresent(in: app)
    }

    @MainActor
    func testSavePageToWorkspaceThenOpenFromTreeAndLibrarySearch() async throws {
        let fixture = try await requireReachableFixture()
        let token = UUID().uuidString.lowercased()
        let workspaceName = "Closure Harbor \(token.prefix(8))"
        let pageURL = fixture.url(path: "/navigation?workspaceClosure=\(token)")
        let app = launchFixture()
        defer {
            deleteWorkspaceIfPresent(named: workspaceName, in: app)
            app.terminate()
        }

        openLibrary(in: app)
        createWorkspace(named: workspaceName, in: app)
        closeLibrary(in: app)

        navigate(to: pageURL, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )

        openActions(in: app)
        let saveMenu = app.buttons["browser.actions.save-to-workspace"]
        reveal(saveMenu, in: app)
        saveMenu.tap()
        let destination = app.buttons[workspaceName]
        XCTAssertTrue(
            destination.waitForExistence(timeout: 4),
            "The visible save menu must expose the workspace created through UI."
        )
        destination.tap()
        closeActions(in: app)

        let beforeTreeOpenURL = fixture.url(
            path: "/popup?workspaceClosure=before-tree-open&token=\(token)"
        )
        navigate(to: beforeTreeOpenURL, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Synthetic popup"].waitForExistence(timeout: 8)
        )

        openLibrary(in: app)
        selectWorkspace(named: workspaceName, in: app)
        let savedPage = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.library.saved-page."
        )).firstMatch
        XCTAssertTrue(
            savedPage.waitForExistence(timeout: 8),
            "Saving the active page must create a visible saved-page tree row."
        )
        savedPage.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForNonExistence(timeout: 5)
        )
        assertAddress(pageURL, containsOrigin: fixture.origin, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )

        let beforeSearchOpenURL = fixture.url(
            path: "/privacy?workspaceClosure=before-search-open&token=\(token)"
        )
        navigate(to: beforeSearchOpenURL, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts[
                "Cookies, CHIPS, GPC, referrer and tracking"
            ].waitForExistence(timeout: 8)
        )

        openLibrary(in: app)
        let search = librarySearchField(in: app)
        XCTAssertTrue(search.waitForExistence(timeout: 5))
        search.tap()
        search.typeText(token)
        let result = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.library.search-result.savedPage."
        )).firstMatch
        XCTAssertTrue(
            result.waitForExistence(timeout: 8),
            "Library search must surface the saved page by its unique URL token."
        )
        result.tap()

        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForNonExistence(timeout: 5)
        )
        assertAddress(pageURL, containsOrigin: fixture.origin, in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )
        attachScreenshot(named: "Workspace tree and search reopening", of: app)
    }

    @MainActor
    func testVisibleMemoryWarningIntegrationDiscardsAndReloadsInactivePage() async throws {
        let fixture = try await requireReachableFixture()
        let token = UUID().uuidString.lowercased()
        let oldToken = "memory-old-\(token)"
        let selectedToken = "memory-selected-\(token)"
        let recoveryURL = fixture.url(
            path: "/navigation?fixtureSearch=\(oldToken)&recovery=memory"
        )
        let app = launchFixture(extraArguments: ["-AhoiUITestMemoryPressureControl"])

        navigate(to: recoveryURL, in: app)
        activatePageOnlyMarker(renderedToken: oldToken, in: app)
        createNormalTab(in: app)
        let selectedURL = fixture.url(
            path: "/navigation?fixtureSearch=\(selectedToken)&recovery=memory-selected"
        )
        navigate(to: selectedURL, in: app)
        activatePageOnlyMarker(renderedToken: selectedToken, in: app)

        openActions(in: app)
        let memoryWarning = app.buttons["browser.e2e.memory-warning"]
        reveal(memoryWarning, in: app)
        memoryWarning.tap()
        XCTAssertTrue(
            memoryWarning.waitForNonExistence(timeout: 4),
            "The visible event control must dismiss before tab restoration."
        )

        assertAddress(selectedURL, containsOrigin: fixture.origin, in: app)
        assertPageOnlyMarkerIsActive(renderedToken: selectedToken, in: app)

        selectTab(containing: recoveryURL.absoluteString, in: app)
        assertReloadedRecoveryDocument(
            recoveryURL,
            renderedToken: oldToken,
            fixture: fixture,
            in: app
        )
        attachScreenshot(named: "Visible memory warning integration restoration", of: app)
    }

    @MainActor
    func testBackgroundForegroundDiscardsOldestPageAndRestoresItVisibly() async throws {
        let fixture = try await requireReachableFixture()
        let token = UUID().uuidString.lowercased()
        let oldToken = "background-old-\(token)"
        let middleToken = "background-middle-\(token)"
        let selectedToken = "background-selected-\(token)"
        let recoveryURL = fixture.url(
            path: "/navigation?fixtureSearch=\(oldToken)&recovery=background"
        )
        let app = launchFixture()

        navigate(to: recoveryURL, in: app)
        activatePageOnlyMarker(renderedToken: oldToken, in: app)

        createNormalTab(in: app)
        let middleURL = fixture.url(
            path: "/navigation?fixtureSearch=\(middleToken)&recovery=background-middle"
        )
        navigate(to: middleURL, in: app)
        activatePageOnlyMarker(renderedToken: middleToken, in: app)
        createNormalTab(in: app)
        let selectedURL = fixture.url(
            path: "/navigation?fixtureSearch=\(selectedToken)&recovery=background-selected"
        )
        navigate(to: selectedURL, in: app)
        activatePageOnlyMarker(renderedToken: selectedToken, in: app)

        XCUIDevice.shared.press(.home)
        XCTAssertTrue(
            waitUntil(timeout: 8) {
                app.state == .runningBackground ||
                    app.state == .runningBackgroundSuspended
            },
            "The candidate must visibly cross the real simulator background boundary."
        )
        XCTAssertNotEqual(
            app.state,
            .notRunning,
            "A terminated process is not evidence for the background-discard policy."
        )
        try await Task.sleep(for: .milliseconds(750))
        app.activate()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))

        assertAddress(selectedURL, containsOrigin: fixture.origin, in: app)
        assertPageOnlyMarkerIsActive(renderedToken: selectedToken, in: app)
        selectTab(containing: middleURL.absoluteString, in: app)
        assertAddress(middleURL, containsOrigin: fixture.origin, in: app)
        assertPageOnlyMarkerIsActive(renderedToken: middleToken, in: app)

        selectTab(containing: recoveryURL.absoluteString, in: app)
        assertReloadedRecoveryDocument(
            recoveryURL,
            renderedToken: oldToken,
            fixture: fixture,
            in: app
        )
        attachScreenshot(named: "Background foreground page restoration", of: app)
    }

    private func searchURL(template: String, phrase: String) throws -> URL {
        let allowed = CharacterSet.urlQueryAllowed.subtracting(
            CharacterSet(charactersIn: "+&=")
        )
        let encoded = try XCTUnwrap(
            phrase.addingPercentEncoding(withAllowedCharacters: allowed)
        )
        return try XCTUnwrap(URL(
            string: template.replacingOccurrences(of: "%@", with: encoded)
        ))
    }

    @MainActor
    private func launchFixture(extraArguments: [String] = []) -> XCUIApplication {
        launchExactCandidate(arguments: ["-AhoiUITestFixture"] + extraArguments)
    }

    @MainActor
    private func openSettings(in app: XCUIApplication) {
        openActions(in: app)
        let settings = app.buttons["browser.actions.settings"]
        reveal(settings, in: app)
        settings.tap()
        XCTAssertTrue(app.buttons["settings.done"].waitForExistence(timeout: 5))
    }

    @MainActor
    private func closeSettings(in app: XCUIApplication) {
        let done = app.buttons["settings.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3))
        done.tap()
        XCTAssertTrue(done.waitForNonExistence(timeout: 4))
    }

    @MainActor
    private func selectSearchEngine(
        named name: String,
        with picker: XCUIElement,
        in app: XCUIApplication
    ) {
        guard selectedSearchEngineName(from: picker) != name else { return }
        picker.tap()
        let option = app.descendants(matching: .any).matching(NSPredicate(
            format: "label == %@",
            name
        )).firstMatch
        XCTAssertTrue(option.waitForExistence(timeout: 4))
        XCTAssertTrue(option.isHittable)
        option.tap()
        XCTAssertTrue(
            waitUntil(timeout: 4) { self.selectedSearchEngineName(from: picker) == name },
            "Settings must visibly report the newly selected search provider."
        )
    }

    @MainActor
    private func selectedSearchEngineName(from picker: XCUIElement) -> String? {
        let projection = "\(picker.label) \(picker.value as? String ?? "")"
        return ["DuckDuckGo", "Google", "Bing"].first { projection.contains($0) }
    }

    @MainActor
    private func dismissAddressEditorIfPresent(in app: XCUIApplication) {
        guard app.textFields["browser.address.field"].exists else { return }
        let cancel = app.buttons.matching(NSPredicate(
            format: "label IN %@",
            ["Cancel", "Abbrechen"]
        )).firstMatch
        XCTAssertTrue(cancel.waitForExistence(timeout: 3))
        cancel.tap()
        XCTAssertTrue(app.textFields["browser.address.field"].waitForNonExistence(timeout: 4))
    }

    @MainActor
    private func openActions(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        XCTAssertTrue(app.buttons["browser.actions.done"].waitForExistence(timeout: 4))
    }

    @MainActor
    private func closeActions(in app: XCUIApplication) {
        let done = app.buttons["browser.actions.done"]
        if done.waitForExistence(timeout: 2) {
            done.tap()
            XCTAssertTrue(done.waitForNonExistence(timeout: 4))
        }
    }

    @MainActor
    private func createNormalTab(in app: XCUIApplication) {
        openActions(in: app)
        let newTab = app.buttons["browser.actions.new-tab"]
        XCTAssertTrue(newTab.waitForExistence(timeout: 3))
        newTab.tap()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 4))
    }

    @MainActor
    private func openLibrary(in app: XCUIApplication) {
        if app.descendants(matching: .any)["browser.library.root"].exists { return }
        openActions(in: app)
        let workspaces = app.buttons["browser.actions.workspaces"]
        XCTAssertTrue(workspaces.waitForExistence(timeout: 3))
        workspaces.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForExistence(timeout: 5)
        )
    }

    @MainActor
    private func closeLibrary(in app: XCUIApplication) {
        let done = app.buttons["browser.library.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3))
        done.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForNonExistence(timeout: 4)
        )
    }

    @MainActor
    private func createWorkspace(named name: String, in app: XCUIApplication) {
        let manage = app.buttons["browser.library.manage"]
        XCTAssertTrue(manage.waitForExistence(timeout: 4))
        manage.tap()
        let create = app.buttons["browser.library.create.workspace"]
        XCTAssertTrue(create.waitForExistence(timeout: 3))
        create.tap()

        let identified = app.textFields["browser.library.create.name"]
        let fallback = app.alerts.firstMatch.textFields.firstMatch
        let field = identified.waitForExistence(timeout: 1) ? identified : fallback
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        field.tap()
        field.typeText(name)
        let confirm = app.buttons["browser.library.create.confirm"]
        XCTAssertTrue(confirm.waitForExistence(timeout: 3))
        confirm.tap()
        XCTAssertTrue(app.staticTexts[name].waitForExistence(timeout: 5))
    }

    @MainActor
    private func selectWorkspace(named name: String, in app: XCUIApplication) {
        let row = app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.library.workspace.",
            name
        )).firstMatch
        if row.waitForExistence(timeout: 3) {
            row.tap()
        } else {
            let label = app.staticTexts[name]
            XCTAssertTrue(label.waitForExistence(timeout: 3))
            label.tap()
        }
    }

    @MainActor
    private func librarySearchField(in app: XCUIApplication) -> XCUIElement {
        let identified = app.searchFields["browser.library.search"]
        return identified.exists ? identified : app.searchFields.firstMatch
    }

    @MainActor
    private func activatePageOnlyMarker(
        renderedToken: String,
        in app: XCUIApplication
    ) {
        let webView = visibleWebView(rendering: renderedToken, in: app)
        let markerButton = webView.buttons["Activate page-only recovery marker"]
        reveal(markerButton, in: webView)
        markerButton.tap()
        XCTAssertTrue(
            webView.staticTexts["Page-only marker active."]
                .waitForExistence(timeout: 4)
        )
    }

    @MainActor
    private func assertReloadedRecoveryDocument(
        _ url: URL,
        renderedToken: String,
        fixture: FixtureContext,
        in app: XCUIApplication
    ) {
        let webView = visibleWebView(rendering: renderedToken, in: app)
        XCTAssertTrue(
            webView.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 10),
            "Selecting a discarded tab must recreate its real HTTPS document."
        )
        let resetMarker = webView.staticTexts[
            "Page-only marker reset after document load."
        ]
        reveal(resetMarker, in: webView)
        XCTAssertFalse(
            webView.staticTexts["Page-only marker active."].exists,
            "Document-only state must not survive page discard and reload."
        )
        assertAddress(url, containsOrigin: fixture.origin, in: app)
    }

    @MainActor
    private func assertPageOnlyMarkerIsActive(
        renderedToken: String,
        in app: XCUIApplication
    ) {
        let webView = visibleWebView(rendering: renderedToken, in: app)
        let activeMarker = webView.staticTexts["Page-only marker active."]
        reveal(activeMarker, in: webView)
        XCTAssertFalse(
            webView.staticTexts["Page-only marker reset after document load."].exists,
            "A retained page must preserve its document-only marker."
        )
    }

    @MainActor
    private func visibleWebView(
        rendering token: String,
        in app: XCUIApplication
    ) -> XCUIElement {
        let expectedText = "Search phrase: \(token)"
        let visibleWebViews = app.webViews.allElementsBoundByIndex.filter {
            $0.exists && $0.isHittable
        }
        XCTAssertEqual(
            visibleWebViews.count,
            1,
            "Exactly one active, hittable WebView must back the selected tab."
        )
        let webView = visibleWebViews.first ?? app.webViews.element(boundBy: 0)
        let identity = webView.staticTexts[expectedText]
        reveal(identity, in: webView)
        XCTAssertTrue(
            identity.isHittable,
            "The selected WebView must visibly render its unique per-tab token."
        )
        return webView
    }

    @MainActor
    private func selectTab(containing text: String, in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(tabs.waitForExistence(timeout: 5))
        tabs.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.list"]
                .waitForExistence(timeout: 4)
        )
        let row = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.tab-row.",
            text
        )).firstMatch
        XCTAssertTrue(row.waitForExistence(timeout: 5))
        row.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.list"]
                .waitForNonExistence(timeout: 4)
        )
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in container: XCUIElement) {
        XCTAssertTrue(element.waitForExistence(timeout: 8))
        for _ in 0..<10 where !element.isHittable {
            container.swipeUp()
        }
        XCTAssertTrue(element.isHittable)
    }

    @MainActor
    private func waitUntil(
        timeout: TimeInterval,
        condition: () -> Bool
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if condition() { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
        return condition()
    }

    @MainActor
    private func deleteWorkspaceIfPresent(named name: String, in app: XCUIApplication) {
        if app.state != .runningForeground { app.activate() }
        if !app.descendants(matching: .any)["browser.library.root"].exists {
            guard app.buttons["browser.more"].waitForExistence(timeout: 2) else { return }
            app.buttons["browser.more"].tap()
            guard app.buttons["browser.actions.workspaces"].waitForExistence(timeout: 2) else {
                return
            }
            app.buttons["browser.actions.workspaces"].tap()
        }
        let row = app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.library.workspace.",
            name
        )).firstMatch
        guard row.waitForExistence(timeout: 3) else { return }
        row.press(forDuration: 1.1)
        let delete = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.library.workspace.delete."
        )).firstMatch
        guard delete.waitForExistence(timeout: 3) else { return }
        delete.tap()
        let confirm = app.buttons["browser.library.workspace.delete.confirm"]
        guard confirm.waitForExistence(timeout: 3) else { return }
        confirm.tap()
    }

    @MainActor
    private func attachScreenshot(named name: String, of app: XCUIApplication) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
