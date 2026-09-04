import CloudKit
import CryptoKit
import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

@available(iOS 17.0, *)
final class AhoiMobileCloudKitE2ETests: XCTestCase {
    typealias Harness = AhoiMobileCloudKitE2EHarness

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
            isTombstone: expected.tombstone != nil,
            plaintext: expectedPlaintext
        )
    }

    private func assertServerPrivacy(
        _ record: CKRecord,
        isTombstone: Bool,
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
        if isTombstone {
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
