import XCTest

final class MobileBrowserAccessibilityAppearanceUITests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    @MainActor
    func testMaximumSystemTextSizeKeepsNormalAndPrivateHarborDeckReachableAcrossRotation() throws {
        try requireSimulator(
            reason: "The system text-size journey changes Settings and is simulator-only."
        )
        let device = XCUIDevice.shared
        let originalOrientation = device.orientation
        device.orientation = .portrait

        let textSize = try SystemTextSizeController.openMaximumSizeControl()
        defer { textSize.restore() }
        try textSize.selectMaximumSize()

        let app = XCUIApplication()
        app.launchArguments = []
        defer {
            app.terminate()
            restore(originalOrientation, on: device)
        }
        app.launch()

        assertNormalSemantics(in: app)
        assertExpandedHarborGeometry(in: app)
        attachScreenshot(named: "Maximum text size - normal portrait", of: app)

        device.orientation = .landscapeLeft
        XCTAssertTrue(waitForLandscape(app, timeout: 5))
        assertNormalSemantics(in: app)
        assertExpandedHarborGeometry(in: app)
        attachScreenshot(named: "Maximum text size - normal landscape", of: app)

        device.orientation = .portrait
        XCTAssertTrue(waitForPortrait(app, timeout: 5))
        createPrivateTab(in: app)
        assertPrivateSemantics(in: app)
        assertExpandedHarborGeometry(in: app, privateBrowsing: true)
        attachScreenshot(named: "Maximum text size - private portrait", of: app)

        device.orientation = .landscapeRight
        XCTAssertTrue(waitForLandscape(app, timeout: 5))
        assertPrivateSemantics(in: app)
        assertExpandedHarborGeometry(in: app, privateBrowsing: true)
        attachScreenshot(named: "Maximum text size - private landscape", of: app)
    }

    @MainActor
    func testSystemLightAndDarkAppearancesRenderNormalAndPrivateSemanticSurfaces() throws {
        try requireSimulator(
            reason: "The appearance journey changes Settings and is simulator-only."
        )
        let appearance = try SystemAppearanceController.openAppearanceControl()
        defer { appearance.restore() }

        for style in SystemAppearanceStyle.allCases {
            try appearance.select(style)

            let app = XCUIApplication()
            app.launchArguments = []
            app.launch()
            assertNormalSemantics(in: app)
            assertExpandedHarborGeometry(in: app)
            attachScreenshot(named: "System \(style.rawValue) - normal", of: app)

            createPrivateTab(in: app)
            assertPrivateSemantics(in: app)
            assertExpandedHarborGeometry(in: app, privateBrowsing: true)
            attachScreenshot(named: "System \(style.rawValue) - private", of: app)
            app.terminate()
        }
    }

    @MainActor
    func testHardwareEscapeDismissesAddressAndTabPresentationsWhenXCUIDeliversIt() throws {
        let app = XCUIApplication()
        app.launchArguments = []
        app.launch()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))

        app.typeKey("l", modifierFlags: .command)
        let addressField = app.textFields["browser.address.field"]
        XCTAssertTrue(
            addressField.waitForExistence(timeout: 3),
            "The selected candidate must deliver Command-L through XCUI."
        )
        app.typeKey(XCUIKeyboardKey.escape.rawValue, modifierFlags: [])
        XCTAssertTrue(
            addressField.waitForNonExistence(timeout: 3),
            "Escape must dismiss the address presentation on the selected candidate."
        )

        app.buttons["browser.tabs"].tap()
        let mode = app.descendants(matching: .any)["browser.tabs.mode"]
        XCTAssertTrue(mode.waitForExistence(timeout: 3))
        app.typeKey(XCUIKeyboardKey.escape.rawValue, modifierFlags: [])
        XCTAssertTrue(
            mode.waitForNonExistence(timeout: 3),
            "Escape must dismiss the native tab presentation on the selected candidate."
        )
    }

    @MainActor
    private func assertNormalSemantics(
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.header"]
                .waitForExistence(timeout: 8),
            file: file,
            line: line
        )
        XCTAssertEqual(app.buttons.matching(identifier: "browser.address").count, 1)
        XCTAssertEqual(app.buttons.matching(identifier: "browser.address.private").count, 0)
        XCTAssertEqual(
            app.descendants(matching: .any)
                .matching(identifier: "browser.harbor-deck.workspace").count,
            1,
            file: file,
            line: line
        )
    }

    @MainActor
    private func assertPrivateSemantics(
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private"]
                .waitForExistence(timeout: 5),
            file: file,
            line: line
        )
        XCTAssertTrue(
            app.descendants(matching: .any)["browser.focus-voyage.private-explanation"].exists,
            file: file,
            line: line
        )
        XCTAssertEqual(app.buttons.matching(identifier: "browser.address.private").count, 1)
        XCTAssertEqual(app.buttons.matching(identifier: "browser.address").count, 0)
        XCTAssertEqual(
            app.descendants(matching: .any)
                .matching(identifier: "browser.harbor-deck.workspace").count,
            1,
            file: file,
            line: line
        )
    }

    @MainActor
    private func assertExpandedHarborGeometry(
        in app: XCUIApplication,
        privateBrowsing: Bool = false,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let addressID = privateBrowsing ? "browser.address.private" : "browser.address"
        let identifiers = [
            "browser.back",
            "browser.forward",
            addressID,
            "browser.reload-stop",
            "browser.tabs",
            "browser.more",
        ]
        let controls = identifiers.map { app.buttons[$0] }
        for control in controls {
            XCTAssertTrue(control.waitForExistence(timeout: 5), file: file, line: line)
            XCTAssertGreaterThanOrEqual(control.frame.width, 44, file: file, line: line)
            XCTAssertGreaterThanOrEqual(control.frame.height, 44, file: file, line: line)
        }

        for control in controls.filter({ $0.identifier != "browser.back" &&
            $0.identifier != "browser.forward" &&
            $0.identifier != "browser.reload-stop"
        }) {
            XCTAssertTrue(control.isHittable, file: file, line: line)
        }
        for leftIndex in controls.indices {
            for rightIndex in controls.indices where rightIndex > leftIndex {
                XCTAssertTrue(
                    controls[leftIndex].frame.intersection(controls[rightIndex].frame).isNull,
                    "\(controls[leftIndex].identifier) overlaps \(controls[rightIndex].identifier).",
                    file: file,
                    line: line
                )
            }
        }

        let workspace = app.descendants(matching: .any)["browser.harbor-deck.workspace"]
        XCTAssertTrue(workspace.exists, file: file, line: line)
        XCTAssertGreaterThanOrEqual(workspace.frame.height, 44, file: file, line: line)
    }

    @MainActor
    private func createPrivateTab(in app: XCUIApplication) {
        let more = app.buttons["browser.more"]
        XCTAssertTrue(more.waitForExistence(timeout: 5))
        more.tap()
        let newPrivateTab = app.buttons["browser.new-private-tab"]
        XCTAssertTrue(newPrivateTab.waitForExistence(timeout: 3))
        newPrivateTab.tap()
        XCTAssertTrue(app.buttons["browser.address.private"].waitForExistence(timeout: 4))
    }

    @MainActor
    private func attachScreenshot(named name: String, of app: XCUIApplication) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func requireSimulator(reason: String) throws {
        guard ProcessInfo.processInfo.environment["SIMULATOR_UDID"] != nil else {
            throw XCTSkip(reason)
        }
    }

    @MainActor
    private func waitForLandscape(
        _ app: XCUIApplication,
        timeout: TimeInterval
    ) -> Bool {
        waitUntil(timeout: timeout) { app.frame.width > app.frame.height }
    }

    @MainActor
    private func waitForPortrait(
        _ app: XCUIApplication,
        timeout: TimeInterval
    ) -> Bool {
        waitUntil(timeout: timeout) { app.frame.height > app.frame.width }
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

@MainActor
private final class SystemTextSizeController {
    private let settings: XCUIApplication
    private let largerSizes: XCUIElement
    private let slider: XCUIElement
    private let originalLargerSizesEnabled: Bool
    private let originalSliderValue: String

    private init(
        settings: XCUIApplication,
        largerSizes: XCUIElement,
        slider: XCUIElement
    ) {
        self.settings = settings
        self.largerSizes = largerSizes
        self.slider = slider
        originalLargerSizesEnabled = Self.isOn(largerSizes)
        originalSliderValue = Self.value(of: slider)
    }

    static func openMaximumSizeControl() throws -> SystemTextSizeController {
        let settings = XCUIApplication(bundleIdentifier: "com.apple.Preferences")
        settings.launch()
        try openSettingsRoot(in: settings)
        try tapLocalizedRow(
            labels: ["Accessibility", "Bedienungshilfen"],
            in: settings
        )
        try tapLocalizedRow(
            labels: ["Display & Text Size", "Anzeige & Textgröße"],
            in: settings
        )
        try tapLocalizedRow(
            labels: ["Larger Text", "Größerer Text"],
            in: settings
        )

        let slider = settings.sliders.firstMatch
        guard slider.waitForExistence(timeout: 4) else {
            throw XCTSkip("Settings does not expose the Larger Text slider through XCUI.")
        }
        let largerSizes = settings.switches.firstMatch
        guard largerSizes.waitForExistence(timeout: 3) else {
            throw XCTSkip(
                "Settings does not expose Larger Accessibility Sizes through XCUI."
            )
        }
        return SystemTextSizeController(
            settings: settings,
            largerSizes: largerSizes,
            slider: slider
        )
    }

    func selectMaximumSize() throws {
        settings.activate()
        if !Self.isOn(largerSizes) {
            largerSizes.coordinate(
                withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)
            ).tap()
            guard waitUntil(timeout: 3, condition: { Self.isOn(self.largerSizes) }) else {
                throw XCTSkip("XCUI could not enable Larger Accessibility Sizes.")
            }
        }
        slider.adjust(toNormalizedSliderPosition: 0)
        let minimumValue = Self.value(of: slider)
        slider.adjust(toNormalizedSliderPosition: 1)
        let maximumValue = Self.value(of: slider)
        guard !minimumValue.isEmpty,
              !maximumValue.isEmpty,
              minimumValue != maximumValue else {
            throw XCTSkip(
                "XCUI cannot prove distinct minimum and maximum text-size endpoints."
            )
        }
    }

    func restore() {
        settings.activate()
        if Self.value(of: slider) != originalSliderValue {
            restoreSliderValue(originalSliderValue)
        }
        if Self.isOn(largerSizes) != originalLargerSizesEnabled {
            largerSizes.coordinate(
                withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)
            ).tap()
        }
        XCTAssertEqual(
            Self.isOn(largerSizes),
            originalLargerSizesEnabled,
            "The E2E journey must restore Larger Accessibility Sizes."
        )
    }

    private func restoreSliderValue(_ expectedValue: String) {
        for step in 0...40 {
            slider.adjust(toNormalizedSliderPosition: CGFloat(step) / 40)
            if Self.value(of: slider) == expectedValue { break }
        }
        XCTAssertEqual(
            Self.value(of: slider),
            expectedValue,
            "The E2E journey must restore the exact visible system text-size value."
        )
    }

    private static func isOn(_ element: XCUIElement) -> Bool {
        if let value = element.value as? String { return value == "1" }
        if let value = element.value as? NSNumber { return value.boolValue }
        return false
    }

    private static func value(of element: XCUIElement) -> String {
        if let value = element.value as? String { return value }
        if let value = element.value as? NSNumber { return value.stringValue }
        return ""
    }
}

private enum SystemAppearanceStyle: String, CaseIterable {
    case light = "Light"
    case dark = "Dark"
}

@MainActor
private final class SystemAppearanceController {
    private let settings: XCUIApplication
    private let light: XCUIElement
    private let dark: XCUIElement
    private let automatic: XCUIElement?
    private let originalStyle: SystemAppearanceStyle
    private let originalAutomaticEnabled: Bool?

    private init(
        settings: XCUIApplication,
        light: XCUIElement,
        dark: XCUIElement,
        automatic: XCUIElement?
    ) throws {
        self.settings = settings
        self.light = light
        self.dark = dark
        self.automatic = automatic
        guard light.isSelected != dark.isSelected else {
            throw XCTSkip(
                "Settings does not expose the selected Light/Dark appearance through XCUI."
            )
        }
        originalStyle = dark.isSelected ? .dark : .light
        originalAutomaticEnabled = automatic.map(Self.isOn)
    }

    static func openAppearanceControl() throws -> SystemAppearanceController {
        let settings = XCUIApplication(bundleIdentifier: "com.apple.Preferences")
        settings.launch()
        try openSettingsRoot(in: settings)
        try tapLocalizedRow(
            labels: ["Display & Brightness", "Anzeige & Helligkeit"],
            in: settings
        )

        let light = selectedAppearanceElement(
            labels: ["Light", "Hell"],
            in: settings
        )
        let dark = selectedAppearanceElement(
            labels: ["Dark", "Dunkel"],
            in: settings
        )
        guard light.waitForExistence(timeout: 4), dark.waitForExistence(timeout: 4) else {
            throw XCTSkip("Settings does not expose Light and Dark appearance controls.")
        }
        let automaticQuery = settings.switches.matching(NSPredicate(
            format: "label IN %@",
            ["Automatic", "Automatisch"]
        )).firstMatch
        return try SystemAppearanceController(
            settings: settings,
            light: light,
            dark: dark,
            automatic: automaticQuery.exists ? automaticQuery : nil
        )
    }

    func select(_ style: SystemAppearanceStyle) throws {
        settings.activate()
        let target = style == .dark ? dark : light
        target.tap()
        guard waitUntil(timeout: 4, condition: { target.isSelected }) else {
            throw XCTSkip(
                "XCUI could not select the system \(style.rawValue) appearance."
            )
        }
    }

    func restore() {
        settings.activate()
        let original = originalStyle == .dark ? dark : light
        if !original.isSelected { original.tap() }
        XCTAssertTrue(
            waitUntil(timeout: 4, condition: { original.isSelected }),
            "The E2E journey must restore the original system appearance."
        )
        if let automatic, let originalAutomaticEnabled,
           Self.isOn(automatic) != originalAutomaticEnabled {
            automatic.coordinate(
                withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)
            ).tap()
        }
        if let automatic, let originalAutomaticEnabled {
            XCTAssertEqual(
                Self.isOn(automatic),
                originalAutomaticEnabled,
                "The E2E journey must restore Automatic appearance."
            )
        }
    }

    private static func isOn(_ element: XCUIElement) -> Bool {
        if let value = element.value as? String { return value == "1" }
        if let value = element.value as? NSNumber { return value.boolValue }
        return false
    }
}

@MainActor
private func openSettingsRoot(in settings: XCUIApplication) throws {
    for _ in 0..<7 {
        let accessibility = settings.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["Accessibility", "Bedienungshilfen"]
        )).firstMatch
        let display = settings.staticTexts.matching(NSPredicate(
            format: "label IN %@",
            ["Display & Brightness", "Anzeige & Helligkeit"]
        )).firstMatch
        if accessibility.exists || display.exists { return }
        let back = settings.navigationBars.buttons.firstMatch
        guard back.exists else { break }
        back.tap()
    }
    let rootRow = settings.staticTexts.matching(NSPredicate(
        format: "label IN %@",
        [
            "Accessibility", "Bedienungshilfen",
            "Display & Brightness", "Anzeige & Helligkeit",
        ]
    )).firstMatch
    guard rootRow.waitForExistence(timeout: 4) else {
        throw XCTSkip("Settings could not be returned to its visible root through XCUI.")
    }
}

@MainActor
private func tapLocalizedRow(
    labels: [String],
    in settings: XCUIApplication
) throws {
    let row = settings.staticTexts.matching(NSPredicate(
        format: "label IN %@",
        labels
    )).firstMatch
    for _ in 0..<8 where !row.exists || !row.isHittable {
        settings.swipeUp()
    }
    guard row.waitForExistence(timeout: 4), row.isHittable else {
        throw XCTSkip("Settings row \(labels.joined(separator: "/")) is not XCUI-visible.")
    }
    row.tap()
}

@MainActor
private func selectedAppearanceElement(
    labels: [String],
    in settings: XCUIApplication
) -> XCUIElement {
    let buttons = settings.buttons.matching(NSPredicate(format: "label IN %@", labels))
    if buttons.firstMatch.exists { return buttons.firstMatch }
    return settings.descendants(matching: .any).matching(NSPredicate(
        format: "label IN %@",
        labels
    )).firstMatch
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
