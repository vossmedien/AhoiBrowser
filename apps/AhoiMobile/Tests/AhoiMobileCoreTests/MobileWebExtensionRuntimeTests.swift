import WebKit
import XCTest
@testable import AhoiMobileCore

/// MOB-EXT-01 (ADR 0012 section 4): the bundled spike extension loads and the
/// runtime stays off unless its launch argument is present.
@MainActor
final class MobileWebExtensionRuntimeTests: XCTestCase {
    func testBundledSpikeExtensionLoadsWithItsManifest() async throws {
        let url = try XCTUnwrap(MobileWebExtensionRuntime.spikeExtensionURL)
        let webExtension = try await WKWebExtension(resourceBaseURL: url)
        XCTAssertEqual(webExtension.displayName, "Ahoi Spike")
        XCTAssertEqual(webExtension.manifestVersion, 3)
        XCTAssertTrue(webExtension.requestedPermissions.contains(.declarativeNetRequest))
        XCTAssertTrue(webExtension.requestedPermissions.contains(.storage))
        XCTAssertTrue(webExtension.hasInjectedContent)
        XCTAssertTrue(webExtension.hasContentModificationRules)
        XCTAssertTrue(webExtension.errors.isEmpty, "\(webExtension.errors)")
    }

    func testRuntimeIsOffWithoutTheLaunchArgument() {
        let runtime = MobileWebExtensionRuntime(arguments: [])
        var configuration = WebPage.Configuration()
        runtime.attach(to: &configuration, mode: .normal, usesDefaultWebsiteDataStore: true)
        XCTAssertEqual(runtime.state, .disabled)
        XCTAssertNil(configuration.webExtensionController)
    }

    func testSpikeAttachesOnlyToNormalDefaultStorePages() async {
        let runtime = MobileWebExtensionRuntime(
            arguments: [MobileWebExtensionRuntime.launchArgument]
        )
        var privatePage = WebPage.Configuration()
        runtime.attach(to: &privatePage, mode: .privateBrowsing, usesDefaultWebsiteDataStore: true)
        XCTAssertNil(privatePage.webExtensionController)
        var separated = WebPage.Configuration()
        runtime.attach(to: &separated, mode: .normal, usesDefaultWebsiteDataStore: false)
        XCTAssertNil(separated.webExtensionController)

        var normal = WebPage.Configuration()
        runtime.attach(to: &normal, mode: .normal, usesDefaultWebsiteDataStore: true)
        XCTAssertNotNil(normal.webExtensionController)
        await runtime.waitUntilLoaded()
        XCTAssertEqual(runtime.state, .loaded(displayName: "Ahoi Spike"))
        XCTAssertEqual(runtime.controller?.extensionContexts.count, 1)
    }
}
