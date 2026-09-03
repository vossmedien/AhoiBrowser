import XCTest

final class MobileBrowserIPadInteractionUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testIPadHardwareCommandsDriveAddressTabLifecycleAndNumberedSelection() async throws {
        let fixture = try await requireReachableFixture()
        let device = XCUIDevice.shared
        let originalOrientation = device.orientation
        device.orientation = .portrait
        defer { restore(originalOrientation, on: device) }

        let app = coldLaunchApplication()
        try requireIPad(app)

        let tabURLs = (1...9).map { number in
            fixture.url(path: "/navigation?keyboard-tab=\(number)")
        }
        navigate(to: tabURLs[0], in: app)

        app.typeKey("l", modifierFlags: .command)
        let addressField = app.textFields["browser.address.field"]
        XCTAssertTrue(
            addressField.waitForExistence(timeout: 3),
            "The selected iPad candidate must deliver Command-L through XCUI."
        )
        XCTAssertTrue(waitUntil(timeout: 3) { addressField.hasFocus })
        app.typeKey(XCUIKeyboardKey.escape.rawValue, modifierFlags: [])
        XCTAssertTrue(addressField.waitForNonExistence(timeout: 3))

        for index in 1..<tabURLs.count {
            app.typeKey("t", modifierFlags: .command)
            XCTAssertTrue(waitForTabCount(index + 1, in: app, timeout: 4))
            navigate(to: tabURLs[index], in: app)
        }

        for number in 1...9 {
            app.typeKey(String(number), modifierFlags: .command)
            assertAddress(
                tabURLs[number - 1],
                containsOrigin: fixture.origin,
                in: app
            )
        }

        app.typeKey("1", modifierFlags: .command)
        assertAddress(tabURLs[0], containsOrigin: fixture.origin, in: app)
        app.typeKey(XCUIKeyboardKey.tab.rawValue, modifierFlags: .control)
        assertAddress(tabURLs[1], containsOrigin: fixture.origin, in: app)
        app.typeKey(
            XCUIKeyboardKey.tab.rawValue,
            modifierFlags: [.control, .shift]
        )
        assertAddress(tabURLs[0], containsOrigin: fixture.origin, in: app)

        app.typeKey("w", modifierFlags: .command)
        XCTAssertTrue(waitForTabCount(8, in: app, timeout: 4))
        XCTAssertNotEqual(addressValue(in: app), tabURLs[0].absoluteString)

        app.typeKey("t", modifierFlags: [.command, .shift])
        XCTAssertTrue(waitForTabCount(9, in: app, timeout: 5))
        assertAddress(tabURLs[0], containsOrigin: fixture.origin, in: app)

        app.typeKey("n", modifierFlags: [.command, .shift])
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 4))
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 4)
        )
        app.typeKey("w", modifierFlags: .command)
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 4))
        XCTAssertTrue(waitForTabCount(9, in: app, timeout: 4))
        assertAddress(tabURLs[0], containsOrigin: fixture.origin, in: app)

        app.typeKey("\\", modifierFlags: [.command, .shift])
        let tabMode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(
            tabMode.waitForExistence(timeout: 4),
            "Command-Shift-Backslash must present the native tab overview."
        )
        let done = app.buttons["browser.tabs.done"]
        XCTAssertTrue(done.waitForExistence(timeout: 3))
        done.tap()
        XCTAssertTrue(tabMode.waitForNonExistence(timeout: 3))
    }

    @MainActor
    func testIPadHardwareCommandsToggleSidebarAndKeepAddressFocusAfterRotation() throws {
        let device = XCUIDevice.shared
        let originalOrientation = device.orientation
        device.orientation = .portrait
        defer { restore(originalOrientation, on: device) }

        let app = coldLaunchApplication()
        try requireIPad(app)
        let sidebarCommand = app.buttons["browser.sidebar.command"]
        XCTAssertTrue(sidebarCommand.waitForExistence(timeout: 8))

        app.typeKey("s", modifierFlags: [.command, .control])
        XCTAssertTrue(
            sidebarCommand.waitForNonExistence(timeout: 4),
            "Command-Control-S must hide the regular-width Workspace Canvas sidebar."
        )
        app.typeKey("s", modifierFlags: [.command, .control])
        XCTAssertTrue(
            sidebarCommand.waitForExistence(timeout: 4),
            "Repeating Command-Control-S must restore the sidebar."
        )

        device.orientation = .landscapeLeft
        XCTAssertTrue(waitUntil(timeout: 5) { app.frame.width > app.frame.height })
        XCTAssertTrue(sidebarCommand.waitForExistence(timeout: 4))
        app.typeKey("l", modifierFlags: .command)
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(
            field.waitForExistence(timeout: 3),
            "The selected iPad candidate must retain Command-L after rotation."
        )
        XCTAssertTrue(waitUntil(timeout: 3) { field.hasFocus })
        app.typeKey(XCUIKeyboardKey.escape.rawValue, modifierFlags: [])
        XCTAssertTrue(field.waitForNonExistence(timeout: 3))
        XCTAssertTrue(app.buttons["browser.address"].isHittable)
    }

    @MainActor
    func testIPadWorkspaceHardwareCommandsTraverseVisibleProductWorkspaces() throws {
        let app = coldLaunchApplication()
        try requireIPad(app)
        let token = UUID().uuidString.lowercased().prefix(8)
        let firstName = "Keyboard Harbor \(token)-A"
        let secondName = "Keyboard Harbor \(token)-B"
        defer {
            deleteWorkspaces(named: [firstName, secondName], in: app)
            app.terminate()
        }

        openLibrary(in: app)
        createWorkspace(named: firstName, in: app)
        createWorkspace(named: secondName, in: app)
        app.buttons["browser.library.done"].tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForNonExistence(timeout: 4)
        )

        let firstSidebarWorkspace = app.staticTexts[firstName]
        XCTAssertTrue(firstSidebarWorkspace.waitForExistence(timeout: 5))
        firstSidebarWorkspace.tap()
        XCTAssertTrue(waitForWorkspaceLabel(firstName, in: app, timeout: 5))

        let next = app.buttons["browser.workspace.next"]
        let previous = app.buttons["browser.workspace.previous"]
        XCTAssertTrue(
            next.waitForExistence(timeout: 4) && previous.exists,
            "The selected iPad candidate must expose both workspace switching controls."
        )

        app.typeKey("]", modifierFlags: [.command, .control])
        XCTAssertTrue(
            waitForWorkspaceLabel(secondName, in: app, timeout: 5),
            "Command-Control-Right-Bracket must select the next visible workspace."
        )
        app.typeKey("[", modifierFlags: [.command, .control])
        XCTAssertTrue(
            waitForWorkspaceLabel(firstName, in: app, timeout: 5),
            "Command-Control-Left-Bracket must select the previous visible workspace."
        )
    }

    @MainActor
    private func requireIPad(_ app: XCUIApplication) throws {
        let shortestEdge = min(app.frame.width, app.frame.height)
        guard shortestEdge >= 700 else {
            throw XCTSkip("This visible hardware-key journey requires a full-screen iPad.")
        }
    }

    @MainActor
    private func openLibrary(in app: XCUIApplication) {
        if app.descendants(matching: .any)["browser.library.root"].exists { return }
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let workspaces = app.buttons["browser.actions.workspaces"]
        XCTAssertTrue(workspaces.waitForExistence(timeout: 3))
        workspaces.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.library.root"]
                .waitForExistence(timeout: 5)
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
        // SwiftUI alerts can expose the same semantic action through both the
        // alert host and its rendered button on some iOS runtimes. Scope the
        // query to the active alert and operate on that single visible action.
        let confirm = app.alerts.firstMatch
            .buttons["browser.library.create.confirm"]
            .firstMatch
        XCTAssertTrue(confirm.waitForExistence(timeout: 3))
        confirm.tap()
        XCTAssertTrue(app.staticTexts[name].waitForExistence(timeout: 5))
    }

    @MainActor
    private func deleteWorkspaces(named names: [String], in app: XCUIApplication) {
        if app.state != .runningForeground { app.activate() }
        openLibrary(in: app)
        for name in names {
            let workspace = app.staticTexts[name]
            guard workspace.waitForExistence(timeout: 3) else { continue }
            workspace.press(forDuration: 1.1)
            let delete = app.buttons.matching(NSPredicate(
                format: "identifier BEGINSWITH %@",
                "browser.library.workspace.delete."
            )).firstMatch
            guard delete.waitForExistence(timeout: 3) else { continue }
            delete.tap()
            let confirm = app.sheets.firstMatch
                .buttons["browser.library.workspace.delete.confirm"]
                .firstMatch
            guard confirm.waitForExistence(timeout: 3) else { continue }
            confirm.tap()
            XCTAssertTrue(
                workspace.waitForNonExistence(timeout: 5),
                "The workspace E2E journey must remove its own \(name) fixture."
            )
        }
        let done = app.buttons["browser.library.done"]
        if done.exists { done.tap() }
    }

    @MainActor
    private func waitForWorkspaceLabel(
        _ expected: String,
        in app: XCUIApplication,
        timeout: TimeInterval
    ) -> Bool {
        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        return waitUntil(timeout: timeout) {
            workspace.exists && workspace.label.contains(expected)
        }
    }

    @MainActor
    private func waitForTabCount(
        _ expected: Int,
        in app: XCUIApplication,
        timeout: TimeInterval
    ) -> Bool {
        let tabs = app.buttons["browser.tabs"]
        return waitUntil(timeout: timeout) { tabCount(in: tabs) == expected }
    }

    @MainActor
    private func tabCount(in element: XCUIElement) -> Int? {
        if let value = element.value as? String,
           let count = Int(value.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return count
        }
        if let value = element.value as? NSNumber { return value.intValue }
        let digits = element.label.compactMap(\.wholeNumberValue)
        guard !digits.isEmpty else { return nil }
        return digits.reduce(0) { $0 * 10 + $1 }
    }

    @MainActor
    private func addressValue(in app: XCUIApplication) -> String {
        if app.buttons["browser.address"].exists {
            return app.buttons["browser.address"].value as? String ?? ""
        }
        return app.buttons["browser.address.private"].value as? String ?? ""
    }

    @MainActor
    private func waitUntil(
        timeout: TimeInterval,
        condition: () -> Bool
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if condition() { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return condition()
    }

    private func restore(_ orientation: UIDeviceOrientation, on device: XCUIDevice) {
        switch orientation {
        case .portrait, .portraitUpsideDown, .landscapeLeft, .landscapeRight:
            device.orientation = orientation
        default:
            device.orientation = .portrait
        }
    }
}
