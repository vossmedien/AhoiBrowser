import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

#if canImport(CloudKit)
import CloudKit
#endif

final class CompanionCoreTests: XCTestCase {
    func testRemoteTabRejectsIncognitoAndURLUserInfo() throws {
        let device = DeviceID()
        let version = makeVersion(device: device, milliseconds: 1)

        XCTAssertThrowsError(try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: DeviceSessionID(),
            title: "Private",
            url: "https://example.test",
            lastActiveAt: version.modifiedAt,
            context: .incognito,
            version: version
        )) { error in
            XCTAssertEqual(error as? CompanionModelError, .incognitoNotSyncable)
        }

        XCTAssertThrowsError(try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: DeviceSessionID(),
            title: "Secret",
            url: "https://user:password@example.test",
            lastActiveAt: version.modifiedAt,
            version: version
        )) { error in
            XCTAssertEqual(error as? CompanionModelError, .remoteTabURLNotAllowed)
        }
    }

    func testRepositoryKeepsRemoteTabsSearchableLocally() async throws {
        let device = DeviceID()
        let now = UInt64(Date().timeIntervalSince1970 * 1_000)
        let version = makeVersion(device: device, milliseconds: now)
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Research",
            version: version
        )
        let tab = try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .iPad,
            deviceName: "iPad Pro",
            sessionID: DeviceSessionID(),
            workspaceID: workspace.id,
            workspaceName: workspace.name,
            title: "Ahoi Docs",
            url: "https://example.test/docs",
            lastActiveAt: version.modifiedAt,
            version: version
        )
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore()
        )
        try await repository.upsert(Device(
            deviceID: device,
            name: "iPad Pro",
            kind: .iPad,
            lastSeenAt: version.modifiedAt,
            isOnline: true,
            version: version
        ))
        try await repository.upsert(DeviceSession(
            sessionID: tab.sessionID,
            deviceID: device,
            deviceName: "iPad Pro",
            deviceKind: .iPad,
            lastActiveAt: version.modifiedAt,
            isOnline: true,
            version: version
        ))
        try await repository.upsert(workspace)
        try await repository.upsert(tab)

        let results = try await repository.search("example.test")

        XCTAssertEqual(results.count, 1)
        XCTAssertEqual(results.first?.kind, .remoteTab)
        XCTAssertEqual(results.first?.deviceName, "iPad Pro")
    }

    func testRemoteTabsRequireFreshMatchingSessionAndSeparateActionWindow() throws {
        let device = DeviceID()
        let now: UInt64 = 2_000_000_000_000
        let sessionVersion = makeVersion(
            device: device,
            milliseconds: now - 20 * 60 * 1_000
        )
        let sessionID = DeviceSessionID()
        let session = DeviceSession(
            sessionID: sessionID,
            deviceID: device,
            deviceName: "Mac",
            deviceKind: .mac,
            lastActiveAt: sessionVersion.modifiedAt,
            isOnline: true,
            version: sessionVersion
        )
        let sourceDevice = Device(
            deviceID: device,
            name: "Mac",
            kind: .mac,
            lastSeenAt: sessionVersion.modifiedAt,
            isOnline: true,
            version: sessionVersion
        )
        let tab = try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: sessionID,
            title: "Ahoi",
            url: "https://example.test",
            lastActiveAt: sessionVersion.modifiedAt,
            version: sessionVersion
        )
        let snapshot = CompanionSnapshot(
            devices: [sourceDevice],
            sessions: [session],
            remoteTabs: [tab]
        )

        XCTAssertEqual(snapshot.visibleRemoteTabs(atMilliseconds: now), [tab])
        XCTAssertFalse(snapshot.isRemoteTabActionable(tab, atMilliseconds: now))

        let expiredNow = now + CompanionSnapshot.remoteSessionVisibleAgeMilliseconds + 1
        XCTAssertTrue(snapshot.visibleRemoteTabs(atMilliseconds: expiredNow).isEmpty)

        let revokedDevice = Device(
            deviceID: device,
            name: "Mac",
            kind: .mac,
            lastSeenAt: sessionVersion.modifiedAt,
            isOnline: true,
            isRevoked: true,
            version: sessionVersion
        )
        let revokedSnapshot = CompanionSnapshot(
            devices: [revokedDevice],
            sessions: [session],
            remoteTabs: [tab]
        )
        XCTAssertTrue(revokedSnapshot.visibleRemoteTabs(atMilliseconds: now).isEmpty)
        XCTAssertFalse(revokedSnapshot.isRemoteTabActionable(tab, atMilliseconds: now))
    }

    func testRemoteTabWireSchemaUsesStableSidebarFieldNames() throws {
        let device = DeviceID()
        let version = makeVersion(device: device, milliseconds: 11)
        let tab = try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac mini",
            sessionID: DeviceSessionID(),
            workspaceID: WorkspaceID(),
            workspaceName: "Main",
            title: "Ahoi",
            url: "https://example.test",
            lastActiveAt: version.modifiedAt,
            version: version
        )
        let object = try XCTUnwrap(
            try JSONSerialization.jsonObject(
                with: JSONEncoder().encode(tab)
            ) as? [String: Any]
        )

        XCTAssertNotNil(object["tabID"])
        XCTAssertNotNil(object["deviceID"])
        XCTAssertNotNil(object["deviceKind"])
        XCTAssertNotNil(object["deviceName"])
        XCTAssertNotNil(object["workspaceID"])
        XCTAssertNotNil(object["workspaceName"])
        XCTAssertNotNil(object["lastActiveAt"])
        XCTAssertNil(object["id"])
    }

    func testDeviceAndWorkspaceWireSchemaUsesSidebarMetadataNames() throws {
        let deviceID = DeviceID()
        let version = makeVersion(device: deviceID, milliseconds: 11)
        let device = Device(
            deviceID: deviceID,
            name: "Ahoi Mac",
            kind: .mac,
            lastSeenAt: version.modifiedAt,
            version: version
        )
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Main",
            version: version
        )
        let deviceObject = try XCTUnwrap(
            try JSONSerialization.jsonObject(
                with: JSONEncoder().encode(device)
            ) as? [String: Any]
        )
        let workspaceObject = try XCTUnwrap(
            try JSONSerialization.jsonObject(
                with: JSONEncoder().encode(workspace)
            ) as? [String: Any]
        )

        XCTAssertNotNil(deviceObject["deviceID"])
        XCTAssertEqual(deviceObject["deviceName"] as? String, "Ahoi Mac")
        XCTAssertEqual(deviceObject["deviceKind"] as? String, "mac")
        XCTAssertNil(deviceObject["name"])
        XCTAssertNil(deviceObject["kind"])
        XCTAssertNotNil(workspaceObject["workspaceID"])
        XCTAssertEqual(workspaceObject["workspaceName"] as? String, "Main")
        XCTAssertNil(workspaceObject["name"])
    }

    func testRemoteTabDecodeReappliesIncognitoBoundary() throws {
        let device = DeviceID()
        let version = makeVersion(device: device, milliseconds: 11)
        let tab = try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac mini",
            sessionID: DeviceSessionID(),
            title: "Ahoi",
            url: "https://example.test",
            lastActiveAt: version.modifiedAt,
            version: version
        )
        var object = try XCTUnwrap(
            try JSONSerialization.jsonObject(
                with: JSONEncoder().encode(tab)
            ) as? [String: Any]
        )
        object["context"] = BrowserContextKind.incognito.rawValue

        let encoded = try JSONSerialization.data(withJSONObject: object)
        XCTAssertThrowsError(try JSONDecoder().decode(RemoteTab.self, from: encoded)) { error in
            XCTAssertEqual(error as? CompanionModelError, .incognitoNotSyncable)
        }
    }

    func testPayloadCodecSealsModelBeforeSyncEnvelope() throws {
        let device = DeviceID()
        let version = makeVersion(device: device, milliseconds: 12)
        let tab = try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: DeviceSessionID(),
            title: "Private title",
            url: "https://example.test/private",
            lastActiveAt: version.modifiedAt,
            version: version
        )
        let record = try CompanionPayloadCodec(
            sealer: SyntheticCompanionPayloadSealer()
        ).makeRecord(
            recordID: tab.tabID.rawValue,
            entityID: tab.tabID.rawValue,
            dataClass: .deviceTab,
            version: version,
            value: tab
        )

        XCTAssertEqual(record.dataClass, .deviceTab)
        XCTAssertEqual(record.encryptedValue.nonce.count, 12)
        XCTAssertGreaterThanOrEqual(record.encryptedValue.ciphertextAndTag.count, 16)
        XCTAssertNoThrow(try SyncBoundary().authorize(record))
    }

    func testDesktopRemoteTabMatchesMacGoldenPayload() throws {
        let device = DeviceID(
            rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000002")!
        )
        let clock = HybridLogicalClock(
            physicalMilliseconds: 1_000,
            logicalCounter: 2,
            nodeID: device
        )
        let version = SyncVersion(modifiedAt: clock, modifiedBy: device)
        let tab = try RemoteTab(
            tabID: TabID(
                rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000001")!
            ),
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: DeviceSessionID(
                rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000003")!
            ),
            title: "Ahoi",
            url: "https://example.test/path",
            lastActiveAt: clock,
            version: version
        )

        let payload = try DesktopWirePayloadCodec().encode(tab)
        XCTAssertEqual(
            String(decoding: payload, as: UTF8.self),
            #"{"device_id":"10000000-0000-4000-8000-000000000002","field_versions":{"device_id":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"is_incognito":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"last_active":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"opened_at":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"pinned":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"session_id":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"title":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"tombstone":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"url":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"workspace_id":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"}},"id":"10000000-0000-4000-8000-000000000001","is_incognito":false,"last_active":"11644473601000000","model_version":2,"opened_at":"11644473601000000","pinned":false,"session_id":"10000000-0000-4000-8000-000000000003","title":"Ahoi","tombstone":false,"url":"https://example.test/path","version_device":"10000000-0000-4000-8000-000000000002","version_logical":2,"version_model":2,"version_physical":"11644473601000000"}"#
        )
    }

    func testFileStoreRoundTripIsAtomicPersistenceSeam() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiMobileTests-\(UUID().uuidString)", isDirectory: true)
        let fileURL = directory.appendingPathComponent("snapshot.json")
        defer { try? FileManager.default.removeItem(at: directory) }

        let device = DeviceID()
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Persisted",
            version: makeVersion(device: device, milliseconds: 20)
        )
        let first = LocalFirstRepository(store: FileCompanionStore(fileURL: fileURL))
        try await first.upsert(workspace)

        let second = LocalFirstRepository(store: FileCompanionStore(fileURL: fileURL))
        let restored = try await second.currentSnapshot()

        XCTAssertEqual(restored.visibleWorkspaces, [workspace])
    }

    func testFileSyncRecordStoreRestoresPendingPayloadAfterRestart() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiMobileRecordTests-\(UUID().uuidString)", isDirectory: true)
        let fileURL = directory.appendingPathComponent("records.json")
        defer { try? FileManager.default.removeItem(at: directory) }

        let record = makeSyncRecord(dataClass: .deviceTab)
        let first = try FileSyncRecordStore(fileURL: fileURL)
        try await first.upsert(record)

        let second = try FileSyncRecordStore(fileURL: fileURL)
        let restored = try await second.record(for: record.recordID)
        XCTAssertEqual(restored, record)
    }

    func testFileSyncRecordStoreDurablyStagesDistinctFetchedEnvelopeBytes() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(
                "AhoiFetchedRecordTests-\(UUID().uuidString)",
                isDirectory: true
            )
        let fileURL = directory.appendingPathComponent("records.json")
        defer { try? FileManager.default.removeItem(at: directory) }

        let canonical = makeSyncRecord(dataClass: .workspace)
        let fetched = SyncRecord(
            recordID: canonical.recordID,
            entityID: canonical.entityID,
            schemaVersion: canonical.schemaVersion,
            dataClass: canonical.dataClass,
            modifiedAt: canonical.modifiedAt,
            originatingDevice: canonical.originatingDevice,
            orderKey: canonical.orderKey,
            encryptedValue: .init(
                keyVersion: 2,
                nonce: Data(repeating: 0x51, count: 12),
                ciphertextAndTag: Data(repeating: 0x52, count: 32)
            ),
            tombstone: canonical.tombstone
        )
        let first = try FileSyncRecordStore(fileURL: fileURL)
        try await first.upsert(canonical)
        try await first.stageFetchedRecord(fetched)
        try await first.stageFetchedRecord(fetched)

        let restarted = try FileSyncRecordStore(fileURL: fileURL)
        let restoredCanonical = try await restarted.record(for: canonical.recordID)
        let restoredFetched = try await restarted.fetchedRecords()
        XCTAssertEqual(restoredCanonical, canonical)
        XCTAssertEqual(restoredFetched, [fetched])

        try await restarted.acknowledgeFetchedRecord(fetched)
        let acknowledged = try FileSyncRecordStore(fileURL: fileURL)
        let acknowledgedCanonical = try await acknowledged.record(for: canonical.recordID)
        let acknowledgedFetched = try await acknowledged.fetchedRecords()
        XCTAssertEqual(acknowledgedCanonical, canonical)
        XCTAssertTrue(acknowledgedFetched.isEmpty)
    }

    func testTreeNodeSchemaRejectsInvalidPageAndFolderPayloads() throws {
        let device = DeviceID()
        let workspaceID = WorkspaceID()
        let version = makeVersion(device: device, milliseconds: 30)
        let orderKey = try OrderKey(components: [1], tieBreaker: device)

        XCTAssertThrowsError(try TreeNode(
            treeNodeID: TreeNodeID(),
            workspaceID: workspaceID,
            kind: .savedPage,
            title: "Missing URL",
            orderKey: orderKey,
            version: version
        ))
        XCTAssertThrowsError(try TreeNode(
            treeNodeID: TreeNodeID(),
            workspaceID: workspaceID,
            kind: .folder,
            title: "Folder",
            url: "https://example.test",
            orderKey: orderKey,
            version: version
        ))
    }

    func testLocalWorkspaceTreeCRUDKeepsOrderAndTombstonesDescendants() async throws {
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: DeviceID(
                rawValue: UUID(uuidString: "70000000-0000-4000-8000-000000000001")!
            )
        )
        let workspace = try await repository.createWorkspace(name: "  Projekt  ")
        let renamed = try await repository.updateWorkspace(
            workspace.id,
            name: "Projekt 2",
            icon: "🧭"
        )
        let folder = try await repository.createTreeNode(
            workspaceID: workspace.id,
            kind: .folder,
            title: "Ordner"
        )
        let sibling = try await repository.createTreeNode(
            workspaceID: workspace.id,
            kind: .savedPage,
            title: "Danach",
            url: "https://example.test/after"
        )
        let childFolder = try await repository.createTreeNode(
            workspaceID: workspace.id,
            parentID: folder.id,
            kind: .folder,
            title: "Kind"
        )
        let nestedPage = try await repository.createTreeNode(
            workspaceID: workspace.id,
            parentID: childFolder.id,
            kind: .savedPage,
            title: "Tief",
            url: "https://example.test/deep"
        )

        XCTAssertEqual(renamed.name, "Projekt 2")
        XCTAssertEqual(renamed.icon, "🧭")
        XCTAssertLessThan(folder.orderKey, sibling.orderKey)
        do {
            _ = try await repository.moveTreeNode(
                folder.id,
                to: workspace.id,
                parentID: childFolder.id
            )
            XCTFail("Expected cycle rejection")
        } catch {
            XCTAssertEqual(error as? LocalCompanionStoreError, .treeCycle)
        }

        let deleted = try await repository.deleteTreeNode(folder.id)
        XCTAssertEqual(
            Set(deleted.map(\.id)),
            Set([folder.id, childFolder.id, nestedPage.id])
        )
        XCTAssertTrue(deleted.allSatisfy(\.isDeleted))
        XCTAssertTrue(deleted.allSatisfy {
            $0.tombstone?.purgeAfterMilliseconds ==
                $0.version.modifiedAt.physicalMilliseconds + UInt64(30 * 24 * 60 * 60 * 1_000)
        })
        let snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleTreeNodes.map(\.id), [sibling.id])
    }

    func testWorkspaceFieldMergeConvergesDisjointOfflineEdits() throws {
        let firstDevice = DeviceID(
            rawValue: UUID(uuidString: "71000000-0000-4000-8000-000000000001")!
        )
        let secondDevice = DeviceID(
            rawValue: UUID(uuidString: "72000000-0000-4000-8000-000000000002")!
        )
        let created = HybridLogicalClock(
            physicalMilliseconds: 100,
            nodeID: firstDevice
        )
        let identity = WorkspaceID(
            rawValue: UUID(uuidString: "73000000-0000-4000-8000-000000000003")!
        )
        let baseVersion = SyncVersion(
            modifiedAt: created,
            modifiedBy: firstDevice
        ).normalized(for: CompanionFieldMerge.workspaceFields)
        let base = Workspace(
            workspaceID: identity,
            name: "Alt",
            icon: "circle",
            accent: "#ff000001",
            sortKey: "0001!a",
            createdAt: created,
            version: baseVersion
        )
        let localClock = HybridLogicalClock(
            physicalMilliseconds: 300,
            nodeID: firstDevice
        )
        var local = base
        local.icon = "star"
        local.version = SyncVersion(
            modifiedAt: localClock,
            modifiedBy: firstDevice,
            fieldVersions: baseVersion.fieldVersions.merging(["icon": localClock]) { _, new in new }
        )
        let remoteClock = HybridLogicalClock(
            physicalMilliseconds: 200,
            nodeID: secondDevice
        )
        var remote = base
        remote.name = "Remote"
        remote.version = SyncVersion(
            modifiedAt: remoteClock,
            modifiedBy: secondDevice,
            fieldVersions: baseVersion.fieldVersions.merging(["name": remoteClock]) { _, new in new }
        )

        let merged = try CompanionFieldMerge.merge(local, remote)

        XCTAssertEqual(merged.name, "Remote")
        XCTAssertEqual(merged.icon, "star")
        XCTAssertEqual(merged.version.fieldVersions["name"], remoteClock)
        XCTAssertEqual(merged.version.fieldVersions["icon"], localClock)
    }

    func testHistoryModelRejectsPrivateAndNonNetworkURLs() throws {
        let device = DeviceID()
        let version = makeVersion(device: device, milliseconds: 40)
        for url in [
            "file:///tmp/private",
            "data:text/plain,secret",
            "https://user:password@example.test/private",
        ] {
            XCTAssertThrowsError(try HistoryVisit(
                visitID: HistoryVisitID(),
                deviceID: device,
                title: "Private",
                url: url,
                visitedAt: version.modifiedAt,
                transition: "link",
                version: version
            )) { error in
                XCTAssertEqual(error as? CompanionModelError, .historyURLNotAllowed)
            }
        }
    }

    func testFileQuarantineMetadataSurvivesRestartWithoutPayload() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiQuarantineTests-\(UUID().uuidString)")
        let fileURL = directory.appendingPathComponent("quarantine.json")
        defer { try? FileManager.default.removeItem(at: directory) }
        let recordID = UUID()
        let first = try FileSyncQuarantineStore(fileURL: fileURL)
        try await first.quarantine(recordID: recordID, reason: "bad payload: https://secret")

        let second = try FileSyncQuarantineStore(fileURL: fileURL)
        let values = await second.allQuarantined()

        XCTAssertEqual(values[recordID], "badpayloadhttpssecret")
        XCTAssertFalse(String(decoding: try Data(contentsOf: fileURL), as: UTF8.self)
            .contains("https://secret"))
    }

    func testInMemoryBackendExercisesCoordinatorWithoutNetwork() async throws {
        let backend = InMemoryCloudSyncBackend()
        let coordinator = CloudSyncCoordinator(transport: backend)
        let record = makeSyncRecord(dataClass: .history)

        try await coordinator.prepare()
        try await coordinator.upload([record])
        let fetched = try await coordinator.fetch(recordIDs: [record.recordID])

        XCTAssertEqual(fetched, [record])
        let stored = await backend.allRecords()
        XCTAssertEqual(stored, [record])
    }

#if canImport(CloudKit)
    func testCloudKitSafetyGateSurvivesEngineStateClearAndRestart() throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiSafetyState-\(UUID().uuidString)")
        let stateURL = directory.appendingPathComponent("engine.json")
        defer { try? FileManager.default.removeItem(at: directory) }
        let expected = CloudKitSyncSafetyState(
            accountTransitionPending: true,
            zoneRecoveryPending: true
        )
        let first = FileSyncEngineStateStore(fileURL: stateURL)

        try first.saveSafetyState(expected)
        try first.clear()

        let restarted = FileSyncEngineStateStore(fileURL: stateURL)
        XCTAssertEqual(try restarted.loadSafetyState(), expected)
    }

    func testCloudKitProviderQueuesAllowedRecordWithoutNetworkRoundTrip() async throws {
        guard ProcessInfo.processInfo.environment["AHOI_CLOUDKIT_TEST_ENTITLED"] == "1" else {
            throw XCTSkip("CKSyncEngine requires an entitled Apple test target; compile-only gate here")
        }
        let recordStore = InMemorySyncRecordStore()
        let provider = try CloudKitSyncProvider(
            configuration: .init(
                containerIdentifier: "iCloud.app.ahoibrowser.development.placeholder",
                automaticallySync: false
            ),
            recordStore: recordStore,
            stateStore: InMemorySyncEngineStateStore()
        )
        let record = makeSyncRecord(dataClass: .deviceTab)

        try await provider.enqueue(record)

        XCTAssertEqual(provider.pendingRecordCount(), 1)
        let stored = try await recordStore.record(for: record.recordID)
        XCTAssertEqual(stored, record)
        XCTAssertEqual(provider.status().phase, .idle)
    }

    func testCloudKitProviderStopsSensitiveRecordBeforeOutbox() async throws {
        guard ProcessInfo.processInfo.environment["AHOI_CLOUDKIT_TEST_ENTITLED"] == "1" else {
            throw XCTSkip("CKSyncEngine requires an entitled Apple test target; compile-only gate here")
        }
        let recordStore = InMemorySyncRecordStore()
        let provider = try CloudKitSyncProvider(
            configuration: .init(
                containerIdentifier: "iCloud.app.ahoibrowser.development.placeholder",
                automaticallySync: false
            ),
            recordStore: recordStore,
            stateStore: InMemorySyncEngineStateStore()
        )

        do {
            try await provider.enqueue(makeSyncRecord(dataClass: .httpAuthSecret))
            XCTFail("Expected sensitive data class to be rejected")
        } catch let error as SyncBoundaryError {
            XCTAssertEqual(error, .dataClassDenied(.httpAuthSecret))
        }
        let records = try await recordStore.allRecords()
        XCTAssertTrue(records.isEmpty)
        XCTAssertEqual(provider.pendingRecordCount(), 0)
    }

    func testCloudKitProviderValidatesConfigurationWithoutContactingCloudKit() {
        XCTAssertThrowsError(try CloudKitSyncProvider(
            configuration: .init(containerIdentifier: "not-a-container"),
            recordStore: InMemorySyncRecordStore(),
            stateStore: InMemorySyncEngineStateStore()
        )) { error in
            XCTAssertEqual(error as? CloudKitSyncProviderError, .invalidContainerIdentifier)
        }
    }

    func testCloudKitBootstrapStaysLocalWithoutContainerSetting() {
        let provider = CompanionCloudKitBootstrap.makeProvider(
            containerIdentifier: nil,
            recordsURL: FileManager.default.temporaryDirectory.appendingPathComponent("records.json"),
            stateURL: FileManager.default.temporaryDirectory.appendingPathComponent("state.json")
        )
        XCTAssertNil(provider)
    }

    func testCloudKitBootstrapDoesNotConstructProviderBeforeOptIn() {
        let provider = CompanionCloudKitBootstrap.makeProvider(
            syncEnabled: false,
            containerIdentifier: "iCloud.example.configured",
            recordsURL: FileManager.default.temporaryDirectory
                .appendingPathComponent("disabled-records.json"),
            stateURL: FileManager.default.temporaryDirectory
                .appendingPathComponent("disabled-state.json")
        )
        XCTAssertNil(provider)
    }
#endif

    private func makeVersion(device: DeviceID, milliseconds: UInt64) -> SyncVersion {
        SyncVersion(
            modifiedAt: .init(
                physicalMilliseconds: milliseconds,
                nodeID: device
            ),
            modifiedBy: device
        )
    }

    private func makeSyncRecord(dataClass: SyncDataClass) -> SyncRecord {
        let device = DeviceID()
        return SyncRecord(
            entityID: UUID(),
            dataClass: dataClass,
            modifiedAt: .init(physicalMilliseconds: 100, nodeID: device),
            originatingDevice: device,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 3, count: 12),
                ciphertextAndTag: Data(repeating: 4, count: 32)
            )
        )
    }
}
