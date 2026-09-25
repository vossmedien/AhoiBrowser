import XCTest
import CloudKit
import WebKit
@testable import AhoiMobileCore
import AhoiCloudKitSpike

/// ADR 0011 step 4 on the Companion: per-Workspace namespace, zone discovery,
/// separate sessions and dedicated website data stores.
final class SeparatedWorkspaceSyncTests: XCTestCase {
    // Same vectors as overlay/chromium/src/ahoi/browser/sync/sync_namespace_unittest.cc.
    private static let workspace = "0f8b2c4e-6a1d-4e3b-9c5f-7d2a1b3c4e5f"
    private static let main = SyncNamespaceIdentifiers(
        zoneName: "AhoiBrowserSyncV3",
        subscriptionIdentifier: "AhoiBrowserSyncSubscription",
        keychainAccount: "payload-key"
    )

    private var separated: SyncNamespace {
        SyncNamespace.separatedWorkspace(lowercaseUUID: Self.workspace)!
    }

    // MARK: - Namespace (mirrors the desktop helper)

    func testMainNamespaceKeepsBundleValuesByteForByte() {
        XCTAssertEqual(SyncNamespace.main.resolve(Self.main), Self.main)
        XCTAssertEqual(SyncNamespace.main.resolve(Self.main)?.zoneName, "AhoiBrowserSyncV3")
        let odd = SyncNamespaceIdentifiers(
            zoneName: "AhoiSyncAcceptance-x", subscriptionIdentifier: "", keychainAccount: ""
        )
        XCTAssertEqual(SyncNamespace.main.resolve(odd), odd)
        XCTAssertTrue(SyncNamespace.main.isMain)
        XCTAssertEqual(SyncNamespace.mainZoneName, "AhoiBrowserSyncV3")
    }

    func testSeparatedWorkspaceGetsItsOwnZoneAndKey() throws {
        let ws = try XCTUnwrap(separated.resolve(Self.main))
        XCTAssertEqual(ws.zoneName, "AhoiBrowserSyncV3-ws-" + Self.workspace)
        XCTAssertEqual(ws.subscriptionIdentifier,
                       "AhoiBrowserSyncSubscription-ws-" + Self.workspace)
        XCTAssertEqual(ws.keychainAccount, "payload-key.ws-" + Self.workspace)
        XCTAssertTrue(SyncNamespace.isValidCloudKitZoneName(ws.zoneName))
        // Uppercase input maps to the same lowercase names the desktop writes.
        let upper = try XCTUnwrap(UUID(uuidString: Self.workspace.uppercased()))
        XCTAssertEqual(SyncNamespace.separatedWorkspace(upper)?.resolve(Self.main), ws)
    }

    func testSeparatedNamespaceNeverResolvesToTheMainZone() throws {
        let bases = [
            Self.main,
            .init(zoneName: "AhoiBrowserSyncV3", subscriptionIdentifier: "",
                  keychainAccount: "payload-key"),
            .init(zoneName: "AhoiSyncAcceptance-scope",
                  subscriptionIdentifier: "AhoiSyncAcceptanceSubscription-scope",
                  keychainAccount: "payload-key.acceptance-scope"),
        ]
        for base in bases {
            let ws = try XCTUnwrap(separated.resolve(base))
            XCTAssertNotEqual(ws.zoneName, base.zoneName)
            XCTAssertNotEqual(ws.zoneName, SyncNamespace.mainZoneName)
            XCTAssertNotEqual(ws.keychainAccount, base.keychainAccount)
            XCTAssertFalse(ws.subscriptionIdentifier.isEmpty)
            XCTAssertNotNil(ws.subscriptionID)
            XCTAssertNotEqual(ws.subscriptionIdentifier, base.subscriptionIdentifier)
            XCTAssertTrue(ws.keychainAccount.contains(".ws-"))
        }
        let empty = bases[1]
        XCTAssertEqual(separated.resolve(empty)?.subscriptionIdentifier,
                       "AhoiBrowserSyncV3-ws-" + Self.workspace)

        let other = try XCTUnwrap(SyncNamespace.separatedWorkspace(UUID())?.resolve(Self.main))
        let ws = try XCTUnwrap(separated.resolve(Self.main))
        XCTAssertNotEqual(other.zoneName, ws.zoneName)
        XCTAssertNotEqual(other.keychainAccount, ws.keychainAccount)
        XCTAssertNotEqual(other.subscriptionIdentifier, ws.subscriptionIdentifier)
    }

    func testSeparatedNamespaceFailsClosed() {
        for invalid in ["not-a-uuid", "", Self.workspace.uppercased(), "{\(Self.workspace)}",
                        "00000000-0000-0000-0000-000000000000", Self.workspace + "0",
                        String(Self.workspace.dropLast())] {
            XCTAssertNil(SyncNamespace.separatedWorkspace(lowercaseUUID: invalid), invalid)
        }
        XCTAssertNil(SyncNamespace.separatedWorkspace(
            UUID(uuidString: "00000000-0000-0000-0000-000000000000")!
        ))

        var noZone = Self.main; noZone.zoneName = ""
        XCTAssertNil(separated.resolve(noZone))
        var noKey = Self.main; noKey.keychainAccount = ""
        XCTAssertNil(separated.resolve(noKey))
        var nested = Self.main; nested.zoneName = "AhoiBrowserSyncV3-ws-" + Self.workspace
        XCTAssertNil(separated.resolve(nested))
        var nestedKey = Self.main; nestedKey.keychainAccount = "payload-key.ws-x"
        XCTAssertNil(separated.resolve(nestedKey))
        var badChars = Self.main; badChars.zoneName = "Ahoi Browser"
        XCTAssertNil(separated.resolve(badChars))
        var tooLong = Self.main
        tooLong.zoneName = String(repeating: "a", count: SyncNamespace.maxCloudKitZoneNameLength - 39)
        XCTAssertNil(separated.resolve(tooLong))
        tooLong.zoneName.removeLast()
        XCTAssertNotNil(separated.resolve(tooLong), "Exactly 255 characters is still valid.")
    }

    func testZoneNameValidation() {
        XCTAssertTrue(SyncNamespace.isValidCloudKitZoneName("AhoiBrowserSyncV3"))
        XCTAssertTrue(SyncNamespace.isValidCloudKitZoneName("a-b_c9"))
        XCTAssertTrue(SyncNamespace.isValidCloudKitZoneName(String(repeating: "a", count: 255)))
        XCTAssertFalse(SyncNamespace.isValidCloudKitZoneName(""))
        XCTAssertFalse(SyncNamespace.isValidCloudKitZoneName(String(repeating: "a", count: 256)))
        XCTAssertFalse(SyncNamespace.isValidCloudKitZoneName("_defaultZone"))
        XCTAssertFalse(SyncNamespace.isValidCloudKitZoneName("a.b"))
        XCTAssertFalse(SyncNamespace.isValidCloudKitZoneName("a/b"))
        XCTAssertFalse(SyncNamespace.isValidCloudKitZoneName("\u{00E4}"))
    }

    /// Cross-check against the desktop source: the constants the Companion
    /// derives from must be the ones the Mac writes.
    func testConstantsMatchTheDesktopHeader() throws {
        let header = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("overlay/chromium/src/ahoi/browser/sync/sync_namespace.h")
        let source = try String(contentsOf: header, encoding: .utf8)
        XCTAssertTrue(source.contains(
            "kMainSyncZoneName[] = \"\(SyncNamespace.mainZoneName)\""))
        XCTAssertTrue(source.contains(
            "kWorkspaceZoneInfix[] = \"\(SyncNamespace.workspaceZoneInfix)\""))
        XCTAssertTrue(source.contains(
            "kWorkspaceKeyAccountInfix[] = \"\(SyncNamespace.workspaceKeyAccountInfix)\""))
        XCTAssertTrue(source.contains(
            "kMaxCloudKitZoneNameLength = \(SyncNamespace.maxCloudKitZoneNameLength)"))
    }

    // MARK: - Discovery

    func testDiscoveryPicksOnlyWellFormedWorkspaceZones() {
        let valid = "AhoiBrowserSyncV3-ws-" + Self.workspace
        let zones = [
            "AhoiBrowserSyncV3", "_defaultZone", valid,
            "AhoiBrowserSyncV3-ws-" + Self.workspace.uppercased(),
            "AhoiBrowserSyncV3-ws-not-a-uuid",
            "AhoiBrowserSyncV3-ws-" + Self.workspace + "-ws-" + Self.workspace,
            "AhoiBrowserSyncV3-ws-00000000-0000-0000-0000-000000000000",
            "OtherZone-ws-" + Self.workspace,
            "AhoiBrowserSyncV3X-ws-" + Self.workspace,
        ]
        let found = zones.compactMap {
            SyncNamespace.separatedWorkspace(fromZoneName: $0, base: Self.main)
        }
        XCTAssertEqual(found, [separated])
        XCTAssertNil(SyncNamespace.separatedWorkspace(
            fromZoneName: valid,
            base: .init(zoneName: "", subscriptionIdentifier: "", keychainAccount: "k")
        ))
    }

    @MainActor
    func testDiscoveredWorkspaceIsListedWithSyncOffUntilEnabled() async throws {
        let harness = Harness()
        let lister = FakeZoneLister(["AhoiBrowserSyncV3", harness.zone(harness.workspaceA),
                                     "AhoiBrowserSyncV3-ws-garbage"])
        let refreshed = await harness.coordinator.refreshDiscovery(using: lister)
        XCTAssertTrue(refreshed)

        let entry = try XCTUnwrap(harness.coordinator.entries.first)
        XCTAssertEqual(harness.coordinator.entries.count, 1)
        XCTAssertEqual(entry.workspaceID, harness.workspaceA)
        XCTAssertFalse(entry.syncEnabled)
        XCTAssertEqual(entry.state, .off)
        XCTAssertEqual(entry.identifiers.zoneName, harness.zone(harness.workspaceA))
        XCTAssertFalse(SeparatedWorkspaceEntry.isolationLevelLabel.isEmpty)
        XCTAssertTrue(harness.dataStores.separatedWorkspaceIDs.contains(harness.workspaceA))

        await harness.coordinator.syncEnabledWorkspaces()
        XCTAssertTrue(harness.factoryContexts.isEmpty, "No session before the user opts in.")

        await harness.coordinator.setSyncEnabled(true, for: harness.workspaceA)
        XCTAssertEqual(harness.factoryContexts.map(\.workspaceID), [harness.workspaceA])
        XCTAssertEqual(harness.coordinator.entries.first?.state, .ready)
        XCTAssertEqual(harness.stateStore.load().first?.syncEnabled, true)

        await harness.coordinator.setSyncEnabled(false, for: harness.workspaceA)
        XCTAssertEqual(harness.sessions.first?.cancelled, true)
        XCTAssertEqual(harness.coordinator.entries.first?.state, .off)
        XCTAssertNotNil(harness.coordinator.entries.first, "Disabling keeps the Workspace.")
        XCTAssertTrue(harness.removedStores.isEmpty)
    }

    @MainActor
    func testListingFailureNeverRetiresAndMissingZoneDoes() async {
        let harness = Harness()
        await harness.coordinator.applyDiscoveredZoneNames([
            harness.zone(harness.workspaceA), harness.zone(harness.workspaceB),
        ])
        XCTAssertEqual(harness.coordinator.entries.count, 2)

        let failing = FakeZoneLister([])
        failing.error = CloudKitSyncProviderError.unavailable
        let refreshed = await harness.coordinator.refreshDiscovery(using: failing)
        XCTAssertFalse(refreshed)
        XCTAssertEqual(harness.coordinator.entries.count, 2)
        XCTAssertTrue(harness.removedStores.isEmpty)

        await harness.coordinator.refreshDiscovery(
            using: FakeZoneLister(["AhoiBrowserSyncV3", harness.zone(harness.workspaceB)])
        )
        XCTAssertEqual(harness.coordinator.entries.map(\.workspaceID), [harness.workspaceB])
        XCTAssertEqual(harness.removedStores, [harness.workspaceA])
        XCTAssertEqual(harness.removedLocalData, [harness.workspaceA])
        XCTAssertEqual(harness.coordinator.retirements[harness.workspaceA], .zoneRemoved)
        XCTAssertFalse(harness.dataStores.separatedWorkspaceIDs.contains(harness.workspaceA))
        XCTAssertEqual(harness.stateStore.load().map(\.workspaceID), [harness.workspaceB])
    }

    @MainActor
    func testUnconfiguredCoordinatorListsNothing() async {
        let coordinator = SeparatedWorkspaceSyncCoordinator()
        XCTAssertFalse(coordinator.isConfigured)
        await coordinator.applyDiscoveredZoneNames(["AhoiBrowserSyncV3-ws-" + Self.workspace])
        XCTAssertTrue(coordinator.entries.isEmpty)
        // A base that already looks separated disables the feature entirely.
        let nested = SeparatedWorkspaceSyncCoordinator(base: .init(
            zoneName: "AhoiBrowserSyncV3-ws-x", subscriptionIdentifier: "", keychainAccount: "k"
        ))
        XCTAssertFalse(nested.isConfigured)
    }

    // MARK: - Session isolation

    @MainActor
    func testSessionContextNeverCarriesTheMainNamespace() async throws {
        let harness = Harness()
        await harness.coordinator.applyDiscoveredZoneNames([
            harness.zone(harness.workspaceA), harness.zone(harness.workspaceB),
        ])
        await harness.coordinator.setSyncEnabled(true, for: harness.workspaceA)
        await harness.coordinator.setSyncEnabled(true, for: harness.workspaceB)
        XCTAssertEqual(harness.factoryContexts.count, 2)
        for context in harness.factoryContexts {
            XCTAssertFalse(context.namespace.isMain)
            XCTAssertNotEqual(context.identifiers.zoneName, Self.main.zoneName)
            XCTAssertNotEqual(context.identifiers.keychainAccount, Self.main.keychainAccount)
            XCTAssertNotEqual(context.identifiers.subscriptionIdentifier,
                              Self.main.subscriptionIdentifier)
            XCTAssertEqual(context.namespace.resolve(Self.main), context.identifiers)
        }
        XCTAssertNotEqual(harness.factoryContexts[0].identifiers.zoneName,
                          harness.factoryContexts[1].identifiers.zoneName)
        XCTAssertNotEqual(
            SeparatedWorkspaceCloudKitSessionFactory.storageDirectory(
                for: harness.workspaceA, under: URL(fileURLWithPath: "/support")
            ),
            SeparatedWorkspaceCloudKitSessionFactory.storageDirectory(
                for: harness.workspaceB, under: URL(fileURLWithPath: "/support")
            )
        )
    }

#if DEBUG
    /// Records of a Workspace zone reach only that Workspace's session and
    /// repository; main-zone records never reach the Workspace session.
    @MainActor
    func testWorkspaceZoneRecordsNeverReachTheMainStoreAndViceVersa() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiSeparatedWorkspace-\(UUID().uuidString)", isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let server = FakeZoneServer()
        let workspaceID = UUID()
        let wsZone = try XCTUnwrap(
            SyncNamespace.separatedWorkspace(workspaceID)?.resolve(Self.main)?.zoneName
        )

        // The Mac: its main Profile and its separated Workspace's Profile.
        let macMain = RelayPeer(fileURL: directory.appendingPathComponent("mac-main.json"), key: 0x41)
        let macWorkspace = RelayPeer(fileURL: directory.appendingPathComponent("mac-ws.json"), key: 0x42)
        let mainWorkspace = try await macMain.repository.createWorkspace(name: "Main")
        let separatedWorkspace = try await macWorkspace.repository.createWorkspace(name: "Work")
        try await server.push(macMain, to: Self.main.zoneName)
        try await server.push(macWorkspace, to: wsZone)
        let macWorkspaceRecordIDs = Set(server.zones[wsZone, default: [:]].keys)
        XCTAssertFalse(macWorkspaceRecordIDs.isEmpty)

        // The Companion: main peer plus coordinator-created session.
        let phoneMain = RelayPeer(fileURL: directory.appendingPathComponent("phone-main.json"), key: 0x41)
        var sessionPeer: RelayPeer?
        let coordinator = SeparatedWorkspaceSyncCoordinator(
            base: Self.main,
            sessionFactory: { context in
                let peer = RelayPeer(
                    fileURL: directory.appendingPathComponent("phone-ws.json"), key: 0x42
                )
                sessionPeer = peer
                return .ready(RelaySession(peer: peer, zone: context.identifiers.zoneName,
                                           server: server))
            }
        )
        await coordinator.applyDiscoveredZoneNames(Array(server.zones.keys))
        XCTAssertEqual(coordinator.entries.map(\.workspaceID), [workspaceID])
        await coordinator.setSyncEnabled(true, for: workspaceID)
        await coordinator.syncEnabledWorkspaces()
        try await server.pull(Self.main.zoneName, into: phoneMain)
        try await server.push(phoneMain, to: Self.main.zoneName)

        let mainSnapshot = try await phoneMain.repository.currentSnapshot()
        XCTAssertEqual(mainSnapshot.visibleWorkspaces.map(\.id), [mainWorkspace.id])
        let mainRecordIDs = Set(try await phoneMain.records.allRecords().map(\.recordID))
        XCTAssertTrue(mainRecordIDs.isDisjoint(with: macWorkspaceRecordIDs))

        let peer = try XCTUnwrap(sessionPeer)
        let wsSnapshot = try await peer.repository.currentSnapshot()
        XCTAssertEqual(wsSnapshot.visibleWorkspaces.map(\.id), [separatedWorkspace.id])
        let wsRecordIDs = Set(try await peer.records.allRecords().map(\.recordID))
        XCTAssertFalse(wsRecordIDs.contains(mainWorkspace.id.rawValue))

        // The zones on the "server" stayed disjoint after both uploads.
        XCTAssertTrue(Set(server.zones[Self.main.zoneName, default: [:]].keys)
            .isDisjoint(with: Set(server.zones[wsZone, default: [:]].keys)))
        XCTAssertFalse(server.zones[wsZone, default: [:]].keys.contains(mainWorkspace.id.rawValue))
    }
#endif

    // MARK: - Retirement and data-store lifecycle

    @MainActor
    func testTombstonedWorkspaceRemovesItsDataStore() async throws {
        let harness = Harness()
        await harness.coordinator.applyDiscoveredZoneNames([harness.zone(harness.workspaceA)])
        await harness.coordinator.setSyncEnabled(true, for: harness.workspaceA)
        let session = try XCTUnwrap(harness.sessions.first)
        session.snapshot = try harness.snapshot(named: "Work", for: harness.workspaceA,
                                                tombstoned: false)
        await harness.coordinator.syncEnabledWorkspaces()
        XCTAssertEqual(harness.coordinator.entries.first?.displayName, "Work")
        XCTAssertTrue(harness.removedStores.isEmpty)

        session.snapshot = try harness.snapshot(named: "Work", for: harness.workspaceA,
                                                tombstoned: true)
        await harness.coordinator.syncEnabledWorkspaces()
        XCTAssertTrue(harness.coordinator.entries.isEmpty)
        XCTAssertEqual(harness.coordinator.retirements[harness.workspaceA], .tombstoned)
        XCTAssertEqual(harness.closedTabsBeforeRemoval, [harness.workspaceA])
        XCTAssertEqual(harness.removedStores, [harness.workspaceA])
        XCTAssertTrue(session.cancelled)
    }

    @MainActor
    func testKeyLossPausesAndKeepsTheDataStoreUntilTheKeyReturns() async throws {
        let harness = Harness()
        await harness.coordinator.applyDiscoveredZoneNames([
            harness.zone(harness.workspaceA), harness.zone(harness.workspaceB),
        ])
        // B never had a key here: waiting, never retired.
        harness.activations[harness.workspaceB] = .keyMissing
        await harness.coordinator.setSyncEnabled(true, for: harness.workspaceB)
        XCTAssertEqual(harness.coordinator.entries.first { $0.id == harness.workspaceB }?.state,
                       .waitingForKey)

        await harness.coordinator.setSyncEnabled(true, for: harness.workspaceA)
        let session = try XCTUnwrap(harness.sessions.first)
        await harness.coordinator.syncEnabledWorkspaces()
        XCTAssertEqual(session.syncCount, 1)
        let store = try XCTUnwrap(harness.dataStores.dataStore(for: harness.workspaceA))

        // The key disappears (e.g. iCloud Keychain turned off): paused only.
        session.keyPresent = false
        harness.activations[harness.workspaceA] = .keyMissing
        await harness.coordinator.syncEnabledWorkspaces()
        await harness.coordinator.syncEnabledWorkspaces()
        let paused = try XCTUnwrap(harness.coordinator.entries.first { $0.id == harness.workspaceA })
        XCTAssertEqual(paused.state, .keyMissing)
        XCTAssertNotNil(paused.state.localizedLabel)
        XCTAssertTrue(paused.syncEnabled, "The opt-in stays.")
        XCTAssertEqual(session.syncCount, 1, "Sync stops while the key is missing.")
        XCTAssertTrue(session.cancelled)
        XCTAssertNil(harness.coordinator.retirements[harness.workspaceA])
        XCTAssertTrue(harness.removedStores.isEmpty, "Logins must never be wiped.")
        XCTAssertTrue(harness.removedLocalData.isEmpty)
        XCTAssertTrue(harness.closedTabsBeforeRemoval.isEmpty, "Pages stay open.")
        XCTAssertTrue(harness.dataStores.dataStore(for: harness.workspaceA) === store)
        XCTAssertEqual(harness.stateStore.load().first { $0.workspaceID == harness.workspaceA }?
            .pendingRetirement, nil)

        // The key returns: a new session resumes on the same data store.
        harness.activations.removeValue(forKey: harness.workspaceA)
        await harness.coordinator.syncEnabledWorkspaces()
        XCTAssertEqual(harness.coordinator.entries.first { $0.id == harness.workspaceA }?.state,
                       .ready)
        let resumed = try XCTUnwrap(harness.sessions.last)
        XCTAssertFalse(resumed === session)
        XCTAssertEqual(resumed.syncCount, 1)
        XCTAssertTrue(harness.dataStores.dataStore(for: harness.workspaceA) === store)
        XCTAssertEqual(harness.coordinator.entries.count, 2)
    }

    @MainActor
    func testFailedDataStoreRemovalStaysPendingAndRetries() async {
        let harness = Harness()
        await harness.coordinator.applyDiscoveredZoneNames([harness.zone(harness.workspaceA)])
        harness.removalError = CocoaError(.fileWriteUnknown)
        await harness.coordinator.applyDiscoveredZoneNames([])
        XCTAssertTrue(harness.coordinator.entries.isEmpty, "A retiring Workspace is not listed.")
        XCTAssertTrue(harness.dataStores.separatedWorkspaceIDs.contains(harness.workspaceA),
                      "Fail closed: pages keep the dedicated store until it is removed.")
        XCTAssertEqual(harness.stateStore.load().first?.pendingRetirement, .zoneRemoved)
        XCTAssertTrue(harness.removedLocalData.isEmpty)

        harness.removalError = nil
        await harness.coordinator.syncEnabledWorkspaces()
        XCTAssertEqual(harness.removedStores, [harness.workspaceA])
        XCTAssertEqual(harness.removedLocalData, [harness.workspaceA])
        XCTAssertTrue(harness.stateStore.load().isEmpty)
        XCTAssertFalse(harness.dataStores.separatedWorkspaceIDs.contains(harness.workspaceA))
    }

    @MainActor
    func testDataStoreRegistryKeysDedicatedStoresByWorkspace() async throws {
        let idStore = InMemorySeparatedWorkspaceIDStore()
        var made: [UUID] = []
        var removed: [UUID] = []
        let registry = SeparatedWorkspaceDataStoreRegistry<FakeDataStore>(
            idStore: idStore,
            makeStore: { made.append($0); return FakeDataStore(id: $0) },
            removeStore: { removed.append($0) }
        )
        let a = UUID(), b = UUID(), plain = UUID()
        registry.registerSeparatedWorkspace(a)
        registry.registerSeparatedWorkspace(b)
        XCTAssertNil(registry.dataStore(for: plain), "Other Workspaces keep the normal store.")
        let storeA = try XCTUnwrap(registry.dataStore(for: a))
        XCTAssertTrue(storeA === registry.dataStore(for: a))
        XCTAssertEqual(storeA.id, a)
        XCTAssertFalse(storeA === registry.dataStore(for: b))
        XCTAssertEqual(made, [a, b])

        // Survives a relaunch: restored pages get the dedicated store at once.
        let relaunched = SeparatedWorkspaceDataStoreRegistry<FakeDataStore>(
            idStore: idStore, makeStore: { FakeDataStore(id: $0) }, removeStore: { _ in }
        )
        XCTAssertEqual(relaunched.separatedWorkspaceIDs, [a, b])

        var order: [String] = []
        registry.willRemoveDataStore = { _ in order.append("close-tabs") }
        try await registry.removeDataStore(for: a)
        XCTAssertEqual(removed, [a])
        XCTAssertEqual(order, ["close-tabs"])
        XCTAssertNil(registry.dataStore(for: a))
        XCTAssertEqual(idStore.load(), [b])
    }

    @MainActor
    func testBrowserOpensSeparatedPagesInTheirOwnStoreAndNeverShareThem() async throws {
        let workspaceID = UUID()
        let registry = MobileSeparatedWorkspaceDataStores.webKit(
            idStore: InMemorySeparatedWorkspaceIDStore([workspaceID])
        )
        let browser = MobileBrowserController(
            store: InMemoryMobileBrowserSessionStore(),
            separatedWorkspaceDataStores: registry
        )
        let separatedTab = browser.createTab(
            url: URL(string: "https://separated.example")!,
            workspaceID: WorkspaceID(rawValue: workspaceID)
        )
        let normalTab = browser.createTab(url: URL(string: "https://normal.example")!)
        let separatedStore = try XCTUnwrap(browser.websiteDataStores[separatedTab])
        XCTAssertTrue(separatedStore === registry.dataStore(for: workspaceID))
        XCTAssertFalse(separatedStore === WKWebsiteDataStore.default())
        XCTAssertFalse(separatedStore === browser.websiteDataStores[normalTab])
        XCTAssertEqual(separatedStore.identifier, workspaceID)

        let record = try XCTUnwrap(browser.tabs.first { $0.id == separatedTab })
        XCTAssertFalse(record.participatesInSharedTabs,
                       "A separated tab never enters the main zone's shared tabs.")
        browser.moveTab(separatedTab, to: nil)
        XCTAssertEqual(browser.tabs.first { $0.id == separatedTab }?.workspaceID?.rawValue,
                       workspaceID)
        browser.moveTab(normalTab, to: WorkspaceID(rawValue: workspaceID))
        XCTAssertNil(browser.tabs.first { $0.id == normalTab }?.workspaceID)

        registry.willRemoveDataStore = { [weak browser] id in
            browser?.closeTabs(inWorkspace: id)
        }
        try? await registry.removeDataStore(for: workspaceID)
        XCTAssertFalse(browser.tabs.contains { $0.id == separatedTab })
        XCTAssertTrue(browser.tabs.contains { $0.id == normalTab })
        browser.close(normalTab)
    }
}

// MARK: - Fakes

private final class FakeDataStore {
    let id: UUID
    init(id: UUID) { self.id = id }
}

private final class FakeZoneLister: CloudKitRecordZoneListing, @unchecked Sendable {
    var names: [String]
    var error: Error?
    init(_ names: [String]) { self.names = names }
    func allRecordZoneNames() async throws -> [String] {
        if let error { throw error }
        return names
    }
}

@MainActor
private final class FakeSession: SeparatedWorkspaceSyncSession {
    var snapshot = CompanionSnapshot.empty
    var keyPresent = true
    var cancelled = false
    var syncCount = 0
    func sync() async throws -> CompanionSnapshot { syncCount += 1; return snapshot }
    func isKeyPresent() async throws -> Bool { keyPresent }
    func cancel() async { cancelled = true }
}

@MainActor
private final class Harness {
    let workspaceA = UUID(uuidString: "0f8b2c4e-6a1d-4e3b-9c5f-7d2a1b3c4e5f")!
    let workspaceB = UUID(uuidString: "1a2b3c4d-5e6f-4a1b-8c2d-3e4f5a6b7c8d")!
    let base = SyncNamespaceIdentifiers(
        zoneName: "AhoiBrowserSyncV3",
        subscriptionIdentifier: "AhoiBrowserSyncSubscription",
        keychainAccount: "payload-key"
    )
    let stateStore = InMemorySeparatedWorkspaceStateStore()
    var factoryContexts: [SeparatedWorkspaceSessionContext] = []
    var sessions: [FakeSession] = []
    var activations: [UUID: SeparatedWorkspaceSessionActivation] = [:]
    var removedStores: [UUID] = []
    var removedLocalData: [UUID] = []
    var closedTabsBeforeRemoval: [UUID] = []
    var removalError: Error?
    private(set) var dataStores: SeparatedWorkspaceDataStoreRegistry<FakeDataStore>!
    private(set) var coordinator: SeparatedWorkspaceSyncCoordinator!

    init() {
        dataStores = SeparatedWorkspaceDataStoreRegistry<FakeDataStore>(
            idStore: InMemorySeparatedWorkspaceIDStore(),
            makeStore: { FakeDataStore(id: $0) },
            removeStore: { [unowned self] id in
                if let removalError { throw removalError }
                removedStores.append(id)
            }
        )
        dataStores.willRemoveDataStore = { [unowned self] id in
            closedTabsBeforeRemoval.append(id)
        }
        coordinator = SeparatedWorkspaceSyncCoordinator(
            base: base,
            stateStore: stateStore,
            dataStores: dataStores,
            sessionFactory: { [unowned self] context in
                factoryContexts.append(context)
                if let activation = activations[context.workspaceID] { return activation }
                let session = FakeSession()
                sessions.append(session)
                return .ready(session)
            },
            removeLocalData: { [unowned self] id in removedLocalData.append(id) }
        )
    }

    func zone(_ id: UUID) -> String {
        SyncNamespace.separatedWorkspace(id)!.resolve(base)!.zoneName
    }

    func snapshot(named name: String, for id: UUID, tombstoned: Bool) throws -> CompanionSnapshot {
        let device = DeviceID()
        let clock = HybridLogicalClock(physicalMilliseconds: 1_000, nodeID: device)
        let version = SyncVersion(modifiedAt: clock, modifiedBy: device)
        return CompanionSnapshot(workspaces: [Workspace(
            workspaceID: WorkspaceID(rawValue: id), name: name, icon: "briefcase",
            version: version,
            tombstone: tombstoned ? Tombstone(
                entityID: id, deletedAt: clock, deletedBy: device, originalParentID: nil,
                originalOrderKey: nil, purgeAfterMilliseconds: 0
            ) : nil
        )])
    }
}

#if DEBUG
/// One simulated private database: records per zone name.
@MainActor
private final class FakeZoneServer {
    var zones: [String: [UUID: SyncRecord]] = [:]

    func push(_ peer: RelayPeer, to zone: String) async throws {
        try await peer.bridge.enqueueLocalSnapshot()
        try await peer.bridge.syncNow()
        for record in try await peer.records.allRecords() {
            zones[zone, default: [:]][record.recordID] = record
        }
    }

    func pull(_ zone: String, into peer: RelayPeer) async throws {
        let codec = AppleCloudKitRecordCodec()
        let zoneID = CKRecordZone.ID(zoneName: zone, ownerName: CKCurrentUserDefaultName)
        let copied = try zones[zone, default: [:]].values.map {
            try codec.decode(codec.encode($0, zoneID: zoneID))
        }
        guard !copied.isEmpty else { return }
        _ = try await peer.records.mergeRecords(copied, policy: .transportLastWriterWins)
        try await peer.records.stageFetchedRecords(copied)
        try await peer.bridge.syncNow()
    }
}

private struct RelayPeer {
    let repository: LocalFirstRepository
    let records: InMemorySyncRecordStore
    let bridge: CompanionSyncBridge

    init(fileURL: URL, key: UInt8) {
        let records = InMemorySyncRecordStore()
        let transport = CompanionSyncVisibleTestTransport(recordStore: records)
        let repository = LocalFirstRepository(
            store: FileCompanionStore(fileURL: fileURL), localDeviceID: DeviceID()
        )
        let sealer = KeychainCompanionPayloadSealer(
            configuration: .init(service: "separated-workspace-test", account: "fixture",
                                 keyVersion: 1),
            keyLoader: { Data(repeating: key, count: 32) }
        )
        self.repository = repository
        self.records = records
        self.bridge = CompanionSyncBridge(repository: repository, transport: transport,
                                          sealer: sealer)
    }
}

@MainActor
private final class RelaySession: SeparatedWorkspaceSyncSession {
    let peer: RelayPeer
    let zone: String
    let server: FakeZoneServer

    init(peer: RelayPeer, zone: String, server: FakeZoneServer) {
        self.peer = peer
        self.zone = zone
        self.server = server
    }

    func sync() async throws -> CompanionSnapshot {
        try await server.pull(zone, into: peer)
        try await server.push(peer, to: zone)
        return try await peer.repository.currentSnapshot()
    }

    func isKeyPresent() async throws -> Bool { true }
    func cancel() async {}
}
#endif
