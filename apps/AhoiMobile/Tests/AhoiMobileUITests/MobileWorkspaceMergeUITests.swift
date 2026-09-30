import XCTest

/// ADR 0012 section 1, WS-MERGE-07 (mobile form of WS-MERGE-01..03):
/// "Merge into…" on a Workspace in the library moves its saved pages and
/// folders into one new folder named after it at the end of the target,
/// removes the source, and the one-level undo restores the source with
/// its name and nodes.
///
/// The Workspaces are created through the library UI with a per-run
/// token and deleted again at the end.
final class MobileWorkspaceMergeUITests: MobileBrowserUITestCase {
    private let timeout = MobileBrowserUITestCase.adr0012StateTimeout

    @MainActor
    func testMergeIntoFolderThenUndoRestoresSourceWorkspace() throws {
        let token = UUID().uuidString.lowercased().prefix(6)
        let source = "Merge A \(token)"
        let target = "Merge B \(token)"
        let page = "Example Domain"
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer {
            deleteMergeWorkspaces(in: app)
            app.terminate()
        }

        deleteMergeWorkspaces(in: app)
        createWorkspace(named: target, in: app)
        returnToWorkspaceList(in: app)
        createWorkspace(named: source, in: app)
        returnToWorkspaceList(in: app)
        closeLibrary(in: app)

        // The open tab, on a public page, becomes a saved page of A. On
        // iPhone the library's Folder/Saved page items need a selected
        // Workspace, which the compact list does not keep, so the page is
        // saved from the browser like a person would.
        openExamplePage(try XCTUnwrap(URL(
            string: "https://example.com/?ahoi-merge=\(token)"
        )), in: app)
        saveSelectedPage(to: source, in: app)
        let deckWorkspace = app.descendants(matching: .any)[
            "browser.harbor-deck.workspace"
        ]
        let tabWasInSource = deckWorkspace.waitForExistence(timeout: 3)
            && deckWorkspace.label == source
        showTabSwitcher(app)
        let savedInSource = savedSection(of: source, in: app)
            .waitForExistence(timeout: timeout)
        attachEvidence(app, "merge-source-before-switcher")
        if !savedInSource { attachTree(app, "merge-source-before-tree") }
        XCTAssertTrue(savedInSource, "A must hold the saved page first.")
        app.buttons["browser.tabs.done"].tap()
        openLibrary(in: app)
        returnToWorkspaceList(in: app)
        openWorkspace(named: source, in: app)
        attachEvidence(app, "merge-source-before-library")
        returnToWorkspaceList(in: app)

        // Merge A into B as a folder.
        let sourceRow = workspaceRow(named: source, in: app)
        XCTAssertTrue(sourceRow.waitForExistence(timeout: timeout))
        sourceRow.press(forDuration: 1.1)
        let mergeMenu = app.buttons.matching(NSPredicate(
            format: "label IN %@", ["Merge into…", "Zusammenführen mit …",
                                    "Zusammenführen mit…"]
        )).firstMatch
        XCTAssertTrue(
            mergeMenu.waitForExistence(timeout: timeout),
            "The Workspace context menu must offer the merge action."
        )
        mergeMenu.tap()
        // Context-menu items carry no identifiers; the target's name is
        // unique through the run token.
        let targetChoice = app.buttons.matching(NSPredicate(
            format: "label == %@", target
        )).firstMatch
        XCTAssertTrue(targetChoice.waitForExistence(timeout: timeout))
        attachEvidence(app, "merge-target-menu")
        targetChoice.tap()
        let asFolder = app.buttons.matching(
            identifier: "browser.library.workspace.merge.folder"
        ).firstMatch
        XCTAssertTrue(
            asFolder.waitForExistence(timeout: timeout),
            "A non-empty Workspace must ask before merging."
        )
        attachEvidence(app, "merge-confirmation")
        asFolder.tap()

        let undo = app.buttons.matching(
            identifier: "browser.library.workspace.merge.undo"
        ).firstMatch
        XCTAssertTrue(
            undo.waitForExistence(timeout: timeout),
            "A merge must offer its one-level undo."
        )
        XCTAssertTrue(
            workspaceRow(named: source, in: app)
                .waitForNonExistence(timeout: timeout),
            "The source Workspace must be gone after the merge."
        )
        attachEvidence(app, "merge-done-undo-offered")

        // The target now holds one folder named after the source.
        openWorkspace(named: target, in: app)
        let mergedFolder = app.staticTexts[source]
        let folderShown = mergedFolder.waitForExistence(timeout: timeout)
        if !folderShown { attachTree(app, "merge-target-tree") }
        XCTAssertTrue(
            folderShown,
            "The target must hold a folder named after the source."
        )
        attachEvidence(app, "merge-target-with-source-folder")
        XCTAssertTrue(waitForHittable(mergedFolder, timeout: timeout))
        mergedFolder.tap()
        XCTAssertTrue(
            app.staticTexts[page].waitForExistence(timeout: timeout),
            "The source's saved page must arrive inside the new folder."
        )
        attachEvidence(app, "merge-folder-contents")

        // One-level undo: A returns with its nodes, B loses the folder.
        XCTAssertTrue(
            undo.exists,
            "The undo must stay offered while the person looks at B."
        )
        undo.tap()
        XCTAssertTrue(undo.waitForNonExistence(timeout: timeout))
        returnToWorkspaceList(in: app)
        XCTAssertTrue(
            workspaceRow(named: source, in: app)
                .waitForExistence(timeout: timeout),
            "Undo must restore the source Workspace with its name."
        )
        attachEvidence(app, "merge-undone-source-back")
        openWorkspace(named: target, in: app)
        // The Workspace list stays in the tree behind the pushed detail,
        // with a row named like the folder; only a visible one counts.
        XCTAssertTrue(
            waitForNoHittableText(source, in: app),
            "Undo must remove the merged folder from the target."
        )
        attachEvidence(app, "merge-undone-target-empty")
        returnToWorkspaceList(in: app)
        openWorkspace(named: source, in: app)
        XCTAssertTrue(
            app.staticTexts[page].waitForExistence(timeout: timeout),
            "Undo must bring A's saved page back into A."
        )
        attachEvidence(app, "merge-undone-source-nodes")
        returnToWorkspaceList(in: app)
        if tabWasInSource {
            closeLibrary(in: app)
            XCTAssertTrue(
                deckWorkspace.waitForExistence(timeout: timeout)
                    && deckWorkspace.label == source,
                "Undo must move the open tab back to A; deck shows "
                    + "'\(deckWorkspace.label)'."
            )
            attachEvidence(app, "merge-undone-tab-back-in-source")
        }
    }

    @MainActor
    private func waitForNoHittableText(
        _ label: String,
        in app: XCUIApplication
    ) -> Bool {
        let texts = app.staticTexts.matching(NSPredicate(
            format: "label == %@", label
        ))
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
            let visible = texts.allElementsBoundByIndex.contains {
                $0.exists && $0.isHittable
            }
            if !visible { return true }
        } while Date() < deadline
        return false
    }

    /// The switcher's section header for a Workspace's saved tabs.
    @MainActor
    private func savedSection(
        of workspace: String,
        in app: XCUIApplication
    ) -> XCUIElement {
        app.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["\(workspace) · Saved", "\(workspace) · Gespeichert"]
        )).firstMatch
    }

    @MainActor
    private func workspaceRow(
        named name: String,
        in app: XCUIApplication
    ) -> XCUIElement {
        app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS %@",
            "browser.library.workspace.", name
        )).firstMatch
    }

    @MainActor
    private func openLibrary(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(waitForHittable(more, timeout: timeout))
        more.tap()
        let workspaces = app.buttons["browser.actions.workspaces"]
        XCTAssertTrue(waitForHittable(workspaces, timeout: timeout))
        workspaces.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForExistence(timeout: timeout)
        )
    }

    /// On iPhone the selected Workspace's detail is pushed; go back to
    /// the list of Workspaces if it is.
    @MainActor
    private func returnToWorkspaceList(in app: XCUIApplication) {
        let manage = app.buttons["browser.library.manage"]
        for _ in 0..<3 {
            if manage.exists, manage.isHittable { return }
            let back = app.navigationBars.buttons.element(boundBy: 0)
            guard back.waitForExistence(timeout: 2) else { break }
            back.tap()
            _ = manage.waitForExistence(timeout: 2)
        }
        XCTAssertTrue(waitForHittable(manage, timeout: timeout))
    }

    @MainActor
    private func openWorkspace(named name: String, in app: XCUIApplication) {
        let row = workspaceRow(named: name, in: app)
        XCTAssertTrue(waitForHittable(row, timeout: timeout))
        row.tap()
    }

    @MainActor
    private func createWorkspace(named name: String, in app: XCUIApplication) {
        openManageMenu(in: app)
        let create = app.buttons["browser.library.create.workspace"]
        XCTAssertTrue(waitForHittable(create, timeout: timeout))
        create.tap()
        confirmCreation(name: name, in: app)
        XCTAssertTrue(app.staticTexts[name].waitForExistence(
            timeout: timeout
        ))
    }

    @MainActor
    private func closeLibrary(in app: XCUIApplication) {
        let done = app.buttons["browser.library.done"]
        XCTAssertTrue(waitForHittable(done, timeout: timeout))
        for _ in 0..<3 where done.exists {
            if done.isHittable { done.tap() }
            if done.waitForNonExistence(timeout: 4) { break }
        }
        assertLibraryClosed(in: app, timeout: timeout)
    }

    @MainActor
    private func saveSelectedPage(
        to workspace: String,
        in app: XCUIApplication
    ) {
        let save = app.buttons["browser.actions.save-to-workspace"]
        let destination = app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label == %@",
            "browser.actions.save-to-workspace.", workspace
        )).firstMatch
        var opened = false
        for _ in 0..<3 where !opened {
            XCTAssertTrue(openActions(revealing: save, in: app))
            XCTAssertTrue(waitForStableFrame(of: save))
            save.tap()
            opened = destination.waitForExistence(timeout: 5)
        }
        XCTAssertTrue(opened, "Save to Workspace must offer '\(workspace)'.")
        // UIMenu rows report `hittable == false` although they are on
        // screen; XCUI's own element tap still activates them.
        XCTAssertTrue(waitForStableFrame(of: destination))
        destination.tap()
        XCTAssertTrue(destination.waitForNonExistence(timeout: timeout))
        closeActions(in: app)
    }

    /// Opens the actions sheet and drags its list until `element` is
    /// hittable (see MobileSavedPageHomeUITests).
    @MainActor
    private func openActions(
        revealing element: XCUIElement,
        in app: XCUIApplication
    ) -> Bool {
        for _ in 0..<4 {
            closeActions(in: app)
            let more = app.buttons["browser.more"]
            XCTAssertTrue(waitForHittable(more, timeout: timeout))
            more.tap()
            let sheetReady = app.buttons["browser.actions.done"]
            XCTAssertTrue(sheetReady.waitForExistence(timeout: timeout))
            if element.waitForExistence(timeout: 2), element.isHittable {
                return true
            }
            let list = app.collectionViews["browser.actions.list"]
            guard list.waitForExistence(timeout: timeout) else { continue }
            let frame = list.frame
            let origin = app.coordinate(withNormalizedOffset: .zero)
            for _ in 0..<6 {
                let x = frame.maxX - 8
                let start = origin.withOffset(CGVector(
                    dx: x, dy: frame.minY + frame.height * 0.8
                ))
                let end = origin.withOffset(CGVector(
                    dx: x, dy: frame.minY + frame.height * 0.3
                ))
                start.press(forDuration: 0.05, thenDragTo: end)
                if element.waitForExistence(timeout: 1), element.isHittable {
                    return true
                }
            }
        }
        return false
    }

    @MainActor
    private func closeActions(in app: XCUIApplication) {
        let done = app.buttons["browser.actions.done"]
        guard done.waitForExistence(timeout: 1) else { return }
        if waitForHittable(done, timeout: timeout) { done.tap() }
        XCTAssertTrue(done.waitForNonExistence(timeout: timeout))
    }

    @MainActor
    private func waitForStableFrame(of element: XCUIElement) -> Bool {
        var previous = element.frame
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
            let current = element.frame
            if current == previous, !current.isEmpty { return true }
            previous = current
        }
        return false
    }

    /// The manage menu lives in the Workspace list's toolbar; a pushed
    /// detail may need to go back first.
    @MainActor
    private func openManageMenu(in app: XCUIApplication) {
        let manage = app.buttons["browser.library.manage"]
        if !(manage.exists && manage.isHittable) {
            returnToWorkspaceList(in: app)
        }
        XCTAssertTrue(waitForHittable(manage, timeout: timeout))
        manage.tap()
    }

    @MainActor
    private func confirmCreation(name: String, in app: XCUIApplication) {
        let field = app.textFields["browser.library.create.name"]
        XCTAssertTrue(waitForHittable(field, timeout: timeout))
        field.tap()
        field.typeText(name)
        let confirm = app.buttons["browser.library.create.confirm"]
        XCTAssertTrue(waitForHittable(confirm, timeout: timeout))
        confirm.tap()
        XCTAssertTrue(confirm.waitForNonExistence(timeout: timeout))
    }

    /// Deletes this journey's Workspaces, including ones an interrupted
    /// earlier run left behind, so menus stay short and deterministic.
    @MainActor
    private func deleteMergeWorkspaces(in app: XCUIApplication) {
        guard app.state == .runningForeground else { return }
        closeActions(in: app)
        let root = app.descendants(matching: .any)["browser.library.root"]
        if !root.exists {
            let more = app.buttons["browser.more"]
            guard more.waitForExistence(timeout: 3) else { return }
            openLibrary(in: app)
        }
        let back = app.navigationBars.buttons.element(boundBy: 0)
        let manage = app.buttons["browser.library.manage"]
        if !(manage.exists && manage.isHittable), back.exists { back.tap() }
        let stale = app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND "
                + "(label BEGINSWITH %@ OR label BEGINSWITH %@)",
            "browser.library.workspace.", "Merge A ", "Merge B "
        ))
        for _ in 0..<12 {
            let row = stale.firstMatch
            guard row.waitForExistence(timeout: 2) else { break }
            let label = row.label
            row.press(forDuration: 1.1)
            let delete = app.buttons.matching(NSPredicate(
                format: "identifier BEGINSWITH %@",
                "browser.library.workspace.delete."
            )).firstMatch
            guard delete.waitForExistence(timeout: 3) else { break }
            delete.tap()
            let confirm = app.buttons.matching(
                identifier: "browser.library.workspace.delete.confirm"
            ).firstMatch
            guard confirm.waitForExistence(timeout: 3) else { break }
            confirm.tap()
            _ = workspaceRow(named: label, in: app)
                .waitForNonExistence(timeout: 5)
        }
    }
}
