import WebKit
import UIKit
import AhoiCloudKitSpike
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
        XCTAssertTrue(webExtension.requestedPermissions.contains(.declarativeNetRequestWithHostAccess))
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

    func testEffectiveConsentExpiryOverlapsAndContextIsolation() async throws {
        let runtime = MobileWebExtensionRuntime(arguments: [MobileWebExtensionRuntime.launchArgument])
        var config = WebPage.Configuration()
        runtime.attach(to: &config, mode: .normal, usesDefaultWebsiteDataStore: true)
        await runtime.waitUntilLoaded()
        let context = try XCTUnwrap(runtime.context)
        let url = try XCTUnwrap(URL(string: "https://example.com/check"))
        let other = try XCTUnwrap(URL(string: "https://example.org/check"))
        XCTAssertFalse(context.hasAccessToPrivateData)
        XCTAssertFalse(context.hasAccess(to: url))
        XCTAssertTrue(context.grantedPermissions.isEmpty)
        XCTAssertTrue(context.grantedPermissionMatchPatterns.isEmpty)
        // Exact whole seconds survive the Date/NSDate epoch conversion without
        // subsecond floating-point rounding; keep exact renewal assertions.
        let expiry = Date(timeIntervalSince1970: Date().timeIntervalSince1970.rounded(.down) + 60)
        try await runtime.setSiteChoice(.allow, for: url, expiration: expiry)
        XCTAssertEqual(context.permissionStatus(for: url), .grantedExplicitly)
        XCTAssertTrue(context.hasPermission(.storage))
        XCTAssertTrue(context.hasPermission(.declarativeNetRequestWithHostAccess))
        XCTAssertTrue(context.hasPermission(.nativeMessaging))
        XCTAssertFalse(context.hasAccess(to: other))
        XCTAssertFalse(context.hasAccess(to: URL(string: "https://sub.example.com/check")!))
        XCTAssertFalse(context.hasAccess(to: URL(string: "http://example.com/check")!))
        XCTAssertEqual(context.grantedPermissions[.storage], expiry)
        XCTAssertEqual(context.grantedPermissions[.nativeMessaging], expiry)
        let renewedExpiry = expiry.addingTimeInterval(60)
        try await runtime.setSiteChoice(.allow, for: url, expiration: renewedExpiry)
        for permission in context.webExtension.requestedPermissions {
            XCTAssertEqual(context.grantedPermissions[permission], renewedExpiry,
                           "Explicit renewal must replace every consented API Date.")
        }
        let wide = try WKWebExtension.MatchPattern(scheme: "https", host: "*", path: "/*")
        context.setPermissionStatus(.grantedExplicitly, for: wide, expirationDate: expiry)
        try await runtime.setSiteChoice(.revoke, for: url, expiration: expiry)
        XCTAssertTrue(context.isLoaded, "Confirmed public rule removal must retain the memory-only context.")
        XCTAssertFalse(context.hasAccess(to: url))
        XCTAssertFalse(context.hasAccess(to: other), "Revoke must remove the overlapping broad grant.")
        XCTAssertFalse(context.hasPermission(.storage))
        XCTAssertFalse(context.hasPermission(.declarativeNetRequestWithHostAccess))
        XCTAssertFalse(context.hasPermission(.nativeMessaging))
        try await runtime.setSiteChoice(.deny, for: url, expiration: expiry)
        XCTAssertEqual(context.permissionStatus(for: url), .deniedExplicitly)
        try await runtime.setSiteChoice(.allow, for: url, expiration: Date().addingTimeInterval(8))
        try await Task.sleep(for: .milliseconds(8300))
        XCTAssertFalse(context.hasAccess(to: url))
        XCTAssertTrue(context.grantedPermissions.isEmpty)
        XCTAssertTrue(context.grantedPermissionMatchPatterns.isEmpty)
        XCTAssertTrue(context.isLoaded, "Date expiry must retain storage after confirmed rule removal.")
        XCTAssertThrowsError(try MobileWebExtensionRuntime.sitePattern(for: URL(string: "http://example.com")!))
        let store = try XCTUnwrap(runtime.controller?.configuration.defaultWebsiteDataStore)
        print("SDK27 datastore persistent=\(store.isPersistent) identity=\(ObjectIdentifier(store)) pageIdentity=\(ObjectIdentifier(config.websiteDataStore)) identical=\(store === config.websiteDataStore)")
        XCTAssertTrue(runtime.controller?.delegate === runtime.host)
        let folder = try fixtureCopyInTemporaryFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        try await runtime.loadUnpackedFromFiles(folder)
        let imported = try XCTUnwrap(runtime.controller?.extensionContexts.first { $0 !== context })
        XCTAssertTrue(imported.grantedPermissions.isEmpty)
        XCTAssertTrue(imported.grantedPermissionMatchPatterns.isEmpty)
        XCTAssertFalse(imported.hasAccessToPrivateData)
        try runtime.unloadImportedFilesSpike()
        try runtime.unloadImportedFilesSpike()
        XCTAssertEqual(runtime.controller?.extensionContexts.count, 1)
        try runtime.unloadBundledSpike()
        try runtime.unloadBundledSpike()
        XCTAssertEqual(runtime.controller?.extensionContexts.count, 0)
    }

    func testRefreshSerializesRevokeAndSceneSuspension() async throws {
        let runtime = MobileWebExtensionRuntime(arguments: [MobileWebExtensionRuntime.launchArgument])
        var configuration = WebPage.Configuration()
        runtime.attach(to: &configuration, mode: .normal, usesDefaultWebsiteDataStore: true)
        await runtime.waitUntilLoaded()
        let context = try XCTUnwrap(runtime.context)
        let url = try XCTUnwrap(URL(string: "https://example.com/concurrent"))
        let expiry = Date().addingTimeInterval(180)
        try await runtime.setSiteChoice(.allow, for: url, expiration: expiry)
        let firstRefresh = Task { try await runtime.refreshRuleControl() }
        await Task.yield()
        XCTAssertTrue(runtime.ruleUpdateInProgress, "The real public SDK refresh must be in flight.")
        try await runtime.setSiteChoice(.revoke, for: url, expiration: expiry)
        try await firstRefresh.value
        XCTAssertFalse(context.hasAccess(to: url), "Refresh must not drop the confirmed revoke as busy.")
        XCTAssertTrue(context.grantedPermissions.isEmpty)
        XCTAssertTrue(context.isLoaded, "Confirmed cleanup must retain extension memory storage.")

        try await runtime.setSiteChoice(.allow, for: url, expiration: expiry)
        let secondRefresh = Task { try await runtime.refreshRuleControl() }
        await Task.yield()
        XCTAssertTrue(runtime.ruleUpdateInProgress)
        let suspension = try XCTUnwrap(runtime.suspendRuleControl())
        await suspension.value
        try await secondRefresh.value
        XCTAssertTrue(context.hasAccess(to: url), "Scene suspension must not extend or duplicate grants.")
        XCTAssertTrue(context.isLoaded)
        // A visible PageProbe separately proves that the acknowledged removal
        // stops actual DNR blocking after scene deactivation.
        try await runtime.setSiteChoice(.revoke, for: url, expiration: expiry)
        XCTAssertFalse(context.hasAccess(to: url))
        try runtime.unloadBundledSpike()
    }

    func testUnboundPermissionCallbacksDenyExactlyOnceAndNoPresenterIsSafe() async throws {
        let runtime = MobileWebExtensionRuntime(arguments: [MobileWebExtensionRuntime.launchArgument])
        var config = WebPage.Configuration()
        runtime.attach(to: &config, mode: .normal, usesDefaultWebsiteDataStore: true)
        await runtime.waitUntilLoaded()
        let controller = try XCTUnwrap(runtime.controller)
        let context = try XCTUnwrap(runtime.context)
        var calls = 0
        runtime.host.webExtensionController(controller, promptForPermissions: [.storage], in: nil, for: context) { granted, date in
            calls += 1; XCTAssertTrue(granted.isEmpty); XCTAssertNil(date)
        }
        runtime.host.webExtensionController(controller, promptForPermissionToAccess: [URL(string: "https://example.com")!], in: nil, for: context) { granted, date in
            calls += 1; XCTAssertTrue(granted.isEmpty); XCTAssertNil(date)
        }
        let all = try WKWebExtension.MatchPattern(scheme: "https", host: "*", path: "/*")
        runtime.host.webExtensionController(controller, promptForPermissionMatchPatterns: [all], in: nil, for: context) { granted, date in
            calls += 1; XCTAssertTrue(granted.isEmpty); XCTAssertNil(date)
        }
        XCTAssertEqual(calls, 3)
        do {
            try await runtime.setSiteChoice(.allow, for: URL(string: "https://example.com")!,
                                            expiration: Date().addingTimeInterval(60), isCurrent: { false })
            XCTFail("An invalidated source must not grant APIs or start rule control.")
        } catch is CancellationError { }
        XCTAssertTrue(context.isLoaded)
        XCTAssertTrue(context.grantedPermissionMatchPatterns.isEmpty)
        XCTAssertNil(runtime.host.request(popup: true))
        runtime.host.cancel(); runtime.host.cancel()
        XCTAssertTrue(context.grantedPermissions.isEmpty)
    }

    func testQueuedSourceInvalidationAndLatePopupCompleteOnce() async throws {
        let runtime = MobileWebExtensionRuntime(arguments: [MobileWebExtensionRuntime.launchArgument])
        var configuration = WebPage.Configuration()
        runtime.attach(to: &configuration, mode: .normal, usesDefaultWebsiteDataStore: true)
        await runtime.waitUntilLoaded()
        let context = try XCTUnwrap(runtime.context)
        let controller = try XCTUnwrap(runtime.controller)
        // This check requires the existing app as its public XCTest host.
        for _ in 0..<100 where !UIApplication.shared.connectedScenes.contains(where: { $0.activationState == .foregroundActive }) {
            try await Task.sleep(for: .milliseconds(50))
        }
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.first(where: { $0.activationState == .foregroundActive }) as? UIWindowScene)
        let browser = MobileBrowserController()
        let tab = browser.createTab()
        let page = WebPage(configuration: configuration)
        browser.pages[tab] = page
        browser.websiteDataStores[tab] = configuration.websiteDataStore
        let url = URL(string: "https://example.com/queued")!
        page.load(simulatedRequest: URLRequest(url: url), responseHTML: "<html>Queued source</html>")
        for _ in 0..<100 where page.url != url { try await Task.sleep(for: .milliseconds(10)) }
        XCTAssertEqual(page.url, url)
        let anchor = UIViewController()
        let window = UIWindow(windowScene: scene)
        window.rootViewController = anchor
        window.makeKeyAndVisible()
        defer { window.isHidden = true; window.rootViewController = nil }
        runtime.host.bind(anchor: anchor, browser: browser)
        let queued = try XCTUnwrap(runtime.host.request(popup: false))
        XCTAssertNil(runtime.host.request(popup: true), "A queued action already owns the source.")
        var unsolicitedCompletions = 0
        let unsolicited = try XCTUnwrap(context.action(for: nil))
        runtime.host.webExtensionController(controller, presentActionPopup: unsolicited, for: context) { error in
            unsolicitedCompletions += 1; XCTAssertNotNil(error)
        }
        XCTAssertEqual(unsolicitedCompletions, 1)
        XCTAssertNil(runtime.host.request(popup: true), "A late callback must preserve the queued consent owner.")
        browser.selectedTabID = nil
        browser.selectedTabID = tab
        queued()
        XCTAssertNil(anchor.presentedViewController, "Switch away/back before onDismiss must invalidate.")
        let workspaceQueued = try XCTUnwrap(runtime.host.request(popup: false))
        browser.tabs[0].workspaceID = WorkspaceID()
        browser.tabs[0].workspaceID = nil
        workspaceQueued()
        XCTAssertNil(anchor.presentedViewController)
        let siteQueued = try XCTUnwrap(runtime.host.request(popup: false))
        let other = URL(string: "https://example.org/queued")!
        page.load(simulatedRequest: URLRequest(url: other), responseHTML: "<html>Other site</html>")
        for _ in 0..<100 where page.url != other { try await Task.sleep(for: .milliseconds(10)) }
        XCTAssertEqual(page.url, other)
        runtime.host.bind(anchor: anchor, browser: browser)
        page.load(simulatedRequest: URLRequest(url: url), responseHTML: "<html>Original site</html>")
        for _ in 0..<100 where page.url != url { try await Task.sleep(for: .milliseconds(10)) }
        XCTAssertEqual(page.url, url)
        runtime.host.bind(anchor: anchor, browser: browser)
        siteQueued()
        XCTAssertNil(anchor.presentedViewController)
        let sceneQueued = try XCTUnwrap(runtime.host.request(popup: false))
        NotificationCenter.default.post(name: UIScene.willDeactivateNotification, object: scene)
        sceneQueued()
        XCTAssertNil(anchor.presentedViewController)
        let contextQueued = try XCTUnwrap(runtime.host.request(popup: false))
        let action = try XCTUnwrap(context.action(for: nil))
        try runtime.unloadBundledSpike()
        try runtime.unloadBundledSpike()
        contextQueued()
        XCTAssertNil(anchor.presentedViewController)
        var completed = 0
        runtime.host.webExtensionController(controller, presentActionPopup: action, for: context) { error in
            completed += 1; XCTAssertNotNil(error)
        }
        runtime.host.cancel(); runtime.host.cancel()
        XCTAssertEqual(completed, 1)
        XCTAssertFalse(context.isLoaded)
        XCTAssertTrue(context.grantedPermissions.isEmpty)
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
