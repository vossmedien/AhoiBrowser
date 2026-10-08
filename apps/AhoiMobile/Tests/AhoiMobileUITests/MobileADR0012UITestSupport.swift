import XCTest

/// Shared steps for the ADR 0012 journeys (recent-tab flick, Workspace
/// merge, Web Extension spike). They run against the DEBUG candidate with
/// the deterministic `-AhoiUITestFixture` population and public
/// `https://example.com/?…` pages, not the local HTTPS fixture server.
extension MobileBrowserUITestCase {
    static let adr0012StateTimeout: TimeInterval = 10
    static let adr0012NetworkTimeout: TimeInterval = 20

    /// A VoiceOver journey that failed half-way must not leave VoiceOver
    /// on: every tap would then only move its cursor.
    @MainActor
    func ensureVoiceOverOff() {
        guard #available(iOS 27.0, *) else { return }
        let voiceOver = XCUIDevice.shared.voiceOverService
        for _ in 0..<3 where voiceOver.isEnabled {
            try? voiceOver.disable()
            RunLoop.current.run(until: Date().addingTimeInterval(1))
        }
        XCTAssertFalse(voiceOver.isEnabled, "VoiceOver must be off.")
    }

    @MainActor
    func addressControl(in app: XCUIApplication) -> XCUIElement {
        app.buttons.matching(NSPredicate(
            format: "identifier IN %@", ["browser.address", "browser.address.private"]
        )).firstMatch
    }

    /// The address control's value is the selected tab's full URL.
    @MainActor
    func waitForAddressValue(
        containing text: String,
        in app: XCUIApplication,
        timeout: TimeInterval = adr0012StateTimeout
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value CONTAINS %@", text),
            object: addressControl(in: app)
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout)
            == .completed
    }

    @MainActor
    func currentAddress(in app: XCUIApplication) -> String {
        addressControl(in: app).value as? String ?? ""
    }

    @MainActor
    func visibleTabCount(in app: XCUIApplication) -> Int? {
        let tabs = app.buttons["browser.tabs"]
        if let value = tabs.value as? String,
           let count = Int(value.trimmingCharacters(in: .whitespaces)) {
            return count
        }
        return Int(tabs.label.prefix { $0.isNumber })
    }

    /// One-finger horizontal drag across the Harbor Deck's address
    /// control. XCUI calls the control unhittable (the web view's frame
    /// reaches under the deck), so the drag uses its visible frame.
    @MainActor
    func flickAddress(
        _ direction: Int,
        in app: XCUIApplication,
        distance: CGFloat = 170,
        hold: TimeInterval = 0
    ) {
        let address = addressControl(in: app)
        XCTAssertTrue(address.waitForExistence(timeout: 5))
        let frame = address.frame
        let origin = app.coordinate(
            withNormalizedOffset: CGVector(dx: 0, dy: 0)
        )
        let startX = direction > 0
            ? frame.minX + 24
            : frame.maxX - 24
        let endX = direction > 0
            ? min(frame.maxX, startX + distance)
            : max(frame.minX, startX - distance)
        let start = origin.withOffset(CGVector(dx: startX, dy: frame.midY))
        let end = origin.withOffset(CGVector(dx: endX, dy: frame.midY))
        start.press(
            forDuration: 0.05,
            thenDragTo: end,
            withVelocity: .default,
            thenHoldForDuration: hold
        )
    }

    /// Opens `url` through the address editor and waits for the public
    /// example.com page. `isHittable` is a false signal for the address
    /// over a loaded page (79828c0); the editor field proves the tap.
    @MainActor
    func openExamplePage(_ url: URL, in app: XCUIApplication) {
        let address = addressControl(in: app)
        XCTAssertTrue(
            address.waitForExistence(timeout: Self.adr0012StateTimeout)
        )
        // A blank tab exposes a localized address prompt, not an empty value.
        let startsBlank = URL(string: address.value as? String ?? "")?.host == nil
        let needsKeystrokes = address.identifier == "browser.address.private" || startsBlank
        address.tap()
        let field = app.textFields["browser.address.field"]
        if !field.waitForExistence(timeout: 2) {
            // A tap immediately after switcher dismissal can hit its outgoing
            // presentation. Retap the current real control once, then prove it.
            addressControl(in: app).tap()
        }
        XCTAssertTrue(waitForHittable(field, timeout: Self.adr0012StateTimeout))
        clearAddressEditor(field, in: app)
        if needsKeystrokes {
            // SDK27 XCUI dropped even the two-character chunk "ht" on the
            // private/new blank sheet. Verify each native keystroke prefix.
            field.tap()
            var prefix = ""
            for character in url.absoluteString {
                field.typeText(String(character))
                prefix.append(character)
                let entered = XCTNSPredicateExpectation(
                    predicate: NSPredicate(format: "value == %@", prefix), object: field)
                XCTAssertEqual(XCTWaiter.wait(for: [entered], timeout: 2), .completed)
            }
            XCTAssertEqual(field.value as? String, url.absoluteString)
        } else {
            enterExactAddress(url.absoluteString, into: field, in: app)
        }
        let go = app.buttons["browser.search.navigate"]
        XCTAssertTrue(waitForHittable(go, timeout: Self.adr0012StateTimeout))
        go.tap()
        XCTAssertTrue(
            field.waitForNonExistence(timeout: Self.adr0012StateTimeout)
        )
        let displayedAddress = addressControl(in: app)
        XCTAssertTrue(displayedAddress.waitForExistence(timeout: 5))
        if displayedAddress.identifier == "browser.address.private" {
            // Private chrome exposes only the origin. The editor above proved
            // the full input; the caller's PageProbe proves the loaded query.
            XCTAssertTrue(waitForAddressValue(containing: url.host ?? "example.com", in: app))
            XCTAssertFalse(currentAddress(in: app).contains(url.absoluteString))
        } else {
            let exactAddress = XCTNSPredicateExpectation(
                predicate: NSPredicate(format: "value == %@", url.absoluteString), object: displayedAddress)
            XCTAssertEqual(XCTWaiter.wait(for: [exactAddress], timeout: 8), .completed,
                           "Expected \(url.absoluteString), got \(currentAddress(in: app)).")
        }
        XCTAssertTrue(
            exampleDomainPage(in: app).waitForExistence(
                timeout: Self.adr0012NetworkTimeout
            ),
            "The public example.com page must load."
        )
    }

    @MainActor
    func showTabSwitcher(_ app: XCUIApplication) {
        let tabs = app.buttons["browser.tabs"]
        XCTAssertTrue(waitForHittable(tabs, timeout: 5))
        tabs.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.list"]
                .waitForExistence(timeout: Self.adr0012StateTimeout)
        )
    }

    @MainActor
    func switcherRow(
        containing text: String,
        in app: XCUIApplication
    ) -> XCUIElement {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@",
            "browser.tab-row.",
            text
        )).firstMatch
    }

    /// Selects a tab through the switcher, which refreshes its
    /// most-recent-use time exactly like a person picking it.
    @MainActor
    func selectTabInSwitcher(
        rowContaining text: String,
        expectAddress address: String,
        in app: XCUIApplication
    ) {
        showTabSwitcher(app)
        let row = switcherRow(containing: text, in: app)
        XCTAssertTrue(
            waitForHittable(row, timeout: Self.adr0012StateTimeout),
            "The switcher must list a tab containing '\(text)'."
        )
        row.tap()
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.tabs.list"]
                .waitForNonExistence(timeout: Self.adr0012StateTimeout)
        )
        XCTAssertTrue(
            waitForAddressValue(containing: address, in: app),
            "Selecting '\(text)' must show \(address); "
                + "got \(currentAddress(in: app))."
        )
    }

    @MainActor
    func attachEvidence(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    @MainActor
    func attachTree(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(string: app.debugDescription)
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
