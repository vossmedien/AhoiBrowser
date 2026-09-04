import XCTest

final class MobileBrowserTabWorkspaceRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testNormalTabReorderPersistsAcrossProcessRelaunch() async throws {
        let fixture = try await requireReachableFixture()
        let token = UUID().uuidString.lowercased()
        let urls = (1...3).map {
            fixture.url(path: "/navigation?normal-reorder=\($0)&run=\(token)")
        }
        let app = coldLaunchApplication()
        defer { app.terminate() }

        normalizeNormalPopulationToOne(in: app)
        navigate(to: urls[0], in: app)
        XCTAssertTrue(
            app.webViews.staticTexts["Redirect and popup controls"]
                .waitForExistence(timeout: 8)
        )
        for url in urls.dropFirst() {
            createNormalTab(in: app)
            navigate(to: url, in: app)
            XCTAssertTrue(
                app.webViews.staticTexts["Redirect and popup controls"]
                    .waitForExistence(timeout: 8)
            )
        }

        openTabSwitcher(in: app)
        let before = try stableRowIDs(for: urls, in: app)
        XCTAssertEqual(before.count, 3)
        let edit = app.buttons["browser.tabs.edit"]
        XCTAssertTrue(waitForHittable(edit, timeout: 3))
        edit.tap()
        let reorderList = app.descendants(matching: .any)["browser.tabs.reorder-list"]
        XCTAssertTrue(reorderList.waitForExistence(timeout: 3))

        let rows = try visibleRows(for: urls, in: app)
        let applicationOrigin = app.coordinate(withNormalizedOffset: CGVector(dx: 0, dy: 0))
        let gripX = reorderList.frame.maxX - 18
        let source = applicationOrigin.withOffset(CGVector(
            dx: gripX,
            dy: rows[0].frame.midY
        ))
        let target = applicationOrigin.withOffset(CGVector(
            dx: gripX,
            dy: rows[2].frame.maxY - 4
        ))
        source.press(forDuration: 0.8, thenDragTo: target)

        let after = try XCTUnwrap(waitForChangedOrder(
            from: before,
            urls: urls,
            in: app,
            timeout: 5
        ))
        XCTAssertEqual(Set(after), Set(before))
        XCTAssertNotEqual(after, before)
        app.buttons["browser.tabs.done"].tap()
        XCTAssertTrue(reorderList.waitForNonExistence(timeout: 4))

        flushThroughVisibleBackgroundTransition(app)
        relaunchExactCandidate(app)
        openTabSwitcher(in: app)
        XCTAssertEqual(
            try stableRowIDs(for: urls, in: app),
            after,
            "Normal-tab order must survive the candidate's persisted process restoration."
        )
        closeRows(for: urls, in: app)
    }

    @MainActor
    func testSavedNormalTabMovesBetweenWorkspacesAndRestoresInDestination() async throws {
        let fixture = try await requireReachableFixture()
        let token = UUID().uuidString.lowercased().prefix(8)
        let sourceWorkspace = "Move Harbor \(token)-A"
        let destinationWorkspace = "Move Harbor \(token)-B"
        let pageURL = fixture.url(path: "/navigation?workspace-move=\(token)")
        let app = coldLaunchApplication()
        defer {
            deleteWorkspaceIfPresent(named: sourceWorkspace, in: app)
            deleteWorkspaceIfPresent(named: destinationWorkspace, in: app)
            app.terminate()
        }

        openLibrary(in: app)
        createWorkspace(named: sourceWorkspace, in: app)
        createWorkspace(named: destinationWorkspace, in: app)
        closeLibrary(in: app)

        navigate(to: pageURL, in: app)
        saveSelectedPage(to: sourceWorkspace, in: app)
        openTabSwitcher(in: app)
        XCTAssertTrue(
            sectionHeader(
                workspace: sourceWorkspace,
                kindLabels: ["Saved", "Gespeichert"],
                in: app
            ).waitForExistence(timeout: 5),
            "The save must visibly settle in the source workspace before it is moved."
        )
        let sourceRow = row(containing: pageURL.absoluteString, in: app)
        XCTAssertTrue(sourceRow.waitForExistence(timeout: 5))
        sourceRow.press(forDuration: 1.1)

        let move = localizedButton(
            labels: ["Move to Workspace", "In Workspace verschieben"],
            in: app
        )
        XCTAssertTrue(move.waitForExistence(timeout: 4))
        move.tap()
        let destination = app.buttons[destinationWorkspace]
        XCTAssertTrue(destination.waitForExistence(timeout: 4))
        destination.tap()

        XCTAssertTrue(
            sectionHeader(
                workspace: destinationWorkspace,
                kindLabels: ["Saved", "Gespeichert"],
                in: app
            ).waitForExistence(timeout: 5),
            "The moved saved tab must visibly join the destination workspace section."
        )
        XCTAssertTrue(row(containing: pageURL.absoluteString, in: app).exists)
        app.buttons["browser.tabs.done"].tap()

        flushThroughVisibleBackgroundTransition(app)
        relaunchExactCandidate(app)
        openTabSwitcher(in: app)
        XCTAssertTrue(
            sectionHeader(
                workspace: destinationWorkspace,
                kindLabels: ["Saved", "Gespeichert"],
                in: app
            ).waitForExistence(timeout: 5),
            "The destination workspace assignment must survive process restoration."
        )
        let restored = row(containing: pageURL.absoluteString, in: app)
        XCTAssertTrue(restored.waitForExistence(timeout: 5))
        restored.tap()
        assertAddress(pageURL, containsOrigin: fixture.origin, in: app)
    }

    @MainActor
    func testTargetBlankCreatesOneNormalThirdPartyTabWithDestinationAttribution() async throws {
        let fixture = try await requireReachableFixture()
        let app = coldLaunchApplication()
        defer { app.terminate() }
        navigate(to: fixture.url(path: "/navigation"), in: app)

        let tabs = app.buttons["browser.tabs"]
        let countBefore = try XCTUnwrap(tabCount(from: tabs.label))
        let sourceURL = try XCTUnwrap(app.buttons["browser.address"].value as? String)
        let webView = app.webViews.firstMatch
        let link = webView.links["noopener popup"]
        XCTAssertTrue(link.waitForExistence(timeout: 5))
        link.tap()

        XCTAssertTrue(
            app.webViews.staticTexts["Synthetic popup"].waitForExistence(timeout: 10),
            "The target-blank document must render through a newly selected WebKit page."
        )
        XCTAssertTrue(waitForTabCount(countBefore + 1, in: tabs, timeout: 8))
        let popupAddress = try XCTUnwrap(app.buttons["browser.address"].value as? String)
        XCTAssertNotEqual(popupAddress, sourceURL)
        XCTAssertTrue(
            popupAddress.contains("third-party.localhost"),
            "The selected popup must retain the fixture's third-party destination origin."
        )

        openTabSwitcher(in: app)
        let popupRow = row(containing: "Synthetic popup", in: app)
        XCTAssertTrue(popupRow.waitForExistence(timeout: 5))
        XCTAssertTrue(
            popupRow.label.contains("third-party.localhost"),
            "The popup row must visibly preserve destination attribution."
        )
        closeRows(matching: "third-party.localhost", in: app)
    }

    @MainActor
    private func createNormalTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: 5))
        more.tap()
        let newTab = app.buttons["browser.actions.new-tab"]
        XCTAssertTrue(waitForHittable(newTab, timeout: 3))
        newTab.tap()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 3))
    }

    @MainActor
    private func normalizeNormalPopulationToOne(in app: XCUIApplication) {
        openTabSwitcher(in: app)
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        mode.coordinate(withNormalizedOffset: CGVector(dx: 0.25, dy: 0.5)).tap()
        let closeButtons = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-close."
        ))
        var attempts = 0
        while closeButtons.count > 1, attempts < 40 {
            attempts += 1
            let close = (0..<closeButtons.count)
                .map { closeButtons.element(boundBy: $0) }
                .first(where: \.isHittable)
            guard let close else {
                XCTFail("Normal-tab cleanup must retain a hittable close control.")
                return
            }
            let identifier = close.identifier
            close.tap()
            XCTAssertTrue(app.buttons[identifier].waitForNonExistence(timeout: 3))
        }
        XCTAssertEqual(closeButtons.count, 1)
        let remainingRow = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.tab-row."
        )).firstMatch
        XCTAssertTrue(waitForHittable(remainingRow, timeout: 3))
        remainingRow.tap()
        XCTAssertTrue(mode.waitForNonExistence(timeout: 3))
        XCTAssertTrue(waitForTabCount(1, in: app.buttons["browser.tabs"], timeout: 3))
    }

    @MainActor
    private func openTabSwitcher(in app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForHittable(tabs, timeout: 5))
        tabs.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.list"]
                .waitForExistence(timeout: 4)
        )
    }

    @MainActor
    private func row(containing text: String, in app: XCUIApplication) -> XCUIElement {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.tab-row.",
            text
        )).firstMatch
    }

    @MainActor
    private func visibleRows(for urls: [URL], in app: XCUIApplication) throws -> [XCUIElement] {
        let rows = urls.map { row(containing: $0.absoluteString, in: app) }
        for row in rows {
            XCTAssertTrue(row.waitForExistence(timeout: 5))
        }
        let visible = rows.filter { $0.exists && !$0.frame.isEmpty }.sorted {
            $0.frame.minY < $1.frame.minY
        }
        guard visible.count == urls.count else {
            throw TabWorkspaceE2EContractError.missingVisibleRows
        }
        return visible
    }

    @MainActor
    private func stableRowIDs(for urls: [URL], in app: XCUIApplication) throws -> [String] {
        try visibleRows(for: urls, in: app).map(\.identifier)
    }

    @MainActor
    private func waitForChangedOrder(
        from original: [String],
        urls: [URL],
        in app: XCUIApplication,
        timeout: TimeInterval
    ) -> [String]? {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if let current = try? stableRowIDs(for: urls, in: app), current != original {
                return current
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
        return nil
    }

    @MainActor
    private func closeRows(for urls: [URL], in app: XCUIApplication) {
        for url in urls { closeRows(matching: url.absoluteString, in: app) }
    }

    @MainActor
    private func closeRows(matching text: String, in app: XCUIApplication) {
        while true {
            let target = row(containing: text, in: app)
            guard target.exists else { return }
            let suffix = target.identifier.replacingOccurrences(
                of: "browser.tab-row.",
                with: ""
            )
            let close = app.buttons["browser.tab-close.\(suffix)"]
            guard close.waitForExistence(timeout: 2) else { return }
            close.tap()
        }
    }

    @MainActor
    private func flushThroughVisibleBackgroundTransition(_ app: XCUIApplication) {
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(
            app.wait(for: .runningBackground, timeout: 5)
                || app.wait(for: .runningBackgroundSuspended, timeout: 5)
        )
        app.activate()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 5))
        assertExactCandidateBinding(in: app)
    }

    @MainActor
    private func openLibrary(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: 5))
        more.tap()
        let workspaces = app.buttons["browser.actions.workspaces"]
        XCTAssertTrue(waitForHittable(workspaces, timeout: 4))
        workspaces.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForExistence(timeout: 5)
        )
    }

    @MainActor
    private func closeLibrary(in app: XCUIApplication) {
        let done = app.buttons["browser.library.done"]
        XCTAssertTrue(waitForHittable(done, timeout: 3))
        done.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForNonExistence(timeout: 4)
        )
    }

    @MainActor
    private func createWorkspace(named name: String, in app: XCUIApplication) {
        let manage = app.buttons["browser.library.manage"]
        XCTAssertTrue(waitForHittable(manage, timeout: 4))
        manage.tap()
        let create = app.buttons["browser.library.create.workspace"]
        XCTAssertTrue(waitForHittable(create, timeout: 3))
        create.tap()
        let identified = app.textFields["browser.library.create.name"]
        let fallback = app.alerts.firstMatch.textFields.firstMatch
        let field = identified.waitForExistence(timeout: 1) ? identified : fallback
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        field.tap()
        field.typeText(name)
        let confirm = app.alerts.firstMatch
            .buttons["browser.library.create.confirm"]
            .firstMatch
        XCTAssertTrue(confirm.waitForExistence(timeout: 3))
        confirm.tap()
        XCTAssertTrue(app.staticTexts[name].waitForExistence(timeout: 5))
    }

    @MainActor
    private func saveSelectedPage(to workspace: String, in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: 4))
        more.tap()
        let save = app.buttons["browser.actions.save-to-workspace"]
        reveal(save, in: app)
        save.tap()
        let destination = app.buttons[workspace]
        XCTAssertTrue(destination.waitForExistence(timeout: 4))
        destination.tap()
        dismissActionsIfPresent(in: app)
    }

    @MainActor
    private func sectionHeader(
        workspace: String,
        kindLabels: [String],
        in app: XCUIApplication
    ) -> XCUIElement {
        let labels = kindLabels.map { "\(workspace) · \($0)" }
        return app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            labels
        )).firstMatch
    }

    @MainActor
    private func localizedButton(labels: [String], in app: XCUIApplication) -> XCUIElement {
        app.buttons.matching(NSPredicate(format: "label IN %@", labels)).firstMatch
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        for _ in 0..<8 where !element.isHittable { app.swipeUp() }
        XCTAssertTrue(element.waitForExistence(timeout: 3))
        XCTAssertTrue(element.isHittable)
    }

    @MainActor
    private func dismissActionsIfPresent(in app: XCUIApplication) {
        if app.buttons["browser.actions.done"].waitForExistence(timeout: 1) {
            app.buttons["browser.actions.done"].tap()
        }
    }

    @MainActor
    private func tabCount(from label: String) -> Int? {
        Int(label.prefix(while: { $0.isNumber }))
    }

    @MainActor
    private func waitForTabCount(
        _ expectedCount: Int,
        in tabs: XCUIElement,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if tabCount(from: tabs.label) == expectedCount { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        } while Date() < deadline
        return tabCount(from: tabs.label) == expectedCount
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
        let workspace = app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.library.workspace.",
            name
        )).firstMatch
        guard workspace.waitForExistence(timeout: 3) else { return }
        workspace.press(forDuration: 1.1)
        let delete = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@",
            "browser.library.workspace.delete."
        )).firstMatch
        guard delete.waitForExistence(timeout: 3) else { return }
        delete.tap()
        let confirm = app.sheets.firstMatch
            .buttons["browser.library.workspace.delete.confirm"]
            .firstMatch
        guard confirm.waitForExistence(timeout: 3) else { return }
        confirm.tap()
    }
}

private enum TabWorkspaceE2EContractError: Error {
    case missingVisibleRows
}
