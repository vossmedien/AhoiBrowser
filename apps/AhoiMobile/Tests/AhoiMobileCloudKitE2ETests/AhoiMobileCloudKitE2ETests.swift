import CloudKit
import CryptoKit
import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

@available(iOS 17.0, *)
final class AhoiMobileCloudKitE2ETests: XCTestCase {
    typealias Harness = AhoiMobileCloudKitE2EHarness

    /// Two independent domain repositories and CKSyncEngines exchange actual
    /// Workspace/TreeNode/Device/Session/Tab payloads through the real server.
    /// Both logical devices run the Mobile bridge in this signed iPhone host;
    /// this does not impersonate execution of the Chromium/macOS client.
    func testRealContainerTwoLogicalDevicesMergePagesTabsAndDeletion() async throws {
        executionTimeAllowance = 240
        let contract = try Harness.validateSignedHost(requireRealMutation: true)
        let scope = try XCTUnwrap(contract.runScope)
        let database = CKContainer(identifier: contract.containerIdentifier)
            .privateCloudDatabase
        let key = SymmetricKey(size: .bits256)
        let cleanup = AhoiMobileCloudKitE2ECleanupOwner(
            containerIdentifier: contract.containerIdentifier, scope: scope, key: key
        )
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiDomainE2E-\(scope.token.uuidString)", isDirectory: true
        )
        addTeardownBlock {
            try await cleanup.cleanup()
            if FileManager.default.fileExists(atPath: directory.path) {
                try FileManager.default.removeItem(at: directory)
            }
        }

        let mac = try await makeDomainPeer(
            contract: contract, scope: scope, key: key, cleanup: cleanup,
            directory: directory.appendingPathComponent("logical-mac"),
            deviceID: scope.deviceID, createsSubscription: true
        )
        let owner = try Harness.makeRecord(
            recordID: scope.ownerRecordID, entityID: scope.ownerRecordID,
            dataClass: .recoveryMetadata, deviceID: scope.deviceID,
            clock: HybridLogicalClock(
                physicalMilliseconds: UInt64(Date().timeIntervalSince1970 * 1_000),
                nodeID: scope.deviceID
            ),
            plaintext: scope.ownerPlaintext, key: key
        )
        // The same authenticated marker-first cleanup as the transport smoke
        // protects every server mutation; only this fresh UUID zone is used.
        try await replaceCloudRecord(owner, using: mac.provider)
        let phone = try await makeDomainPeer(
            contract: contract, scope: scope, key: key, cleanup: cleanup,
            directory: directory.appendingPathComponent("logical-iphone"),
            deviceID: DeviceID()
        )

        let workspace = try await mac.repository.createWorkspace(name: "Domain E2E")
        let folder = try await mac.repository.createTreeNode(
            workspaceID: workspace.id, kind: .folder, title: "Shared folder"
        )
        let initialURL = "https://sync-fixture.ahoibrowser.invalid/\(scope.token)/initial"
        let page = try await mac.repository.createTreeNode(
            workspaceID: workspace.id, parentID: folder.id, kind: .savedPage,
            title: "Created on logical Mac", url: initialURL
        )
        let macTab = try await mac.repository.publishLocalMobileTab(
            tabID: UUID(), sessionID: DeviceSessionID(), deviceName: "Logical Mac",
            deviceKind: .mac, workspaceID: workspace.id, title: "Mac normal tab",
            url: initialURL, pinned: false
        )
        try await mac.bridge.enqueueLocalSnapshot()
        try await mac.bridge.syncNow()
        try await phone.bridge.syncNow()
        let received = try await phone.repository.currentSnapshot()
        XCTAssertEqual(received.visibleWorkspaces.map(\.id), [workspace.id])
        XCTAssertEqual(received.visibleTreeNodes.first { $0.id == page.id }?.parentID,
                       folder.id)
        XCTAssertEqual(received.visibleTreeNodes.first { $0.id == page.id }?.url,
                       initialURL)
        XCTAssertTrue(received.visibleRemoteTabs.contains { $0.id == macTab.tab.id })

        _ = try await phone.repository.updateTreeNode(page.id, title: "Edited on iPhone")
        let phoneTab = try await phone.repository.publishLocalMobileTab(
            tabID: UUID(), sessionID: DeviceSessionID(), deviceName: "Logical iPhone",
            deviceKind: .iPhone, workspaceID: workspace.id, title: "iPhone normal tab",
            url: initialURL, pinned: false
        )
        try await phone.bridge.enqueueLocalSnapshot()
        try await phone.bridge.syncNow()
        try await mac.bridge.syncNow()
        let returned = try await mac.repository.currentSnapshot()
        XCTAssertEqual(returned.visibleTreeNodes.first { $0.id == page.id }?.title,
                       "Edited on iPhone")
        XCTAssertTrue(returned.visibleRemoteTabs.contains { $0.id == phoneTab.tab.id })

        // Both peers edit their independent durable repositories before either
        // contacts the server. Field merge must preserve both changes.
        let changedURL = "https://sync-fixture.ahoibrowser.invalid/\(scope.token)/offline"
        _ = try await mac.repository.updateTreeNode(page.id, title: "Mac offline rename")
        _ = try await phone.repository.updateTreeNode(page.id, url: .some(changedURL))
        try await mac.bridge.enqueueLocalSnapshot()
        try await phone.bridge.enqueueLocalSnapshot()
        try await mac.bridge.syncNow()
        try await phone.bridge.syncNow()
        try await mac.bridge.syncNow()
        let macMerged = try await mac.repository.currentSnapshot()
        let phoneMerged = try await phone.repository.currentSnapshot()
        let macPage = try XCTUnwrap(macMerged.visibleTreeNodes.first { $0.id == page.id })
        let phonePage = try XCTUnwrap(phoneMerged.visibleTreeNodes.first { $0.id == page.id })
        XCTAssertEqual(macPage, phonePage)
        XCTAssertEqual(macPage.title, "Mac offline rename")
        XCTAssertEqual(macPage.url, changedURL)

        let savedOnDisk = try await FileCompanionStore(
            fileURL: directory.appendingPathComponent("logical-iphone/domain.json")
        ).load()
        XCTAssertEqual(savedOnDisk.visibleTreeNodes.first { $0.id == page.id }, phonePage)
        try await assertDomainServerPrivacy(
            recordIDs: [workspace.id.rawValue, folder.id.rawValue, page.id.rawValue,
                        macTab.tab.id.rawValue, phoneTab.tab.id.rawValue],
            database: database, scope: scope, key: key
        )
        try await assertPrivateRecordNeverLeavesPeer(
            phone, database: database, scope: scope, key: key
        )

        let deleted = try await phone.repository.deleteTreeNode(page.id)
        for node in deleted { try await phone.bridge.enqueue(node) }
        try await phone.bridge.syncNow()
        // Queue the retained live page on the still-stale peer before fetching
        // the server tombstone. Merge-before-send must prevent resurrection.
        try await mac.bridge.enqueue(macPage)
        XCTAssertGreaterThan(mac.provider.pendingRecordCount(), 0)
        try await mac.bridge.syncNow()
        // A second publication of the complete local state may not resurrect
        // a deleted saved page or create duplicate device tabs.
        try await mac.bridge.enqueueLocalSnapshot()
        try await mac.bridge.syncNow()
        try await phone.bridge.syncNow()
        for peer in [mac, phone] {
            let snapshot = try await peer.repository.currentSnapshot()
            XCTAssertFalse(snapshot.visibleTreeNodes.contains { $0.id == page.id })
            XCTAssertTrue(snapshot.treeNodes.first { $0.id == page.id }?.isDeleted == true)
            XCTAssertEqual(Set(snapshot.visibleRemoteTabs.map(\.id)),
                           Set([macTab.tab.id, phoneTab.tab.id]))
            XCTAssertEqual(peer.provider.pendingRecordCount(), 0)
            XCTAssertEqual(peer.provider.status().phase, .idle)
        }
        try await assertDomainServerPrivacy(
            recordIDs: [page.id.rawValue], database: database, scope: scope, key: key
        )
        try await cleanup.cleanup()
    }

    private struct DomainPeer {
        let repository: LocalFirstRepository
        let provider: CloudKitSyncProvider
        let bridge: CompanionSyncBridge
    }

    private func makeDomainPeer(
        contract: Harness.HostContract,
        scope: Harness.RunScope,
        key: SymmetricKey,
        cleanup: AhoiMobileCloudKitE2ECleanupOwner,
        directory: URL,
        deviceID: DeviceID,
        createsSubscription: Bool = false
    ) async throws -> DomainPeer {
        let provider = try Harness.makeProvider(
            contract: contract, scope: scope, createsSubscription: createsSubscription,
            recordStore: FileSyncRecordStore(
                fileURL: directory.appendingPathComponent("encrypted-records.json")
            )
        )
        cleanup.register(provider)
        do {
            try await provider.prepare()
        } catch CloudKitSyncProviderError.accountTransitionRequiresConfirmation {
            // Only the already authorized, fresh synthetic test scope is seeded.
            try await provider.confirmAccountTransition(allowLocalUpload: true)
        }
        let repository = LocalFirstRepository(
            store: FileCompanionStore(fileURL: directory.appendingPathComponent("domain.json")),
            localDeviceID: deviceID
        )
        let keyData = key.withUnsafeBytes { Data($0) }
        let sealer = KeychainCompanionPayloadSealer(
            configuration: .init(service: "app.ahoibrowser.domain-e2e",
                                 account: scope.token.uuidString, keyVersion: 1),
            keyLoader: { keyData }
        )
        return DomainPeer(
            repository: repository, provider: provider,
            bridge: CompanionSyncBridge(repository: repository, provider: provider,
                                        sealer: sealer)
        )
    }

    private func assertDomainServerPrivacy(
        recordIDs: [UUID], database: CKDatabase,
        scope: Harness.RunScope, key: SymmetricKey
    ) async throws {
        for id in recordIDs {
            let record = try await Harness.fetchServerRecord(
                database: database,
                recordID: CKRecord.ID(recordName: id.uuidString.lowercased(), zoneID: scope.zoneID)
            )
            let decoded = try AppleCloudKitRecordCodec().decode(record)
            assertServerPrivacy(record, expected: decoded,
                                plaintext: try Harness.open(decoded.encryptedValue, using: key))
        }
    }

    private func assertPrivateRecordNeverLeavesPeer(
        _ peer: DomainPeer, database: CKDatabase,
        scope: Harness.RunScope, key: SymmetricKey
    ) async throws {
        let id = UUID()
        let before = try await peer.provider.allRecords()
        let record = try Harness.makeRecord(
            recordID: id, entityID: id, dataClass: .incognito,
            deviceID: scope.deviceID,
            clock: HybridLogicalClock(physicalMilliseconds: 1, nodeID: scope.deviceID),
            plaintext: Data("private-domain-sentinel".utf8), key: key
        )
        do {
            try await peer.provider.enqueue(record)
            XCTFail("Private data must be rejected before entering the outbox.")
        } catch let error as SyncBoundaryError {
            XCTAssertEqual(error, .dataClassDenied(.incognito))
        }
        let after = try await peer.provider.allRecords()
        XCTAssertEqual(Set(before.map(\.recordID)), Set(after.map(\.recordID)))
        do {
            _ = try await Harness.fetchServerRecord(
                database: database,
                recordID: CKRecord.ID(recordName: id.uuidString.lowercased(), zoneID: scope.zoneID)
            )
            XCTFail("The private sentinel must not exist on the actual server.")
        } catch let error as CKError {
            XCTAssertEqual(error.code, .unknownItem)
        }
    }

    /// This is a same-process, same-account transport smoke against the real
    /// Development container. It does not claim a second-device domain merge;
    /// that remains a separate CompanionSyncBridge device journey.
    func testRealContainerSameAccountTransportSmokeWithTombstone() async throws {
        executionTimeAllowance = 180
        let contract = try Harness.validateSignedHost(requireRealMutation: true)
        let scope = try XCTUnwrap(contract.runScope)

        // Every signed-host and explicit-mutation gate above runs before this
        // first CKContainer construction.
        let container = CKContainer(identifier: contract.containerIdentifier)
        let accountStatus = try await container.accountStatus()
        guard accountStatus == .available else {
            throw XCTSkip(
                "The signed real-container smoke requires an available iCloud account."
            )
        }
        let accountID = try await container.userRecordID()
        guard !accountID.recordName.isEmpty else {
            throw Harness.HarnessError.missingServerRecord("current-user")
        }

        let key = SymmetricKey(size: .bits256)
        let cleanup = AhoiMobileCloudKitE2ECleanupOwner(
            containerIdentifier: contract.containerIdentifier,
            scope: scope,
            key: key
        )
        addTeardownBlock {
            try await cleanup.cleanup()
        }
        do {
            try await runTransportSmoke(
                contract: contract,
                scope: scope,
                key: key,
                cleanup: cleanup,
                database: container.privateCloudDatabase
            )
            try await cleanup.cleanup()
        } catch {
            let primaryError = error
            do {
                try await cleanup.cleanup()
            } catch {
                XCTFail("Scoped CloudKit E2E cleanup failed safely: \(error)")
            }
            throw primaryError
        }
    }

    /// Hosted equivalent of the old entitlement-env unit seam. No account
    /// lookup, fetch, send, subscription, or zone mutation occurs here.
    func testHostedProviderQueuesAllowedRecordBeforeAccountPreparation() async throws {
        let contract = try Harness.validateSignedHost(requireRealMutation: false)
        let scope = try Harness.RunScope(token: UUID())
        let recordStore = InMemorySyncRecordStore()
        let provider = try Harness.makeProvider(
            contract: contract,
            scope: scope,
            recordStore: recordStore
        )
        let key = SymmetricKey(size: .bits256)
        let record = try Harness.makeRecord(
            recordID: scope.payloadRecordID,
            entityID: scope.payloadRecordID,
            dataClass: .deviceTab,
            deviceID: scope.deviceID,
            clock: HybridLogicalClock(
                physicalMilliseconds: 1,
                nodeID: scope.deviceID
            ),
            plaintext: Data("queued-before-account-ready".utf8),
            key: key
        )
        do {
            try await provider.enqueue(record)
            XCTAssertEqual(provider.pendingRecordCount(), 1)
            let storedRecord = try await recordStore.record(for: record.recordID)
            XCTAssertEqual(
                storedRecord,
                record
            )
            XCTAssertEqual(provider.status().phase, .idle)
            await provider.cancel()
        } catch {
            await provider.cancel()
            throw error
        }
    }

    /// Hosted real-container configuration with a forbidden data class proves
    /// the outbound boundary rejects it before it reaches the local outbox.
    func testHostedProviderDeniesSensitiveRecordBeforeAccountPreparation() async throws {
        let contract = try Harness.validateSignedHost(requireRealMutation: false)
        let scope = try Harness.RunScope(token: UUID())
        let recordStore = InMemorySyncRecordStore()
        let provider = try Harness.makeProvider(
            contract: contract,
            scope: scope,
            recordStore: recordStore
        )
        let record = try Harness.makeRecord(
            recordID: scope.payloadRecordID,
            entityID: scope.payloadRecordID,
            dataClass: .httpAuthSecret,
            deviceID: scope.deviceID,
            clock: HybridLogicalClock(
                physicalMilliseconds: 1,
                nodeID: scope.deviceID
            ),
            plaintext: Data("must-never-reach-cloudkit".utf8),
            key: SymmetricKey(size: .bits256)
        )
        do {
            do {
                try await provider.enqueue(record)
                XCTFail("Expected the sensitive data class to be rejected.")
            } catch let error as SyncBoundaryError {
                XCTAssertEqual(error, .dataClassDenied(.httpAuthSecret))
            }
            XCTAssertEqual(provider.pendingRecordCount(), 0)
            let storedRecords = try await recordStore.allRecords()
            XCTAssertTrue(storedRecords.isEmpty)
            await provider.cancel()
        } catch {
            await provider.cancel()
            throw error
        }
    }

    private func runTransportSmoke(
        contract: Harness.HostContract,
        scope: Harness.RunScope,
        key: SymmetricKey,
        cleanup: AhoiMobileCloudKitE2ECleanupOwner,
        database: CKDatabase
    ) async throws {
        let writer = try Harness.makeProvider(
            contract: contract,
            scope: scope,
            createsSubscription: true
        )
        cleanup.register(writer)
        do {
            try await writer.prepare()
        } catch CloudKitSyncProviderError.accountTransitionRequiresConfirmation {
            // The provider deliberately treats the first observed account as
            // an upload boundary. This synthetic scope has no prior local
            // payload, so the E2E host explicitly approves that transition.
            try await writer.confirmAccountTransition(allowLocalUpload: true)
        }
        XCTAssertFalse(writer.safetyState().accountTransitionPending)

        let now = UInt64(Date().timeIntervalSince1970 * 1_000)
        let ownerClock = HybridLogicalClock(
            physicalMilliseconds: now,
            nodeID: scope.deviceID
        )
        let ownerRecord = try Harness.makeRecord(
            recordID: scope.ownerRecordID,
            entityID: scope.ownerRecordID,
            dataClass: .recoveryMetadata,
            deviceID: scope.deviceID,
            clock: ownerClock,
            plaintext: scope.ownerPlaintext,
            key: key
        )
        try await replaceCloudRecord(ownerRecord, using: writer)

        let activeClock = try ownerClock.ticking(at: now + 1)
        let activePlaintext = Data(
            "ahoi-cloudkit-e2e-active:\(scope.token.uuidString.lowercased())".utf8
        )
        let activeRecord = try Harness.makeRecord(
            recordID: scope.payloadRecordID,
            entityID: scope.payloadRecordID,
            dataClass: .permittedSetting,
            deviceID: scope.deviceID,
            clock: activeClock,
            plaintext: activePlaintext,
            key: key
        )
        try await replaceCloudRecord(activeRecord, using: writer)
        try await verifyReadback(
            expected: activeRecord,
            expectedPlaintext: activePlaintext,
            contract: contract,
            scope: scope,
            key: key,
            cleanup: cleanup,
            database: database
        )

        let deletedClock = try activeClock.ticking(at: now + 2)
        let tombstone = Tombstone(
            entityID: scope.payloadRecordID,
            deletedAt: deletedClock,
            deletedBy: scope.deviceID,
            originalParentID: nil,
            originalOrderKey: nil,
            purgeAfterMilliseconds: now + 86_400_000
        )
        let tombstonePlaintext = Data(
            "ahoi-cloudkit-e2e-tombstone:\(scope.token.uuidString.lowercased())".utf8
        )
        let tombstoneRecord = try Harness.makeRecord(
            recordID: scope.payloadRecordID,
            entityID: scope.payloadRecordID,
            dataClass: .permittedSetting,
            deviceID: scope.deviceID,
            clock: deletedClock,
            plaintext: tombstonePlaintext,
            key: key,
            tombstone: tombstone
        )
        try await replaceCloudRecord(tombstoneRecord, using: writer)
        try await verifyReadback(
            expected: tombstoneRecord,
            expectedPlaintext: tombstonePlaintext,
            contract: contract,
            scope: scope,
            key: key,
            cleanup: cleanup,
            database: database
        )
    }

    private func replaceCloudRecord(
        _ record: SyncRecord,
        using provider: CloudKitSyncProvider
    ) async throws {
        let passID = try await provider.fetchChanges()
        let fetched = try await provider.pendingFetchedRecords()
        try await provider.acknowledgeFetchedRecords(fetched)
        try await provider.enqueue(record)
        try await provider.sendPendingChanges(passID: passID)
        try await provider.finalizeBoundedSync(passID: passID)
        XCTAssertEqual(provider.pendingRecordCount(), 0)
    }

    private func verifyReadback(
        expected: SyncRecord,
        expectedPlaintext: Data,
        contract: Harness.HostContract,
        scope: Harness.RunScope,
        key: SymmetricKey,
        cleanup: AhoiMobileCloudKitE2ECleanupOwner,
        database: CKDatabase
    ) async throws {
        let reader = try Harness.makeProvider(contract: contract, scope: scope)
        cleanup.register(reader)
        let fetchedRecord: SyncRecord
        do {
            try await reader.prepare()
            let passID = try await reader.fetchChanges()
            let fetched = try await reader.pendingFetchedRecords()
            fetchedRecord = try XCTUnwrap(
                fetched.first { $0.recordID == expected.recordID }
            )
            try await reader.acknowledgeFetchedRecords(fetched)
            try await reader.finalizeBoundedSync(passID: passID)
            await cleanup.stop(reader)
        } catch {
            await cleanup.stop(reader)
            throw error
        }
        XCTAssertEqual(fetchedRecord, expected)
        XCTAssertEqual(
            try Harness.open(fetchedRecord.encryptedValue, using: key),
            expectedPlaintext
        )

        let cloudID = CKRecord.ID(
            recordName: expected.recordID.uuidString.lowercased(),
            zoneID: scope.zoneID
        )
        let serverRecord = try await Harness.fetchServerRecord(
            database: database,
            recordID: cloudID
        )
        let decodedServerRecord = try AppleCloudKitRecordCodec().decode(serverRecord)
        XCTAssertEqual(decodedServerRecord, expected)
        XCTAssertEqual(
            try Harness.open(decodedServerRecord.encryptedValue, using: key),
            expectedPlaintext
        )
        assertServerPrivacy(
            serverRecord,
            expected: expected,
            plaintext: expectedPlaintext
        )
    }

    private func assertServerPrivacy(
        _ record: CKRecord,
        expected: SyncRecord,
        plaintext: Data,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let fields = AppleCloudKitRecordCodec.Fields.self
        var expectedClearKeys: Set<String> = [
            fields.entityID,
            fields.schemaVersion,
            fields.dataClass,
            fields.hlcPhysical,
            fields.hlcSubmillisecond,
            fields.hlcLogical,
            fields.hlcNodeID,
            fields.originatingDeviceID,
            fields.isTombstone,
        ]
        if expected.orderKey != nil {
            expectedClearKeys.formUnion([
                fields.orderComponents, fields.orderTieBreaker, fields.orderSortKey,
            ])
        }
        if expected.tombstone != nil {
            expectedClearKeys.formUnion([
                fields.tombstoneEntityID,
                fields.tombstoneDeletedPhysical,
                fields.tombstoneDeletedSubmillisecond,
                fields.tombstoneDeletedLogical,
                fields.tombstoneDeletedNodeID,
                fields.tombstoneDeletedBy,
                fields.tombstonePurgeAfter,
            ])
        }
        if expected.tombstone?.originalParentID != nil {
            expectedClearKeys.insert(fields.tombstoneOriginalParentID)
        }
        if expected.tombstone?.originalOrderKey != nil {
            expectedClearKeys.formUnion([
                fields.tombstoneOriginalOrderComponents,
                fields.tombstoneOriginalOrderTieBreaker,
                fields.tombstoneOriginalOrderSortKey,
            ])
        }
        let encryptedKeys = Set(record.encryptedValues.allKeys())
        let clearKeys = Set(record.allKeys()).subtracting(encryptedKeys)
        XCTAssertEqual(clearKeys, expectedClearKeys, file: file, line: line)
        XCTAssertEqual(
            encryptedKeys,
            Set([fields.encryptedValue]),
            file: file,
            line: line
        )
        XCTAssertNil(record[fields.encryptedValue], file: file, line: line)

        let forbiddenKeys: Set<String> = [
            "url", "title", "username", "password", "cookie", "siteData",
            "httpAuthSecret", "headerSecret", "plaintext", "debug", "payload",
        ]
        XCTAssertTrue(
            clearKeys.isDisjoint(with: forbiddenKeys),
            file: file,
            line: line
        )
        if let encryptedEnvelope =
            record.encryptedValues[fields.encryptedValue] as? Data {
            XCTAssertFalse(
                encryptedEnvelope.containsSubsequence(plaintext),
                file: file,
                line: line
            )
        } else {
            XCTFail("Missing encrypted CloudKit envelope.", file: file, line: line)
        }
    }
}

private extension Data {
    func containsSubsequence(_ candidate: Data) -> Bool {
        guard !candidate.isEmpty, candidate.count <= count else { return false }
        return range(of: candidate) != nil
    }
}
