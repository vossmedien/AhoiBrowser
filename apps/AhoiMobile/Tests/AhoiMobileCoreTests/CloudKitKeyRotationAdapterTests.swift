import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CloudKitKeyRotationAdapterTests: XCTestCase {
    func testBoundedPublishRequiresAllWritersThenReturnsServerReadback() async throws {
        let first = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x21)
        let second = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x22)
        let records = [first, second]
        let manifest = makeAdapterManifest(records: records)
        let writerA = UUID()
        let writerB = UUID()
        let revoked = UUID()
        let events = AdapterEventLog()
        let transport = FixtureKeyRotationTransport(
            events: events,
            serverRecords: records
        )
        let writerAuthority = FixtureWriterAcknowledgements(
            events: events,
            snapshot: .init(
                planID: manifest.planID,
                keyVersion: manifest.nextVersion,
                recordIDs: manifest.recordIDs,
                recordsDigest: manifest.recordsDigest,
                promotedBootstrapKeyVersion: manifest.nextVersion,
                enrolledWriterIDs: [writerA, writerB, revoked],
                revokedWriterIDs: [revoked],
                acknowledgedWriterIDs: [writerA, writerB]
            )
        )
        let adapter = CloudKitKeyRotationRemoteAdapter(
            transport: transport,
            writerAcknowledgements: writerAuthority,
            now: { Date(timeIntervalSince1970: 2_000) }
        )

        let readback = try await adapter.publishAndReadBack(
            records: records,
            manifest: manifest
        )

        XCTAssertEqual(readback.records, records)
        XCTAssertEqual(readback.planID, manifest.planID)
        XCTAssertEqual(readback.keyVersion, 2)
        XCTAssertEqual(readback.readBackAt, Date(timeIntervalSince1970: 2_000))
        let requestedIDs = await transport.requestedServerRecordIDs()
        let sentPassIDs = await transport.sentPassIdentifiers()
        let finalizedPassIDs = await transport.finalizedPassIdentifiers()
        XCTAssertEqual(requestedIDs, manifest.recordIDs)
        XCTAssertEqual(sentPassIDs, [77])
        XCTAssertEqual(finalizedPassIDs, [77])
        let observed = await events.values()
        XCTAssertEqual(observed, [
            "register", "fetch", "validate-fetch", "enqueue", "send",
            "finalize", "await-writers", "server-readback",
        ])
    }

    func testMissingWriterInfrastructureFailsBeforeTransportMutation() async throws {
        let record = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x31)
        let manifest = makeAdapterManifest(records: [record])
        let events = AdapterEventLog()
        let transport = FixtureKeyRotationTransport(
            events: events,
            serverRecords: [record]
        )
        let adapter = CloudKitKeyRotationRemoteAdapter(transport: transport)

        do {
            _ = try await adapter.publishAndReadBack(
                records: [record],
                manifest: manifest
            )
            XCTFail("A local upload must never substitute for writer acknowledgements")
        } catch {
            XCTAssertEqual(
                error as? CloudKitKeyRotationAdapterError,
                .writerAcknowledgementInfrastructureUnavailable
            )
        }
        let observed = await events.values()
        XCTAssertTrue(observed.isEmpty)
    }

    func testMissingWriterAcknowledgementFailsBeforeServerReadback() async throws {
        let record = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x41)
        let manifest = makeAdapterManifest(records: [record])
        let writerA = UUID()
        let writerB = UUID()
        let events = AdapterEventLog()
        let transport = FixtureKeyRotationTransport(
            events: events,
            serverRecords: [record]
        )
        let writerAuthority = FixtureWriterAcknowledgements(
            events: events,
            snapshot: .init(
                planID: manifest.planID,
                keyVersion: manifest.nextVersion,
                recordIDs: manifest.recordIDs,
                recordsDigest: manifest.recordsDigest,
                promotedBootstrapKeyVersion: manifest.nextVersion,
                enrolledWriterIDs: [writerA, writerB],
                revokedWriterIDs: [],
                acknowledgedWriterIDs: [writerA]
            )
        )
        let adapter = CloudKitKeyRotationRemoteAdapter(
            transport: transport,
            writerAcknowledgements: writerAuthority
        )

        do {
            _ = try await adapter.publishAndReadBack(
                records: [record],
                manifest: manifest
            )
            XCTFail("Every non-revoked writer must acknowledge the manifest")
        } catch {
            XCTAssertEqual(
                error as? CloudKitKeyRotationAdapterError,
                .missingWriterAcknowledgements([writerB])
            )
        }
        let observed = await events.values()
        XCTAssertEqual(observed.last, "await-writers")
        XCTAssertFalse(observed.contains("server-readback"))
    }

    func testFetchedInboxBlocksRotationAndClosesBoundedPass() async throws {
        let record = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x51)
        let manifest = makeAdapterManifest(records: [record])
        let events = AdapterEventLog()
        let transport = FixtureKeyRotationTransport(
            events: events,
            serverRecords: [record],
            fetchBoundaryError: .fetchedRecordsRequireDomainMerge([UUID()])
        )
        let writer = UUID()
        let writerAuthority = FixtureWriterAcknowledgements(
            events: events,
            snapshot: matchingSnapshot(
                manifest: manifest,
                writerIDs: [writer]
            )
        )
        let adapter = CloudKitKeyRotationRemoteAdapter(
            transport: transport,
            writerAcknowledgements: writerAuthority
        )

        do {
            _ = try await adapter.publishAndReadBack(
                records: [record],
                manifest: manifest
            )
            XCTFail("Unmerged fetched envelopes must stop rotation")
        } catch {
            guard let adapterError =
                    error as? CloudKitKeyRotationAdapterError,
                  case .fetchedRecordsRequireDomainMerge = adapterError else {
                return XCTFail("Unexpected error: \(error)")
            }
        }
        let observed = await events.values()
        let finalizedPassIDs = await transport.finalizedPassIdentifiers()
        XCTAssertEqual(
            observed,
            ["register", "fetch", "validate-fetch", "finalize"]
        )
        XCTAssertEqual(finalizedPassIDs, [77])
    }

    func testUnpromotedBootstrapClaimFailsBeforeServerReadback() async throws {
        let record = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x59)
        let manifest = makeAdapterManifest(records: [record])
        let writer = UUID()
        let events = AdapterEventLog()
        let transport = FixtureKeyRotationTransport(
            events: events,
            serverRecords: [record]
        )
        let writerAuthority = FixtureWriterAcknowledgements(
            events: events,
            snapshot: .init(
                planID: manifest.planID,
                keyVersion: manifest.nextVersion,
                recordIDs: manifest.recordIDs,
                recordsDigest: manifest.recordsDigest,
                promotedBootstrapKeyVersion: manifest.currentVersion,
                enrolledWriterIDs: [writer],
                revokedWriterIDs: [],
                acknowledgedWriterIDs: [writer]
            )
        )
        let adapter = CloudKitKeyRotationRemoteAdapter(
            transport: transport,
            writerAcknowledgements: writerAuthority
        )

        do {
            _ = try await adapter.publishAndReadBack(
                records: [record],
                manifest: manifest
            )
            XCTFail("The authoritative bootstrap claim must be promoted first")
        } catch {
            XCTAssertEqual(
                error as? CloudKitKeyRotationAdapterError,
                .bootstrapClaimPromotionMissing(expected: 2, actual: 1)
            )
        }
        let observed = await events.values()
        XCTAssertEqual(observed.last, "await-writers")
        XCTAssertFalse(observed.contains("server-readback"))
    }

    func testServerReadbackMustContainExactManifestRecordSet() async throws {
        let first = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x61)
        let second = makeAdapterRecord(id: UUID(), keyVersion: 2, byte: 0x62)
        let records = [first, second]
        let manifest = makeAdapterManifest(records: records)
        let events = AdapterEventLog()
        let transport = FixtureKeyRotationTransport(
            events: events,
            serverRecords: [first]
        )
        let writer = UUID()
        let writerAuthority = FixtureWriterAcknowledgements(
            events: events,
            snapshot: matchingSnapshot(
                manifest: manifest,
                writerIDs: [writer]
            )
        )
        let adapter = CloudKitKeyRotationRemoteAdapter(
            transport: transport,
            writerAcknowledgements: writerAuthority
        )

        do {
            _ = try await adapter.publishAndReadBack(
                records: records,
                manifest: manifest
            )
            XCTFail("A partial server readback cannot acknowledge rotation")
        } catch {
            XCTAssertEqual(
                error as? CloudKitKeyRotationAdapterError,
                .serverReadbackRecordSetMismatch
            )
        }
    }
}

private actor AdapterEventLog {
    private var events: [String] = []

    func append(_ event: String) { events.append(event) }
    func values() -> [String] { events }
}

private actor FixtureKeyRotationTransport: CloudKitKeyRotationTransporting {
    private let events: AdapterEventLog
    private let serverRecords: [SyncRecord]
    private let fetchBoundaryError: CloudKitKeyRotationAdapterError?
    private var requestedIDs: [UUID] = []
    private var sentPassIDs: [UInt64] = []
    private var finalizedPassIDs: [UInt64] = []

    init(
        events: AdapterEventLog,
        serverRecords: [SyncRecord],
        fetchBoundaryError: CloudKitKeyRotationAdapterError? = nil
    ) {
        self.events = events
        self.serverRecords = serverRecords
        self.fetchBoundaryError = fetchBoundaryError
    }

    func fetchKeyRotationChanges() async throws -> UInt64 {
        await events.append("fetch")
        return 77
    }

    func validateKeyRotationFetchBoundary(
        manifest: CompanionKeyRotationManifest
    ) async throws {
        _ = manifest
        await events.append("validate-fetch")
        if let fetchBoundaryError { throw fetchBoundaryError }
    }

    func enqueueKeyRotationRecords(
        _ records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) async throws {
        _ = records
        _ = manifest
        await events.append("enqueue")
    }

    func sendKeyRotationChanges(passID: UInt64) async throws {
        sentPassIDs.append(passID)
        await events.append("send")
    }

    func finalizeKeyRotationChanges(passID: UInt64) async throws {
        finalizedPassIDs.append(passID)
        await events.append("finalize")
    }

    func readKeyRotationRecordsFromServer(
        recordIDs: [UUID]
    ) async throws -> [SyncRecord] {
        requestedIDs = recordIDs
        await events.append("server-readback")
        let requested = Set(recordIDs)
        return serverRecords.filter { requested.contains($0.recordID) }
    }

    func requestedServerRecordIDs() -> [UUID] { requestedIDs }
    func sentPassIdentifiers() -> [UInt64] { sentPassIDs }
    func finalizedPassIdentifiers() -> [UInt64] { finalizedPassIDs }
}

private actor FixtureWriterAcknowledgements:
    CloudKitKeyRotationWriterAcknowledgementProviding {
    private let events: AdapterEventLog
    private let snapshot: CloudKitKeyRotationWriterAcknowledgementSnapshot

    init(
        events: AdapterEventLog,
        snapshot: CloudKitKeyRotationWriterAcknowledgementSnapshot
    ) {
        self.events = events
        self.snapshot = snapshot
    }

    func registerRotation(
        _ manifest: CompanionKeyRotationManifest
    ) async throws {
        _ = manifest
        await events.append("register")
    }

    func awaitAcknowledgements(
        for manifest: CompanionKeyRotationManifest
    ) async throws -> CloudKitKeyRotationWriterAcknowledgementSnapshot {
        _ = manifest
        await events.append("await-writers")
        return snapshot
    }
}

private func matchingSnapshot(
    manifest: CompanionKeyRotationManifest,
    writerIDs: Set<UUID>
) -> CloudKitKeyRotationWriterAcknowledgementSnapshot {
    .init(
        planID: manifest.planID,
        keyVersion: manifest.nextVersion,
        recordIDs: manifest.recordIDs,
        recordsDigest: manifest.recordsDigest,
        promotedBootstrapKeyVersion: manifest.nextVersion,
        enrolledWriterIDs: writerIDs,
        revokedWriterIDs: [],
        acknowledgedWriterIDs: writerIDs
    )
}

private func makeAdapterManifest(
    records: [SyncRecord]
) -> CompanionKeyRotationManifest {
    CompanionKeyRotationManifest(
        planID: UUID(),
        currentVersion: 1,
        nextVersion: 2,
        transitionEndsAt: Date(timeIntervalSince1970: 3_000),
        recordIDs: records.map(\.recordID).sorted {
            $0.uuidString < $1.uuidString
        },
        recordsDigest: Data(repeating: 0xA5, count: 32)
    )
}

private func makeAdapterRecord(
    id: UUID,
    keyVersion: UInt32,
    byte: UInt8
) -> SyncRecord {
    let deviceID = DeviceID()
    return SyncRecord(
        recordID: id,
        entityID: id,
        dataClass: .workspace,
        modifiedAt: HybridLogicalClock(
            physicalMilliseconds: 1_000,
            nodeID: deviceID
        ),
        originatingDevice: deviceID,
        encryptedValue: EncryptedValue(
            keyVersion: keyVersion,
            nonce: Data(repeating: byte, count: 12),
            ciphertextAndTag: Data(repeating: byte, count: 16)
        )
    )
}
