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

#if DEBUG
    func testUnpackedFilesCopyLoadsBesideTheBundledSpikeAndUnloads() async throws {
        let folder = try fixtureCopyInTemporaryFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let runtime = MobileWebExtensionRuntime(
            arguments: [MobileWebExtensionRuntime.launchArgument]
        )
        var normal = WebPage.Configuration()
        runtime.attach(to: &normal, mode: .normal, usesDefaultWebsiteDataStore: true)
        await runtime.waitUntilLoaded()
        XCTAssertEqual(runtime.controller?.extensionContexts.count, 1)

        try await runtime.loadUnpackedFromFiles(folder)
        XCTAssertEqual(runtime.importedDisplayName, "Ahoi Spike")
        XCTAssertEqual(runtime.controller?.extensionContexts.count, 2)
        try runtime.unloadImportedFilesSpike()
        XCTAssertNil(runtime.importedDisplayName)
        XCTAssertEqual(runtime.controller?.extensionContexts.count, 1)
    }

    func testFilesSpikeRejectsChangedCodeAndDisabledRuntime() async throws {
        let folder = try fixtureCopyInTemporaryFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let disabled = MobileWebExtensionRuntime(arguments: [])
        do {
            try await disabled.loadUnpackedFromFiles(folder)
            XCTFail("Disabled runtime must not load Files code")
        } catch let error as MobileWebExtensionRuntime.FilesSpikeError {
            XCTAssertEqual(error, .disabled)
        }
        try Data("changed code".utf8).write(
            to: folder.appendingPathComponent("content.js"), options: .atomic)
        let enabled = MobileWebExtensionRuntime(
            arguments: [MobileWebExtensionRuntime.launchArgument]
        )
        do {
            try await enabled.loadUnpackedFromFiles(folder)
            XCTFail("Changed fixture must not be loaded")
        } catch let error as MobileWebExtensionRuntime.FilesSpikeError {
            XCTAssertEqual(error, .fixtureMismatch)
        }
        XCTAssertNil(enabled.importedDisplayName)
        XCTAssertEqual(enabled.controller?.extensionContexts.count, 0)
    }

    private func fixtureCopyInTemporaryFolder() throws -> URL {
        let bundled = try XCTUnwrap(MobileWebExtensionRuntime.spikeExtensionURL)
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(
            "Ahoi-Files-Spike-Test-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: false)
        for filename in ["manifest.json", "content.js", "rules.json", "popup.html", "popup.js"] {
            try FileManager.default.copyItem(
                at: bundled.appendingPathComponent(filename),
                to: folder.appendingPathComponent(filename))
        }
        return folder
    }
#endif
}
