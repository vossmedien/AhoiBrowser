import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class MobileBrowserCoreTests: XCTestCase {
    func testInputRouterDistinguishesURLsDomainsAndSearches() throws {
        XCTAssertEqual(
            try MobileBrowserInputRouter.resolve("https://example.com/path").absoluteString,
            "https://example.com/path"
        )
        XCTAssertEqual(
            try MobileBrowserInputRouter.resolve("example.com").absoluteString,
            "https://example.com"
        )
        XCTAssertEqual(
            try MobileBrowserInputRouter.resolve("ahoi browser").absoluteString,
            "https://duckduckgo.com/?q=ahoi%20browser"
        )
    }

    func testInputRouterRejectsCredentialsAndUnsafeSchemes() {
        XCTAssertThrowsError(try MobileBrowserInputRouter.resolve("javascript:alert(1)"))
        XCTAssertThrowsError(try MobileBrowserInputRouter.resolve("file:///tmp/private"))
        XCTAssertThrowsError(try MobileBrowserInputRouter.resolve("https://user:secret@example.com"))
        XCTAssertThrowsError(try MobileBrowserInputRouter.resolve("data:text/html,hello"))
    }

    @MainActor
    func testNavigationFailureClassificationKeepsOfflineAndTimeoutDistinct() {
        XCTAssertEqual(
            MobileBrowserController.classifyNavigationFailure(URLError(.notConnectedToInternet)),
            .offline
        )
        XCTAssertEqual(
            MobileBrowserController.classifyNavigationFailure(URLError(.timedOut)),
            .timedOut
        )
        XCTAssertEqual(
            MobileBrowserController.classifyNavigationFailure(URLError(.badURL)),
            .invalidURL
        )
    }

    @MainActor
    func testColdStartExternalURLWaitsForRestoreAndOpensExactlyOnce() async {
        let existing = MobileTabRecord(url: "https://restored.example")
        let store = InMemoryMobileBrowserSessionStore(snapshot: .init(
            tabs: [existing],
            selectedTabID: existing.id
        ))
        let browser = MobileBrowserController(store: store)
        let incoming = URL(string: "https://incoming.example/path")!

        browser.handleExternalURL(incoming)
        browser.handleExternalURL(incoming)
        XCTAssertTrue(browser.tabs.isEmpty)

        await browser.load()

        XCTAssertEqual(browser.tabs.count, 2)
        XCTAssertEqual(browser.selectedTab?.url, incoming.absoluteString)
        browser.handleExternalURL(incoming)
        XCTAssertEqual(browser.tabs.count, 2)
    }

    func testSessionSnapshotNeverPersistsPrivateTabs() {
        let normal = MobileTabRecord(url: "https://example.com", mode: .normal)
        let privateTab = MobileTabRecord(
            url: "https://private.example",
            mode: .privateBrowsing
        )
        let snapshot = MobileBrowserSessionSnapshot(
            tabs: [normal, privateTab],
            selectedTabID: privateTab.id
        )
        XCTAssertEqual(snapshot.tabs, [normal])
        XCTAssertEqual(snapshot.selectedTabID, normal.id)
    }

    func testFileSessionStoreRoundtripIsStableAndPrivateFree() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiMobileSessionTests-\(UUID())", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let store = FileMobileBrowserSessionStore(
            fileURL: directory.appendingPathComponent("session.json")
        )
        let normal = MobileTabRecord(
            title: "Example",
            url: "https://example.com",
            createdAt: Date(timeIntervalSince1970: 1_700_000_000),
            lastActiveAt: Date(timeIntervalSince1970: 1_700_000_001),
            isSaved: true,
            websiteTintARGB: 0xFFF9_7316
        )
        try await store.save(.init(tabs: [normal], selectedTabID: normal.id))
        let loaded = try await store.load()
        XCTAssertEqual(loaded.tabs, [normal])
        XCTAssertEqual(loaded.selectedTabID, normal.id)
    }

    func testLegacySessionWithoutWebsiteTintStillDecodes() throws {
        let tab = MobileTabRecord(title: "Legacy", url: "https://legacy.example")
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .millisecondsSince1970
        let encoded = try encoder.encode(tab)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .millisecondsSince1970
        let decoded = try decoder.decode(MobileTabRecord.self, from: encoded)
        XCTAssertNil(decoded.websiteTintARGB)
        XCTAssertEqual(decoded.url, tab.url)
    }

    func testCompanionStorageMigrationCopiesBackupAndIsIdempotent() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiMobileMigrationTests-\(UUID())", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let legacy = root.appendingPathComponent("AhoiCompanion", isDirectory: true)
        let mobile = root.appendingPathComponent("AhoiMobile", isDirectory: true)
        try FileManager.default.createDirectory(at: legacy, withIntermediateDirectories: true)
        let original = Data("legacy-snapshot".utf8)
        try original.write(to: legacy.appendingPathComponent("snapshot.json"))

        try MobileStorageMigrator.migrateIfNeeded(
            legacyDirectory: legacy,
            destinationDirectory: mobile
        )
        XCTAssertEqual(
            try Data(contentsOf: mobile.appendingPathComponent("snapshot.json")),
            original
        )
        XCTAssertEqual(
            try Data(contentsOf: mobile
                .appendingPathComponent("CompanionBackup-v1", isDirectory: true)
                .appendingPathComponent("snapshot.json")),
            original
        )

        try Data("new-mobile-state".utf8).write(
            to: mobile.appendingPathComponent("snapshot.json"),
            options: [.atomic]
        )
        try MobileStorageMigrator.migrateIfNeeded(
            legacyDirectory: legacy,
            destinationDirectory: mobile
        )
        XCTAssertEqual(
            try Data(contentsOf: mobile.appendingPathComponent("snapshot.json")),
            Data("new-mobile-state".utf8)
        )
    }

    func testLocalNavigationCreatesSyncableHistoryWithoutPrivateState() async throws {
        let deviceID = DeviceID()
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: deviceID
        )
        let visit = try await repository.recordLocalHistoryVisit(
            title: "Example",
            url: "https://example.com",
            transition: "typed"
        )
        XCTAssertEqual(visit.deviceID, deviceID)
        XCTAssertEqual(visit.title, "Example")
        XCTAssertEqual(visit.transition, "typed")
        let snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleHistory, [visit])
    }

    func testLocalMobileTabPublicationUsesMobileDeviceAndStableSession() async throws {
        let deviceID = DeviceID()
        let sessionID = DeviceSessionID()
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: deviceID
        )
        let first = try await repository.publishLocalMobileTab(
            tabID: UUID(),
            sessionID: sessionID,
            deviceName: "Test iPhone",
            deviceKind: .iPhone,
            workspaceID: nil,
            title: "Example",
            url: "https://example.com",
            pinned: false
        )
        XCTAssertEqual(first.device.id, deviceID)
        XCTAssertEqual(first.session.id, sessionID)
        XCTAssertEqual(first.tab.deviceKind, .iPhone)
        XCTAssertEqual(first.tab.context, .normal)
        XCTAssertTrue(first.tab.isOpen)

        let closed = try await repository.closeLocalMobileTab(first.tab.id.rawValue)
        XCTAssertEqual(closed?.isDeleted, true)
        XCTAssertNotNil(closed?.tombstone)
    }

    @MainActor
    func testMobileSessionReconciliationPublishesNormalAndTombstonesStaleTabs() async throws {
        let deviceID = DeviceID()
        let sessionID = DeviceSessionID()
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: deviceID
        )
        let suiteName = "AhoiMobileReconciliationTests-\(UUID())"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }
        let model = CompanionAppModel(
            repository: repository,
            mobileSessionID: sessionID,
            mobileDeviceName: "Test iPad",
            mobileDeviceKind: .iPad,
            defaults: defaults
        )
        await model.load()
        let normal = MobileTabRecord(
            title: "Published",
            url: "https://example.com",
            mode: .normal,
            websiteTintARGB: 0xFFF9_7316
        )
        let privateTab = MobileTabRecord(
            title: "Private",
            url: "https://private.example",
            mode: .privateBrowsing,
            websiteTintARGB: 0xFF88_22CC
        )

        await model.reconcilePublishedMobileTabs([normal, privateTab])
        XCTAssertEqual(model.snapshot.visibleRemoteTabs.map(\.id.rawValue), [normal.id])
        XCTAssertEqual(model.snapshot.visibleRemoteTabs.first?.deviceKind, .iPad)
        XCTAssertEqual(model.snapshot.visibleRemoteTabs.first?.title, "Published")

        await model.reconcilePublishedMobileTabs([])
        XCTAssertTrue(model.snapshot.visibleRemoteTabs.isEmpty)
        let stored = (try await repository.currentSnapshot()).remoteTabs
        XCTAssertEqual(stored.first(where: { $0.id.rawValue == normal.id })?.isDeleted, true)
    }

    func testExternalOpenDeduplicatorSuppressesOnlyImmediateDuplicate() throws {
        let url = try XCTUnwrap(URL(string: "https://example.com/path"))
        let start = Date(timeIntervalSince1970: 1_700_000_000)
        var deduplicator = MobileExternalOpenDeduplicator(interval: 1.5)
        XCTAssertTrue(deduplicator.accepts(url, now: start))
        XCTAssertFalse(deduplicator.accepts(url, now: start.addingTimeInterval(1)))
        XCTAssertTrue(deduplicator.accepts(url, now: start.addingTimeInterval(3)))
    }

    func testDownloadFilenameCannotEscapeDestinationDirectory() {
        XCTAssertEqual(
            MobileDownloadCoordinator.safeFilename("../../account.txt", fallback: "download"),
            "account.txt"
        )
        XCTAssertEqual(
            MobileDownloadCoordinator.safeFilename("..", fallback: "download"),
            "download"
        )
        XCTAssertEqual(
            MobileDownloadCoordinator.safeFilename("report:final.pdf", fallback: "download"),
            "report-final.pdf"
        )
    }
}
