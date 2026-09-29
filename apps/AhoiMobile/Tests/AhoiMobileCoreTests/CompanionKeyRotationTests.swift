import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

#if canImport(CryptoKit)
import CryptoKit
#endif

final class CompanionKeyRotationTests: XCTestCase {
    func testRotationRejectsVersionRollbackBeforeAnyMutation() async throws {
        let journal = FixtureRotationJournalStore()
        let keys = FixtureRotationKeyStore(versions: [2])
        let remote = FixtureRotationRemote(mode: .matching)
        let coordinator = makeCoordinator(
            journal: journal,
            keys: keys,
            records: InMemorySyncRecordStore(),
            remote: remote
        )

        do {
            _ = try await coordinator.begin(
                currentVersion: 2,
                nextVersion: 1,
                transitionEndsAt: Date(timeIntervalSince1970: 2_000)
            )
            XCTFail("A key-version rollback must be rejected")
        } catch {
            XCTAssertEqual(error as? CompanionKeyRotationError, .invalidVersions)
        }
        let storedPlan = await journal.loadRotationPlan()
        let versions = await keys.versionsSnapshot()
        let publishCount = await remote.publishCount()
        XCTAssertNil(storedPlan)
        XCTAssertEqual(versions, Set<UInt32>([2]))
        XCTAssertEqual(publishCount, 0)
    }

    func testRotationResealsNormalAndTombstoneThenRevokesPreviousKey() async throws {
        let normal = makeRecord(keyVersion: 1, byte: 0x11)
        let tombstone = makeRecord(keyVersion: 1, byte: 0x22, deleted: true)
        let recordStore = InMemorySyncRecordStore(records: [normal, tombstone])
        let journal = FixtureRotationJournalStore()
        let keys = FixtureRotationKeyStore(versions: [1])
        let remote = FixtureRotationRemote(mode: .matching)
        let coordinator = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )

        let plan = try await coordinator.begin(
            currentVersion: 1,
            nextVersion: 2,
            transitionEndsAt: Date(timeIntervalSince1970: 2_000)
        )

        XCTAssertEqual(plan.stage, .completed)
        XCTAssertTrue(plan.permitsEncryptedDomainRecords)
        XCTAssertEqual(plan.lifecycleStatus, .ready(keyVersion: 2))
        XCTAssertNotNil(plan.acknowledgement)
        let versions = await keys.versionsSnapshot()
        let publishCount = await remote.publishCount()
        XCTAssertEqual(versions, Set<UInt32>([2]))
        XCTAssertEqual(publishCount, 1)
        let records = try await recordStore.allRecords()
        XCTAssertEqual(
            Set(records.map(\.encryptedValue.keyVersion)),
            Set<UInt32>([2])
        )
        XCTAssertEqual(
            records.first(where: { $0.recordID == tombstone.recordID })?.tombstone,
            tombstone.tombstone
        )
    }

    func testRemoteFailureKeepsPreviousKeyAndResumeDoesNotResealAgain() async throws {
        let original = makeRecord(keyVersion: 1, byte: 0x33)
        let recordStore = InMemorySyncRecordStore(records: [original])
        let journal = FixtureRotationJournalStore()
        let keys = FixtureRotationKeyStore(versions: [1])
        let remote = FixtureRotationRemote(mode: .failOnce)
        let coordinator = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )

        do {
            _ = try await coordinator.begin(
                currentVersion: 1,
                nextVersion: 2,
                transitionEndsAt: Date(timeIntervalSince1970: 2_000)
            )
            XCTFail("The unavailable readback must fail closed")
        } catch {
            XCTAssertEqual(error as? FixtureRotationError, .remoteUnavailable)
        }

        let interruptedVersions = await keys.versionsSnapshot()
        let interruptedPlan = await journal.loadRotationPlan()
        let interruptedRecord = try await recordStore.record(
            for: original.recordID
        )
        XCTAssertEqual(interruptedVersions, Set<UInt32>([1, 2]))
        XCTAssertEqual(interruptedPlan?.stage, .awaitingRemoteAcknowledgement)
        let bytesBeforeResume = try XCTUnwrap(interruptedRecord).encryptedValue

        let resumed = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )
        let completed = try await resumed.resume()

        XCTAssertEqual(completed.stage, .completed)
        let completedVersions = await keys.versionsSnapshot()
        let publishCount = await remote.publishCount()
        let resumedRecord = try await recordStore.record(for: original.recordID)
        XCTAssertEqual(completedVersions, Set<UInt32>([2]))
        XCTAssertEqual(publishCount, 2)
        XCTAssertEqual(resumedRecord?.encryptedValue, bytesBeforeResume)
    }

    func testCrashAfterRecordWriteResumesFromReadbackWithoutCiphertextChurn() async throws {
        let original = makeRecord(keyVersion: 1, byte: 0x44)
        let recordStore = InMemorySyncRecordStore(records: [original])
        // Save 4 is the progress write after the replacement record itself has
        // already been durably merged and read back.
        let journal = FixtureRotationJournalStore(failOnceOnSave: 4)
        let keys = FixtureRotationKeyStore(versions: [1])
        let remote = FixtureRotationRemote(mode: .matching)
        let coordinator = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )

        do {
            _ = try await coordinator.begin(
                currentVersion: 1,
                nextVersion: 2,
                transitionEndsAt: Date(timeIntervalSince1970: 2_000)
            )
            XCTFail("Injected journal interruption must escape")
        } catch {
            XCTAssertEqual(error as? FixtureRotationError, .journalUnavailable)
        }
        let interruptedRecord = try await recordStore.record(
            for: original.recordID
        )
        let replacement = try XCTUnwrap(interruptedRecord).encryptedValue
        XCTAssertEqual(replacement.keyVersion, 2)
        let interruptedVersions = await keys.versionsSnapshot()
        XCTAssertEqual(interruptedVersions, Set<UInt32>([1, 2]))

        let resumed = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )
        let completed = try await resumed.resume()
        let resumedRecord = try await recordStore.record(for: original.recordID)
        let generatorCalls = await keys.generatorCalls()
        XCTAssertEqual(completed.stage, .completed)
        XCTAssertEqual(resumedRecord?.encryptedValue, replacement)
        XCTAssertEqual(generatorCalls, 1)
    }

    func testIncompleteRemoteReadbackNeverRevokesPreviousKey() async throws {
        let first = makeRecord(keyVersion: 1, byte: 0x55)
        let second = makeRecord(keyVersion: 1, byte: 0x66, deleted: true)
        let recordStore = InMemorySyncRecordStore(records: [first, second])
        let journal = FixtureRotationJournalStore()
        let keys = FixtureRotationKeyStore(versions: [1])
        let remote = FixtureRotationRemote(mode: .omitLastRecord)
        let coordinator = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )

        do {
            _ = try await coordinator.begin(
                currentVersion: 1,
                nextVersion: 2,
                transitionEndsAt: Date(timeIntervalSince1970: 2_000)
            )
            XCTFail("An incomplete remote readback must not acknowledge")
        } catch {
            XCTAssertEqual(
                error as? CompanionKeyRotationError,
                .remoteReadbackRecordsMismatch
            )
        }

        let versions = await keys.versionsSnapshot()
        let removalCalls = await keys.removalCalls()
        let storedPlan = await journal.loadRotationPlan()
        XCTAssertEqual(versions, Set<UInt32>([1, 2]))
        XCTAssertEqual(removalCalls, [])
        XCTAssertNil(storedPlan?.acknowledgement)
    }

    func testExpiredUnacknowledgedPlanKeepsBothKeys() async throws {
        let record = makeRecord(keyVersion: 2, byte: 0x77)
        var plan = CompanionKeyRotationPlan(
            currentVersion: 1,
            nextVersion: 2,
            startedAt: Date(timeIntervalSince1970: 10),
            transitionEndsAt: Date(timeIntervalSince1970: 20)
        )
        plan.mergeScheduledRecordIDs([record.recordID])
        plan.markResealed(record.recordID)
        plan.setStage(.awaitingRemoteAcknowledgement)
        let journal = FixtureRotationJournalStore(plan: plan)
        let keys = FixtureRotationKeyStore(versions: [1, 2])
        let recordStore = InMemorySyncRecordStore(records: [record])
        let remote = FixtureRotationRemote(mode: .matching)
        let coordinator = CompanionKeyRotationCoordinator(
            journalStore: journal,
            keyStore: keys,
            recordStore: recordStore,
            sealer: FixtureRotationSealer(),
            remote: remote,
            generator: { Data(repeating: 0x99, count: 32) },
            now: { Date(timeIntervalSince1970: 21) }
        )

        do {
            _ = try await coordinator.resume()
            XCTFail("An expired transition without acknowledgement must stop")
        } catch {
            XCTAssertEqual(error as? CompanionKeyRotationError, .transitionExpired)
        }

        let versions = await keys.versionsSnapshot()
        let publishCount = await remote.publishCount()
        XCTAssertEqual(versions, Set<UInt32>([1, 2]))
        XCTAssertEqual(publishCount, 0)
    }

    func testUnknownEnvelopeVersionFailsBeforeRemoteOrRevocation() async throws {
        let record = makeRecord(keyVersion: 9, byte: 0x88)
        let recordStore = InMemorySyncRecordStore(records: [record])
        let journal = FixtureRotationJournalStore()
        let keys = FixtureRotationKeyStore(versions: [1])
        let remote = FixtureRotationRemote(mode: .matching)
        let coordinator = makeCoordinator(
            journal: journal,
            keys: keys,
            records: recordStore,
            remote: remote
        )

        do {
            _ = try await coordinator.begin(
                currentVersion: 1,
                nextVersion: 2,
                transitionEndsAt: Date(timeIntervalSince1970: 2_000)
            )
            XCTFail("Unknown key versions must stop rotation")
        } catch {
            XCTAssertEqual(
                error as? CompanionKeyRotationError,
                .unsupportedRecordKeyVersion(
                    recordID: record.recordID,
                    keyVersion: 9
                )
            )
        }

        let versions = await keys.versionsSnapshot()
        let publishCount = await remote.publishCount()
        let removalCalls = await keys.removalCalls()
        XCTAssertEqual(versions, Set<UInt32>([1, 2]))
        XCTAssertEqual(publishCount, 0)
        XCTAssertEqual(removalCalls, [])
    }

    func testUnknownRotationJournalFormatFailsClosed() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiRotationJournalTests-\(UUID().uuidString)",
            isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let fileURL = directory.appendingPathComponent("rotation.json")
        let store = FileCompanionKeyRotationJournalStore(fileURL: fileURL)
        let plan = CompanionKeyRotationPlan(
            currentVersion: 1,
            nextVersion: 2,
            startedAt: Date(timeIntervalSince1970: 1_000),
            transitionEndsAt: Date(timeIntervalSince1970: 2_000)
        )
        try await store.saveRotationPlan(plan)

        var object = try XCTUnwrap(
            JSONSerialization.jsonObject(with: Data(contentsOf: fileURL))
                as? [String: Any]
        )
        object["formatVersion"] = 99
        try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
            .write(to: fileURL, options: [.atomic])

        do {
            _ = try await store.loadRotationPlan()
            XCTFail("Unknown journal formats must stop rotation")
        } catch {
            XCTAssertEqual(
                error as? CompanionKeyRotationError,
                .unsupportedRotationJournalVersion(99)
            )
        }
    }

    func testKeychainRotationSealerAuthenticatesNextEnvelopeWithoutChangingIt() throws {
#if canImport(CryptoKit)
        let family = CompanionSyncKeyConfiguration(
            service: "fixture",
            account: "payload",
            keyVersion: 1
        )
        let oldKey = Data(repeating: 0x11, count: 32)
        let nextKey = Data(repeating: 0x22, count: 32)
        let oldSealer = KeychainCompanionPayloadSealer(
            configuration: family,
            keyLoader: { oldKey }
        )
        let rotation = KeychainCompanionKeyRotationSealer(
            familyAnchorConfiguration: family,
            currentVersion: 1,
            nextVersion: 2
        ) { configuration in
            configuration.keyVersion == 1 ? oldKey : nextKey
        }
        let nextSealer = KeychainCompanionPayloadSealer(
            configuration: family.canonicalConfiguration(for: 2),
            keyLoader: { nextKey }
        )
        let plaintext = Data("rotate me".utf8)

        let replacement = try rotation.reseal(
            oldSealer.seal(plaintext),
            currentVersion: 1,
            nextVersion: 2
        )
        let replayed = try rotation.reseal(
            replacement,
            currentVersion: 1,
            nextVersion: 2
        )

        XCTAssertEqual(try nextSealer.open(replacement), plaintext)
        XCTAssertEqual(replayed, replacement)
        XCTAssertEqual(
            family.canonicalConfiguration(for: 2).account,
            "payload.v2"
        )
#else
        throw XCTSkip("CryptoKit unavailable")
#endif
    }

    private func makeCoordinator(
        journal: FixtureRotationJournalStore,
        keys: FixtureRotationKeyStore,
        records: InMemorySyncRecordStore,
        remote: FixtureRotationRemote
    ) -> CompanionKeyRotationCoordinator {
        CompanionKeyRotationCoordinator(
            journalStore: journal,
            keyStore: keys,
            recordStore: records,
            sealer: FixtureRotationSealer(),
            remote: remote,
            generator: { Data(repeating: 0x99, count: 32) },
            now: { Date(timeIntervalSince1970: 1_000) }
        )
    }
}

private enum FixtureRotationError: Error, Equatable, Sendable {
    case journalUnavailable
    case remoteUnavailable
    case invalidFixtureKey
}

private actor FixtureRotationJournalStore: CompanionKeyRotationJournalStoring {
    private var plan: CompanionKeyRotationPlan?
    private var saveCount = 0
    private var failOnceOnSave: Int?

    init(
        plan: CompanionKeyRotationPlan? = nil,
        failOnceOnSave: Int? = nil
    ) {
        self.plan = plan
        self.failOnceOnSave = failOnceOnSave
    }

    func loadRotationPlan() -> CompanionKeyRotationPlan? { plan }

    func saveRotationPlan(_ plan: CompanionKeyRotationPlan) throws {
        saveCount += 1
        if saveCount == failOnceOnSave {
            failOnceOnSave = nil
            throw FixtureRotationError.journalUnavailable
        }
        self.plan = plan
    }
}

private actor FixtureRotationKeyStore: CompanionPayloadKeyRotationStoring {
    private var versions: Set<UInt32>
    private var generationCount = 0
    private var removals: [UInt32] = []

    init(versions: Set<UInt32>) {
        self.versions = versions
    }

    func hasCanonicalKey(version: UInt32) -> Bool {
        versions.contains(version)
    }

    func installCanonicalKey(
        version: UInt32,
        replacing currentVersion: UInt32,
        generator: @escaping @Sendable () throws -> Data
    ) throws {
        guard versions.contains(currentVersion), version != currentVersion else {
            throw FixtureRotationError.invalidFixtureKey
        }
        guard !versions.contains(version) else { return }
        let key = try generator()
        guard key.count == 32 else { throw FixtureRotationError.invalidFixtureKey }
        generationCount += 1
        versions.insert(version)
    }

    func removeCanonicalKey(version: UInt32) {
        removals.append(version)
        versions.remove(version)
    }

    func versionsSnapshot() -> Set<UInt32> { versions }
    func generatorCalls() -> Int { generationCount }
    func removalCalls() -> [UInt32] { removals }
}

private struct FixtureRotationSealer: CompanionKeyRotationSealing {
    func reseal(
        _ value: EncryptedValue,
        currentVersion: UInt32,
        nextVersion: UInt32
    ) throws -> EncryptedValue {
        guard value.keyVersion == currentVersion ||
                value.keyVersion == nextVersion else {
            throw CompanionKeyRotationError.unsupportedRecordKeyVersion(
                recordID: UUID(),
                keyVersion: value.keyVersion
            )
        }
        guard value.keyVersion == currentVersion else { return value }
        return EncryptedValue(
            algorithm: value.algorithm,
            keyVersion: nextVersion,
            nonce: value.nonce,
            ciphertextAndTag: value.ciphertextAndTag + Data([0xA2])
        )
    }
}

private actor FixtureRotationRemote: CompanionKeyRotationRemoteAcknowledging {
    enum Mode {
        case matching
        case failOnce
        case omitLastRecord
    }

    private var mode: Mode
    private var calls = 0

    init(mode: Mode) { self.mode = mode }

    func publishAndReadBack(
        records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) throws -> CompanionKeyRotationRemoteReadback {
        calls += 1
        switch mode {
        case .matching:
            break
        case .failOnce:
            mode = .matching
            throw FixtureRotationError.remoteUnavailable
        case .omitLastRecord:
            return CompanionKeyRotationRemoteReadback(
                planID: manifest.planID,
                keyVersion: manifest.nextVersion,
                records: Array(records.dropLast()),
                readBackAt: Date(timeIntervalSince1970: 1_100)
            )
        }
        return CompanionKeyRotationRemoteReadback(
            planID: manifest.planID,
            keyVersion: manifest.nextVersion,
            records: records,
            readBackAt: Date(timeIntervalSince1970: 1_100)
        )
    }

    func publishCount() -> Int { calls }
}

private func makeRecord(
    keyVersion: UInt32,
    byte: UInt8,
    deleted: Bool = false
) -> SyncRecord {
    let device = DeviceID()
    let entityID = UUID()
    let clock = HybridLogicalClock(
        physicalMilliseconds: 1_000,
        nodeID: device
    )
    let tombstone = deleted
        ? Tombstone(
            entityID: entityID,
            deletedAt: clock,
            deletedBy: device,
            originalParentID: nil,
            originalOrderKey: nil,
            purgeAfterMilliseconds: 2_000
        )
        : nil
    return SyncRecord(
        entityID: entityID,
        dataClass: deleted ? .tombstone : .workspace,
        modifiedAt: clock,
        originatingDevice: device,
        encryptedValue: .init(
            keyVersion: keyVersion,
            nonce: Data(repeating: byte, count: 12),
            ciphertextAndTag: Data(repeating: byte, count: 16)
        ),
        tombstone: tombstone
    )
}
