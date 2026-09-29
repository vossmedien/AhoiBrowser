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

    /// Shared-tab writer gate input: a savedRecords receipt persisted the
    /// record's server system fields and no newer save is pending. Any
    /// unavailable/replaced engine or unreadable sidecar fails closed.
    func isRecordAcknowledged(_ recordID: UUID) async -> Bool {
        let cloudID = CKRecord.ID(recordName: recordID.uuidString.lowercased(), zoneID: zoneID)
        let pending = statusLock.withLock { () -> Bool in
            guard !isInvalidated, !engineReplacementInProgress,
                  !accountTransitionPending, !zoneRecoveryPending,
                  let engine else { return true }
            return engine.state.pendingRecordZoneChanges.contains { change in
                switch change {
                case .saveRecord(let id), .deleteRecord(let id): return id == cloudID
                @unknown default: return true
                }
            }
        }
        guard !pending else { return false }
        return (try? await systemFieldsStore.data(for: recordID)) != nil
    }

    /// Only a savedRecords receipt for the exact current encrypted Workspace
    /// tombstone may authorize the domain's local compaction write. Hold an
    /// exclusive public-provider activity across the two local files. Engine
    /// delegate events still run and are rechecked before cache removal. No
    /// CKRecord delete is requested.
    func compactAcknowledgedWorkspace(
        _ record: SyncRecord,
        domainCommit: @Sendable () async throws -> Void
    ) async throws -> Bool {
        guard record.dataClass == .workspace,
              record.recordID == record.entityID,
              record.tombstone?.entityID == record.entityID else { return false }
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let lease = statusLock.withLock { () -> (CKSyncEngine, String, UInt64)? in
            guard !isInvalidated, !engineReplacementInProgress,
                  accountContinuityVerified, !accountTransitionPending,
                  !zoneRecoveryPending, !statePersistenceBlocked,
                  !boundedSyncPassActive, !compactionInProgress,
                  activeActivityCount == 1, let engine,
                  let accountID = lastKnownAccountIdentifier,
                  !accountID.isEmpty else { return nil }
            compactionInProgress = true
            return (engine, accountID, engineGeneration)
        }
        guard let (leasedEngine, accountID, generation) = lease else { return false }
        defer { statusLock.withLock { compactionInProgress = false } }

        let cloudID = CKRecord.ID(
            recordName: record.recordID.uuidString.lowercased(), zoneID: zoneID)
        let hasPendingChange = {
            leasedEngine.state.pendingRecordZoneChanges.contains { change in
                switch change {
                case .saveRecord(let id), .deleteRecord(let id): return id == cloudID
                @unknown default: return true
                }
            }
        }
        guard !hasPendingChange(),
              await quarantineStore.entry(for: record.recordID) == nil,
              try await recordStore.record(for: record.recordID) == record else {
            return false
        }
        let fetched = try await recordStore.fetchedRecords()
        guard !fetched.contains(where: { $0.recordID == record.recordID }),
              try await recordStore.isUploadedTombstoneAcknowledged(
                  record, accountID: accountID,
                  containerID: configuration.containerIdentifier,
                  zoneName: zoneID.zoneName) else { return false }
        let stillLeased = statusLock.withLock {
            compactionInProgress && activeActivityCount == 1 && !isInvalidated &&
                !engineReplacementInProgress && engine === leasedEngine &&
                engineGeneration == generation &&
                accountContinuityVerified && !accountTransitionPending &&
                !zoneRecoveryPending && !statePersistenceBlocked &&
                lastKnownAccountIdentifier == accountID
        }
        guard stillLeased, !hasPendingChange() else {
            throw CloudKitSyncProviderError.unavailable
        }
        try await domainCommit()
        guard statusLock.withLock({
            compactionInProgress && activeActivityCount == 1 && !isInvalidated &&
                engine === leasedEngine && engineGeneration == generation &&
                accountContinuityVerified && !accountTransitionPending &&
                !zoneRecoveryPending && !statePersistenceBlocked &&
                lastKnownAccountIdentifier == accountID
        }), !hasPendingChange() else {
            // The Snapshot watermark is durable; leave the encrypted cache for
            // a later exact retry instead of removing a newly queued save.
            throw CloudKitSyncProviderError.unavailable
        }
        try await recordStore.removeLocallyCompactedTombstones(
            [record], accountID: accountID,
            containerID: configuration.containerIdentifier,
            zoneName: zoneID.zoneName)
        return true
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
        // A reader-capable binary must not upload/recover v3 envelopes until
        // the matching-client gate is implemented and deliberately enabled.
        try bookmarkTransportAuthorization.authorize(record)
        try browserSettingTransportAuthorization.authorize(record)
        let allowedDeveloperAssetIDs = statusLock.withLock {
            authorizedDeveloperAssetIDs
        }
        try boundary.authorize(
            record,
            context: .init(optedInDeveloperAssetIDs: allowedDeveloperAssetIDs)
        )
    }

    func configureBrowserSettingValidation(
        _ validator: @escaping BrowserSettingTransportAuthorization.Validator
    ) {
        browserSettingTransportAuthorization.configure(validator: validator)
    }

    func setBrowserSettingApprovedIDs(_ ids: Set<UUID>, epoch: UInt64) {
        let invalidated = statusLock.withLock { isInvalidated }
        let changed = browserSettingTransportAuthorization.setApproved(
            invalidated ? [] : ids, epoch: epoch
        )
        if changed, !invalidated {
            statusLock.withLock { transportRehydrationRequired = true }
        }
    }

    func isBrowserSettingApproved(_ id: UUID, epoch: UInt64) -> Bool {
        !statusLock.withLock({ isInvalidated }) &&
            browserSettingTransportAuthorization.isApproved(id, epoch: epoch)
    }

    func setExtensionSetupMetadataApproved(_ approved: Bool, epoch: UInt64) {
        statusLock.withLock {
            guard epoch >= extensionSetupMetadataEpoch else { return }
            extensionSetupMetadataEpoch = epoch
            extensionSetupMetadataApproved = approved && !isInvalidated
        }
    }

    func isExtensionSetupMetadataApproved(epoch: UInt64) -> Bool {
        statusLock.withLock {
            !isInvalidated && accountContinuityVerified && !accountTransitionPending &&
                !zoneRecoveryPending && !statePersistenceBlocked &&
                extensionSetupMetadataApproved && epoch == extensionSetupMetadataEpoch
        }
    }

    func setExtensionStorageMetadataApproved(_ approved: Bool, epoch: UInt64) {
        statusLock.withLock {
            guard epoch >= extensionStorageMetadataEpoch else { return }
            extensionStorageMetadataEpoch = epoch
            extensionStorageMetadataApproved = approved && !isInvalidated
        }
    }

    func isExtensionStorageMetadataApproved(epoch: UInt64) -> Bool {
        statusLock.withLock {
            !isInvalidated && accountContinuityVerified && !accountTransitionPending &&
                !zoneRecoveryPending && !statePersistenceBlocked &&
                extensionStorageMetadataApproved && epoch == extensionStorageMetadataEpoch
        }
    }

    func isCategoryConsentDeferral(_ error: any Error) -> Bool {
        error as? BookmarkTransportAuthorizationError == .categoryNotApproved ||
            error as? BrowserSettingTransportAuthorizationError == .categoryNotApproved
    }

    func setBookmarkCategoryApproved(_ approved: Bool) {
        guard bookmarkTransportAuthorization.setApproved(approved), approved else { return }
        // Previously cached/paused records remain local. An explicit approval
        // permits the existing rehydration path to revisit them on the next pass.
        statusLock.withLock { transportRehydrationRequired = true }
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
                if isCategoryConsentDeferral(error) {
                    continue
                }
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
