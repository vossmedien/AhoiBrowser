import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    struct FetchedEnvelopeRetention: Sendable {
        let record: SyncRecord
        let existing: SyncRecord?
        let snapshot: SyncRecord
    }

    /// Durably stages every distinct inbound encrypted envelope in one inbox
    /// write before choosing and persisting the final transport snapshots in one
    /// primary-store write. If the second write fails, the lossless inbox stays
    /// durable and the CloudKit token is blocked for a safe retry.
    static func retainFetchedEnvelopes(
        _ incomingRecords: [SyncRecord],
        in recordStore: any LocalSyncRecordStore
    ) async throws -> [FetchedEnvelopeRetention] {
        guard !incomingRecords.isEmpty else { return [] }
        // Preserve every exact inbound envelope first. The subsequent store
        // merge resolves against its then-current primary state atomically, so
        // a concurrent newer local enqueue cannot be overwritten by a stale
        // read-compute-write batch.
        try await recordStore.stageFetchedRecords(incomingRecords)
        return try await recordStore.mergeRecords(
            incomingRecords,
            policy: .transportLastWriterWins
        ).map {
            FetchedEnvelopeRetention(
                record: $0.incoming,
                existing: $0.existing,
                snapshot: $0.snapshot
            )
        }
    }

    /// Compatibility seam for focused callers and tests. Production fetch pages
    /// use the batch form above.
    @discardableResult
    static func retainFetchedEnvelope(
        _ incoming: SyncRecord,
        in recordStore: any LocalSyncRecordStore
    ) async throws -> (existing: SyncRecord?, snapshot: SyncRecord) {
        guard let retained = try await retainFetchedEnvelopes(
            [incoming],
            in: recordStore
        ).first else {
            return (nil, incoming)
        }
        return (retained.existing, retained.snapshot)
    }

    /// Chooses only the opaque record used for future CloudKit upload. LWW is
    /// safe here because retainFetchedEnvelope has already preserved `rhs` for
    /// the authenticated domain/field merge, even when `lhs` wins this choice.
    static func resolveTransportSnapshot(
        _ lhs: SyncRecord?,
        _ rhs: SyncRecord
    ) -> SyncRecord {
        SyncRecordTransportResolver.resolve(
            lhs,
            rhs,
            policy: .transportLastWriterWins
        )
    }

    /// The transport must stage an encrypted developer-asset envelope before
    /// its opt-in bit and secret-safety constraints can be authenticated. This
    /// grants only that single opaque ID passage to CompanionSyncBridge; it is
    /// not an outbound authorization.
    func authorizeInboundTransportEnvelope(_ record: SyncRecord) throws {
        let context = record.dataClass == .developerAsset
            ? SyncAuthorizationContext(optedInDeveloperAssetIDs: [record.entityID])
            : .init()
        try boundary.authorize(record, context: context)
    }

    func authorizeOutboundRecord(_ record: SyncRecord) throws {
        let allowedDeveloperAssetIDs = statusLock.withLock {
            authorizedDeveloperAssetIDs
        }
        try boundary.authorize(
            record,
            context: .init(optedInDeveloperAssetIDs: allowedDeveloperAssetIDs)
        )
    }

    func developerAssetAuthorizationIsPending(
        for record: SyncRecord,
        error: any Error
    ) -> Bool {
        guard record.dataClass == .developerAsset,
              record.tombstone == nil,
              let boundaryError = error as? SyncBoundaryError,
              case .developerAssetNotOptedIn = boundaryError else {
            return false
        }
        return statusLock.withLock { !developerAssetAuthorizationReady }
    }

    func rehydrateTransportIfRequired(using syncEngine: CKSyncEngine) async throws {
        let shouldRehydrate = statusLock.withLock { () -> Bool in
            guard engine === syncEngine, transportRehydrationRequired else { return false }
            transportRehydrationRequired = false
            return true
        }
        guard shouldRehydrate else { return }
        do {
            try await requeueLocalRecords()
        } catch {
            statusLock.withLock {
                if engine === syncEngine { transportRehydrationRequired = true }
            }
            throw error
        }
    }

    private func requeueLocalRecords() async throws {
        let quarantinedIDs = Set((await quarantineStore.allQuarantined()).keys)
        var changes: [CKSyncEngine.PendingRecordZoneChange] = []
        var deferredDeveloperAssetIDs = Set<UUID>()
        for record in try await recordStore.allRecords()
            where !quarantinedIDs.contains(record.recordID) {
            do {
                try authorizeOutboundRecord(record)
                _ = try codec.encode(record, zoneID: zoneID)
            } catch {
                if developerAssetAuthorizationIsPending(for: record, error: error) {
                    deferredDeveloperAssetIDs.insert(record.recordID)
                    continue
                }
                guard await persistQuarantine(
                    recordID: record.recordID,
                    reason: "outbound_boundary_validation_failed"
                ) else {
                    throw error
                }
                continue
            }
            changes.append(.saveRecord(CKRecord.ID(
                recordName: record.recordID.uuidString.lowercased(),
                zoneID: zoneID
            )))
        }
        let syncEngine = try activeEngine()
        statusLock.withLock {
            deferredDeveloperAssetRehydrationIDs = deferredDeveloperAssetIDs
        }
        syncEngine.state.add(pendingRecordZoneChanges: changes)
        if !changes.isEmpty { markTransportActivity() }
    }

}
#endif
