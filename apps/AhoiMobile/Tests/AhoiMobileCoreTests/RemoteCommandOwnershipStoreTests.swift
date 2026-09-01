import Foundation
import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

#if canImport(CryptoKit)
import CryptoKit
#endif

final class RemoteCommandOwnershipStoreTests: XCTestCase {
    func testFileStoreSurvivesRestartWithoutPersistingCommandPayload() async throws {
        let directory = temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        let fileURL = directory.appendingPathComponent("ownership.json")
        let entries = [
            RemoteCommandOwnershipEntry(
                recordID: uuid("10000000-0000-4000-8000-000000000001"),
                issuedAtMilliseconds: 1_000
            ),
            RemoteCommandOwnershipEntry(
                recordID: uuid("20000000-0000-4000-8000-000000000002"),
                issuedAtMilliseconds: 2_000
            ),
        ]
        let expected = RemoteCommandOwnershipSnapshot(
            entries: entries,
            migrationCompleted: true
        )

        let store = try FileRemoteCommandOwnershipStore(fileURL: fileURL)
        try await store.save(expected)
        let restarted = try FileRemoteCommandOwnershipStore(fileURL: fileURL)
        let restored = await restarted.load()
        let raw = String(decoding: try Data(contentsOf: fileURL), as: UTF8.self)

        XCTAssertEqual(restored, expected)
        XCTAssertFalse(raw.contains("https://"))
        XCTAssertFalse(raw.contains("nonce"))
        XCTAssertFalse(raw.contains("signature"))
        XCTAssertFalse(raw.contains("publicKey"))
    }

    func testFileStoreRejectsCorruptArchiveAtRestart() throws {
        let directory = temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        let fileURL = directory.appendingPathComponent("ownership.json")
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        try Data("{not-valid-json".utf8).write(to: fileURL, options: [.atomic])

        XCTAssertThrowsError(try FileRemoteCommandOwnershipStore(fileURL: fileURL)) {
            XCTAssertEqual(
                $0 as? RemoteCommandOwnershipStoreError,
                .invalidArchive
            )
        }
    }

    func testFailedFileSaveRollsBackInMemorySnapshot() async throws {
        let directory = temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        let blocker = directory.appendingPathComponent("not-a-directory")
        try Data("block".utf8).write(to: blocker)
        let store = try FileRemoteCommandOwnershipStore(
            fileURL: blocker.appendingPathComponent("ownership.json")
        )
        let next = RemoteCommandOwnershipSnapshot(entries: [
            .init(recordID: UUID(), issuedAtMilliseconds: 1_000),
        ])

        do {
            try await store.save(next)
            XCTFail("Expected persistence to fail")
        } catch {
            let restored = await store.load()
            XCTAssertEqual(restored, .init())
        }
    }

    func testOwnershipRetentionIsBoundedDeterministicAndIdempotent() async throws {
        let entries = (0..<1_005).map { index in
            RemoteCommandOwnershipEntry(
                recordID: deterministicUUID(index),
                issuedAtMilliseconds: UInt64(index)
            )
        }
        let snapshot = RemoteCommandOwnershipSnapshot(entries: entries)
        let store = InMemoryRemoteCommandOwnershipStore()

        await store.save(snapshot)
        await store.save(snapshot)
        let loaded = await store.load()

        XCTAssertEqual(loaded.entries.count, 1_000)
        XCTAssertEqual(loaded.entries.first?.issuedAtMilliseconds, 1_004)
        XCTAssertEqual(loaded.entries.last?.issuedAtMilliseconds, 5)
        XCTAssertEqual(Set(loaded.entries.map(\.recordID)).count, 1_000)
    }

    func testClearPersistsEmptyCompletedIndexAcrossRestart() async throws {
        let directory = temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        let fileURL = directory.appendingPathComponent("ownership.json")
        let store = try FileRemoteCommandOwnershipStore(fileURL: fileURL)
        try await store.save(.init(entries: [
            .init(recordID: UUID(), issuedAtMilliseconds: 1_000),
        ]))

        try await store.clear()
        let restarted = try FileRemoteCommandOwnershipStore(fileURL: fileURL)
        let snapshot = await restarted.load()

        XCTAssertTrue(snapshot.entries.isEmpty)
        XCTAssertTrue(snapshot.migrationCompleted)
    }

    func testExactOwnershipCannotBeStarvedByMoreThanOneThousandForeignRecords() {
        let source = DeviceID(rawValue: UUID())
        let foreign = DeviceID(rawValue: UUID())
        let ownedState = makeState(
            source: source,
            commandID: uuid("30000000-0000-4000-8000-000000000003"),
            issuedAtMilliseconds: 1
        )
        let ownedRecord = makeRecord(for: ownedState)
        let foreignStates = (0..<1_001).map { index in
            makeState(
                source: foreign,
                commandID: deterministicUUID(index + 2_000),
                issuedAtMilliseconds: UInt64(10_000 + index)
            )
        }
        let foreignRecords = foreignStates.map(makeRecord)
        let statesByID = Dictionary(uniqueKeysWithValues:
            ([ownedState] + foreignStates).map { ($0.id, $0) }
        )

        let batches = CompanionRemoteCommandRetention.hydrationBatches(
            exactRecords: [ownedRecord],
            migrationRecords: foreignRecords + [ownedRecord],
            migrationCompleted: false
        )
        let exact = CompanionRemoteCommandRetention.decodeOwnedStates(
            from: batches.exact,
            sourceDeviceID: source,
            requireOwnedSource: true,
            decode: { statesByID[$0.recordID]! },
            validateOwned: { _ in }
        )

        XCTAssertEqual(batches.exact.map(\.recordID), [ownedState.id])
        XCTAssertEqual(batches.migration.count, 1_000)
        XCTAssertEqual(exact.ownedStates.map(\.id), [ownedState.id])
        XCTAssertTrue(exact.rejectedRecords.isEmpty)
    }

    func testInvalidOwnedSignatureRestoresNoCommandStateAfterRelaunch() throws {
#if canImport(CryptoKit)
        let privateKey = Curve25519.Signing.PrivateKey()
        let source = DeviceID(rawValue: UUID())
        let unsigned = makeState(
            source: source,
            commandID: UUID(),
            issuedAtMilliseconds: 1_000,
            signature: Data(repeating: 0, count: 64)
        )
        let record = makeRecord(for: unsigned)
        let identity = RemoteControlProvisioningIdentity(
            sourceDeviceID: source,
            publicKey: privateKey.publicKey.rawRepresentation
        )

        let outcome = CompanionRemoteCommandRetention.decodeOwnedStates(
            from: [record],
            sourceDeviceID: source,
            requireOwnedSource: true,
            decode: { _ in unsigned },
            validateOwned: {
                guard try identity.verify($0.envelope) else {
                    throw RemoteCommandValidationError.invalidSignature
                }
            }
        )
        let commandStates = Dictionary(uniqueKeysWithValues:
            outcome.ownedStates.map { ($0.id, $0) }
        )

        XCTAssertTrue(commandStates.isEmpty)
        XCTAssertEqual(outcome.rejectedRecords.map(\.recordID), [record.recordID])
#else
        throw XCTSkip("CryptoKit unavailable")
#endif
    }

    func testValidOwnedSignatureSurvivesRelaunchValidation() throws {
#if canImport(CryptoKit)
        let privateKey = Curve25519.Signing.PrivateKey()
        let source = DeviceID(rawValue: UUID())
        let payload = makePayload(
            source: source,
            commandID: UUID(),
            issuedAtMilliseconds: 1_000
        )
        let state = makeState(
            payload: payload,
            signature: try privateKey.signature(for: payload.canonicalData())
        )
        let identity = RemoteControlProvisioningIdentity(
            sourceDeviceID: source,
            publicKey: privateKey.publicKey.rawRepresentation
        )

        let outcome = CompanionRemoteCommandRetention.decodeOwnedStates(
            from: [makeRecord(for: state)],
            sourceDeviceID: source,
            requireOwnedSource: true,
            decode: { _ in state },
            validateOwned: {
                guard try identity.verify($0.envelope) else {
                    throw RemoteCommandValidationError.invalidSignature
                }
            }
        )

        XCTAssertEqual(outcome.ownedStates.map(\.id), [state.id])
        XCTAssertTrue(outcome.rejectedRecords.isEmpty)
#else
        throw XCTSkip("CryptoKit unavailable")
#endif
    }

#if canImport(CloudKit)
    func testRuntimeBootstrapFailsClosedForCorruptOwnershipIndex() throws {
        let directory = temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        try Data("corrupt".utf8).write(
            to: directory.appendingPathComponent("remote-command-ownership-v1.json")
        )

        XCTAssertThrowsError(try CompanionCloudKitBootstrap.makeRuntimeChecked(
            syncEnabled: true,
            containerIdentifier: "iCloud.app.ahoibrowser.fixture",
            keyConfiguration: .init(
                service: "fixture.invalid",
                account: "sync-key",
                keyVersion: 1
            ),
            repository: LocalFirstRepository(store: InMemoryCompanionStore()),
            recordsURL: directory.appendingPathComponent("records.json"),
            stateURL: directory.appendingPathComponent("engine.json")
        )) {
            XCTAssertEqual(
                $0 as? CompanionCloudKitBootstrapError,
                .remoteCommandOwnershipStoreInitializationFailed
            )
        }
    }
#endif

    private func makeState(
        source: DeviceID,
        commandID: UUID,
        issuedAtMilliseconds: UInt64,
        signature: Data = Data(repeating: 0xAB, count: 64)
    ) -> RemoteCommandState {
        makeState(
            payload: makePayload(
                source: source,
                commandID: commandID,
                issuedAtMilliseconds: issuedAtMilliseconds
            ),
            signature: signature
        )
    }

    private func makeState(
        payload: RemoteCommandPayload,
        signature: Data
    ) -> RemoteCommandState {
        let clock = HybridLogicalClock(
            physicalMilliseconds: payload.issuedAtMilliseconds,
            nodeID: payload.sourceDeviceID
        )
        return RemoteCommandState(
            envelope: .init(payload: payload, signature: signature),
            version: .init(modifiedAt: clock, modifiedBy: payload.sourceDeviceID)
        )
    }

    private func makePayload(
        source: DeviceID,
        commandID: UUID,
        issuedAtMilliseconds: UInt64
    ) -> RemoteCommandPayload {
        .init(
            commandID: commandID,
            sourceDeviceID: source,
            targetDeviceID: DeviceID(rawValue: UUID()),
            nonce: Data(repeating: 0x7A, count: 32),
            issuedAtMilliseconds: issuedAtMilliseconds,
            command: .open(.init(url: "https://fixture.ahoibrowser.test"))
        )
    }

    private func makeRecord(for state: RemoteCommandState) -> SyncRecord {
        SyncRecord(
            recordID: state.id,
            entityID: state.id,
            schemaVersion: state.version.schemaVersion,
            dataClass: .remoteCommand,
            modifiedAt: state.version.modifiedAt,
            originatingDevice: state.version.modifiedBy,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 0xA1, count: 12),
                ciphertextAndTag: Data(repeating: 0xB2, count: 32)
            )
        )
    }

    private func temporaryDirectory() -> URL {
        FileManager.default.temporaryDirectory
            .appendingPathComponent("RemoteCommandOwnership-\(UUID().uuidString)")
    }

    private func uuid(_ value: String) -> UUID {
        UUID(uuidString: value)!
    }

    private func deterministicUUID(_ index: Int) -> UUID {
        UUID(uuidString: String(
            format: "00000000-0000-4000-8000-%012llx",
            UInt64(index)
        ))!
    }
}
