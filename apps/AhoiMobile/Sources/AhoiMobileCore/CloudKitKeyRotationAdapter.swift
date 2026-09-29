import Foundation
import AhoiCloudKitSpike

public enum CloudKitKeyRotationAdapterError: Error, Equatable, Sendable {
    case manifestRecordSetMismatch
    case recordKeyVersionMismatch(
        recordID: UUID,
        expected: UInt32,
        actual: UInt32
    )
    case writerAcknowledgementInfrastructureUnavailable
    case writerAcknowledgementIdentityMismatch
    case bootstrapClaimPromotionMissing(expected: UInt32, actual: UInt32?)
    case invalidWriterRoster
    case noActiveWriters
    case missingWriterAcknowledgements([UUID])
    case fetchedRecordsRequireDomainMerge([UUID])
    case pendingQueueReadbackMismatch([UUID])
    case boundedPassDidNotDrain
    case serverReadbackRecordSetMismatch
    case serverReadbackKeyVersionMismatch(
        recordID: UUID,
        expected: UInt32,
        actual: UInt32
    )
    case serverRecordUnavailable(UUID)
}

/// A manifest-bound snapshot supplied by the durable writer-membership
/// authority. An upload by this device is deliberately not represented here:
/// every enrolled writer that is not revoked must acknowledge the same plan,
/// record set, digest, and next key version.
public struct CloudKitKeyRotationWriterAcknowledgementSnapshot: Sendable {
    public let planID: UUID
    public let keyVersion: UInt32
    public let recordIDs: [UUID]
    public let recordsDigest: Data
    /// Version read back from the authoritative bootstrap claim after its
    /// compare-and-swap promotion. Local key installation is not sufficient.
    public let promotedBootstrapKeyVersion: UInt32?
    public let enrolledWriterIDs: Set<UUID>
    public let revokedWriterIDs: Set<UUID>
    public let acknowledgedWriterIDs: Set<UUID>

    public init(
        planID: UUID,
        keyVersion: UInt32,
        recordIDs: [UUID],
        recordsDigest: Data,
        promotedBootstrapKeyVersion: UInt32?,
        enrolledWriterIDs: Set<UUID>,
        revokedWriterIDs: Set<UUID>,
        acknowledgedWriterIDs: Set<UUID>
    ) {
        self.planID = planID
        self.keyVersion = keyVersion
        self.recordIDs = recordIDs.sorted(by: Self.sortIDs)
        self.recordsDigest = recordsDigest
        self.promotedBootstrapKeyVersion = promotedBootstrapKeyVersion
        self.enrolledWriterIDs = enrolledWriterIDs
        self.revokedWriterIDs = revokedWriterIDs
        self.acknowledgedWriterIDs = acknowledgedWriterIDs
    }

    private static func sortIDs(_ lhs: UUID, _ rhs: UUID) -> Bool {
        lhs.uuidString < rhs.uuidString
    }
}

/// Production implementations must register the plan durably and
/// idempotently before upload, reject a competing active plan, distribute the
/// next key through the approved key channel, and wait for acknowledgements
/// from the complete enrolled/non-revoked writer roster.
public protocol CloudKitKeyRotationWriterAcknowledgementProviding: Sendable {
    func registerRotation(
        _ manifest: CompanionKeyRotationManifest
    ) async throws

    func awaitAcknowledgements(
        for manifest: CompanionKeyRotationManifest
    ) async throws -> CloudKitKeyRotationWriterAcknowledgementSnapshot
}

/// Provider-neutral seam around the exact bounded operations required by the
/// rotation coordinator. The final method must read from the server, never
/// from the provider's local SyncRecord store or fetched-record inbox.
public protocol CloudKitKeyRotationTransporting: Sendable {
    func fetchKeyRotationChanges() async throws -> UInt64
    func validateKeyRotationFetchBoundary(
        manifest: CompanionKeyRotationManifest
    ) async throws
    func enqueueKeyRotationRecords(
        _ records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) async throws
    func sendKeyRotationChanges(passID: UInt64) async throws
    func finalizeKeyRotationChanges(passID: UInt64) async throws
    func readKeyRotationRecordsFromServer(
        recordIDs: [UUID]
    ) async throws -> [SyncRecord]
}

/// Real remote acknowledgement adapter for CompanionKeyRotationCoordinator.
/// Without an injected writer-membership authority it fails before making any
/// transport mutation, so the coordinator can never advance to old-key
/// revocation merely because one device uploaded successfully.
public struct CloudKitKeyRotationRemoteAdapter:
    CompanionKeyRotationRemoteAcknowledging,
    Sendable {
    private let transport: any CloudKitKeyRotationTransporting
    private let writerAcknowledgements:
        (any CloudKitKeyRotationWriterAcknowledgementProviding)?
    private let now: @Sendable () -> Date

    public init(
        transport: any CloudKitKeyRotationTransporting,
        writerAcknowledgements:
            (any CloudKitKeyRotationWriterAcknowledgementProviding)? = nil,
        now: @escaping @Sendable () -> Date = { Date() }
    ) {
        self.transport = transport
        self.writerAcknowledgements = writerAcknowledgements
        self.now = now
    }

    public func publishAndReadBack(
        records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) async throws -> CompanionKeyRotationRemoteReadback {
        try validatePublishedRecords(records, manifest: manifest)
        guard let writerAcknowledgements else {
            throw CloudKitKeyRotationAdapterError
                .writerAcknowledgementInfrastructureUnavailable
        }

        // Registration precedes upload so a competing active plan cannot
        // overwrite records before the durable membership authority rejects it.
        try await writerAcknowledgements.registerRotation(manifest)
        try await runBoundedPublish(records: records, manifest: manifest)

        let snapshot = try await writerAcknowledgements.awaitAcknowledgements(
            for: manifest
        )
        try validate(snapshot: snapshot, manifest: manifest)

        let readback = try await transport.readKeyRotationRecordsFromServer(
            recordIDs: manifest.recordIDs
        )
        try validateServerReadback(readback, manifest: manifest)
        return CompanionKeyRotationRemoteReadback(
            planID: manifest.planID,
            keyVersion: manifest.nextVersion,
            records: readback,
            readBackAt: now()
        )
    }

    private func runBoundedPublish(
        records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) async throws {
        let passID = try await transport.fetchKeyRotationChanges()
        do {
            try await transport.validateKeyRotationFetchBoundary(
                manifest: manifest
            )
            try await transport.enqueueKeyRotationRecords(
                records,
                manifest: manifest
            )
            try await transport.sendKeyRotationChanges(passID: passID)
            try await transport.finalizeKeyRotationChanges(passID: passID)
        } catch let operationError {
            // Best-effort pass closure only; the original failure remains the
            // authority and no acknowledgement/readback can follow it.
            try? await transport.finalizeKeyRotationChanges(passID: passID)
            throw operationError
        }
    }

    private func validatePublishedRecords(
        _ records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) throws {
        let publishedIDs = records.map(\.recordID)
        guard Set(publishedIDs).count == publishedIDs.count,
              Set(manifest.recordIDs).count == manifest.recordIDs.count,
              publishedIDs.sorted(by: sortIDs) ==
                manifest.recordIDs.sorted(by: sortIDs) else {
            throw CloudKitKeyRotationAdapterError.manifestRecordSetMismatch
        }
        for record in records where
            record.encryptedValue.keyVersion != manifest.nextVersion {
            throw CloudKitKeyRotationAdapterError.recordKeyVersionMismatch(
                recordID: record.recordID,
                expected: manifest.nextVersion,
                actual: record.encryptedValue.keyVersion
            )
        }
    }

    private func validate(
        snapshot: CloudKitKeyRotationWriterAcknowledgementSnapshot,
        manifest: CompanionKeyRotationManifest
    ) throws {
        guard snapshot.planID == manifest.planID,
              snapshot.keyVersion == manifest.nextVersion,
              snapshot.recordIDs == manifest.recordIDs.sorted(by: sortIDs),
              snapshot.recordsDigest == manifest.recordsDigest else {
            throw CloudKitKeyRotationAdapterError
                .writerAcknowledgementIdentityMismatch
        }
        guard snapshot.promotedBootstrapKeyVersion == manifest.nextVersion else {
            throw CloudKitKeyRotationAdapterError.bootstrapClaimPromotionMissing(
                expected: manifest.nextVersion,
                actual: snapshot.promotedBootstrapKeyVersion
            )
        }
        guard snapshot.revokedWriterIDs.isSubset(
            of: snapshot.enrolledWriterIDs
        ) else {
            throw CloudKitKeyRotationAdapterError.invalidWriterRoster
        }
        let required = snapshot.enrolledWriterIDs.subtracting(
            snapshot.revokedWriterIDs
        )
        guard !required.isEmpty else {
            throw CloudKitKeyRotationAdapterError.noActiveWriters
        }
        let missing = required.subtracting(snapshot.acknowledgedWriterIDs)
        guard missing.isEmpty else {
            throw CloudKitKeyRotationAdapterError
                .missingWriterAcknowledgements(missing.sorted(by: sortIDs))
        }
    }

    private func validateServerReadback(
        _ records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) throws {
        let recordIDs = records.map(\.recordID)
        guard Set(recordIDs).count == recordIDs.count,
              recordIDs.sorted(by: sortIDs) ==
                manifest.recordIDs.sorted(by: sortIDs) else {
            throw CloudKitKeyRotationAdapterError
                .serverReadbackRecordSetMismatch
        }
        for record in records where
            record.encryptedValue.keyVersion != manifest.nextVersion {
            throw CloudKitKeyRotationAdapterError
                .serverReadbackKeyVersionMismatch(
                    recordID: record.recordID,
                    expected: manifest.nextVersion,
                    actual: record.encryptedValue.keyVersion
                )
        }
    }

    private func sortIDs(_ lhs: UUID, _ rhs: UUID) -> Bool {
        lhs.uuidString < rhs.uuidString
    }
}

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider: CloudKitKeyRotationTransporting {
    public func makeKeyRotationRemoteAdapter(
        writerAcknowledgements:
            (any CloudKitKeyRotationWriterAcknowledgementProviding)? = nil,
        now: @escaping @Sendable () -> Date = { Date() }
    ) -> CloudKitKeyRotationRemoteAdapter {
        CloudKitKeyRotationRemoteAdapter(
            transport: self,
            writerAcknowledgements: writerAcknowledgements,
            now: now
        )
    }

    public func fetchKeyRotationChanges() async throws -> UInt64 {
        try await fetchChanges()
    }

    public func validateKeyRotationFetchBoundary(
        manifest: CompanionKeyRotationManifest
    ) async throws {
        _ = manifest
        let fetched = try await recordStore.fetchedRecords()
        guard fetched.isEmpty else {
            throw CloudKitKeyRotationAdapterError
                .fetchedRecordsRequireDomainMerge(
                    Array(Set(fetched.map(\.recordID))).sorted(by: sortRotationIDs)
                )
        }
    }

    public func enqueueKeyRotationRecords(
        _ records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) async throws {
        guard beginActivity() else {
            throw CloudKitSyncProviderError.unavailable
        }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let syncEngine = try activeEngine()
        guard !statusLock.withLock({
            accountTransitionPending || zoneRecoveryPending ||
                statePersistenceBlocked
        }) else {
            throw CloudKitSyncProviderError.unavailable
        }

        for record in records {
            try authorizeOutboundRecord(record)
            _ = try codec.encode(record, zoneID: zoneID)
        }
        try await recordStore.upsert(records)
        try await ensureAccountContinuity()
        guard (try activeEngine()) === syncEngine else {
            throw CloudKitSyncProviderError.unavailable
        }

        let changes = manifest.recordIDs.map { recordID in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(CKRecord.ID(
                recordName: recordID.uuidString.lowercased(),
                zoneID: zoneID
            ))
        }
        syncEngine.state.add(pendingRecordZoneChanges: changes)
        let requiredIDs = Set(manifest.recordIDs)
        let pendingIDs: Set<UUID> = Set(
            syncEngine.state.pendingRecordZoneChanges.compactMap {
            guard case let .saveRecord(cloudID) = $0 else { return nil }
            return UUID(uuidString: cloudID.recordName)
            }
        )
        let missing = requiredIDs.subtracting(pendingIDs)
        guard missing.isEmpty else {
            throw CloudKitKeyRotationAdapterError.pendingQueueReadbackMismatch(
                missing.sorted(by: sortRotationIDs)
            )
        }
        if !changes.isEmpty { markTransportActivity() }
    }

    public func sendKeyRotationChanges(passID: UInt64) async throws {
        try await sendPendingChanges(passID: passID)
    }

    public func finalizeKeyRotationChanges(passID: UInt64) async throws {
        try await finalizeBoundedSync(passID: passID)
        let fetchedInboxIsEmpty = try await recordStore.fetchedRecords().isEmpty
        let pendingQueueIsEmpty = try activeEngine()
            .state.pendingRecordZoneChanges.isEmpty
        guard fetchedInboxIsEmpty,
              pendingQueueIsEmpty,
              status().phase == .idle else {
            throw CloudKitKeyRotationAdapterError.boundedPassDidNotDrain
        }
    }

    public func readKeyRotationRecordsFromServer(
        recordIDs: [UUID]
    ) async throws -> [SyncRecord] {
        guard beginActivity() else {
            throw CloudKitSyncProviderError.unavailable
        }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let expectedEngine = try activeEngine()
        let cloudIDs = recordIDs.map { recordID in
            CKRecord.ID(
                recordName: recordID.uuidString.lowercased(),
                zoneID: zoneID
            )
        }
        let results = try await database.records(
            for: cloudIDs,
            desiredKeys: nil
        )
        try await ensureAccountContinuity()
        guard (try activeEngine()) === expectedEngine else {
            throw CloudKitSyncProviderError.unavailable
        }

        var records: [SyncRecord] = []
        records.reserveCapacity(recordIDs.count)
        for (recordID, cloudID) in zip(recordIDs, cloudIDs) {
            guard let result = results[cloudID] else {
                throw CloudKitKeyRotationAdapterError
                    .serverRecordUnavailable(recordID)
            }
            let cloudRecord: CKRecord
            switch result {
            case let .success(record):
                cloudRecord = record
            case .failure:
                throw CloudKitKeyRotationAdapterError
                    .serverRecordUnavailable(recordID)
            }
            let decoded = try codec.decode(cloudRecord)
            guard decoded.recordID == recordID else {
                throw CloudKitKeyRotationAdapterError
                    .serverReadbackRecordSetMismatch
            }
            records.append(decoded)
        }
        return records
    }

    private func sortRotationIDs(_ lhs: UUID, _ rhs: UUID) -> Bool {
        lhs.uuidString < rhs.uuidString
    }
}
#endif
