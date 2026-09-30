import XCTest

/// ADR 0012 section 4, MOB-EXT-01 (spike): with `-AhoiWebExtensionSpike`
/// the bundled MV3 test extension (`Sources/AhoiMobileCore/
/// WebExtensionSpike`) injects its content script into a normal page and
/// its `declarativeNetRequest` rule blocks a matching script request.
///
/// On pages whose query contains `ahoi-spike-check` the content script
/// probes two scripts on the page's own origin that differ only in the
/// blocked path, and writes a visible report into the page. Without the
/// launch argument the runtime is off and no report appears.
final class MobileWebExtensionSpikeUITests: MobileBrowserUITestCase {
    private let spikeArgument = "-AhoiWebExtensionSpike"

    @MainActor
    func testBundledExtensionInjectsContentScriptAndBlocksByRule() throws {
        let token = UUID().uuidString.lowercased().prefix(8)
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: [
            "-AhoiUITestFixture", spikeArgument,
        ])
        defer { app.terminate() }

        let first = try checkURL("\(token)-1")
        openExamplePage(first, in: app)
        let report = spikeReport(in: app)
        XCTAssertTrue(
            report.waitForExistence(timeout: Self.adr0012NetworkTimeout),
            "The extension's content script must write its report."
        )
        if !report.exists { attachTree(app, "spike-report-missing") }
        let label = report.label
        XCTAssertTrue(label.contains("content-script=ran"), label)
        XCTAssertTrue(
            label.contains("control=loaded"),
            "The unblocked control script must load: \(label)"
        )
        XCTAssertTrue(
            label.contains("rule=blocked"),
            "The declarativeNetRequest rule must block its script: \(label)"
        )
        attachEvidence(app, "spike-01-content-script-and-rule")

        // Extension storage keeps its count within the session.
        let second = try checkURL("\(token)-2")
        openExamplePage(second, in: app)
        let again = spikeReport(in: app, containing: "visits=")
        XCTAssertTrue(again.waitForExistence(
            timeout: Self.adr0012NetworkTimeout
        ))
        let visits = visitCount(in: again.label)
        XCTAssertNotNil(visits, again.label)
        XCTAssertGreaterThanOrEqual(
            visits ?? 0, 2,
            "storage.local must count across page loads: \(again.label)"
        )
        attachEvidence(app, "spike-01-storage-second-visit")
    }

    @MainActor
    func testRuntimeStaysOffWithoutTheLaunchArgument() throws {
        let token = UUID().uuidString.lowercased().prefix(8)
        ensureVoiceOverOff()
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        defer { app.terminate() }

        openExamplePage(try checkURL("\(token)-off"), in: app)
        // Give a would-be content script the same time it gets above.
        RunLoop.current.run(until: Date().addingTimeInterval(4))
        XCTAssertFalse(
            spikeReport(in: app).exists,
            "Without the launch argument no extension may run."
        )
        attachEvidence(app, "spike-01-off-without-argument")
    }

    private func checkURL(_ value: String) throws -> URL {
        try XCTUnwrap(URL(
            string: "https://example.com/?ahoi-spike-check=\(value)"
        ))
    }

    @MainActor
    private func spikeReport(
        in app: XCUIApplication,
        containing text: String = "Ahoi Spike report"
    ) -> XCUIElement {
        app.webViews.descendants(matching: .any).matching(NSPredicate(
            format: "label CONTAINS %@ AND label CONTAINS %@",
            "Ahoi Spike report", text
        )).firstMatch
    }

    private func visitCount(in label: String) -> Int? {
        guard let range = label.range(of: "visits=") else { return nil }
        return Int(label[range.upperBound...].prefix { $0.isNumber })
    }
}
