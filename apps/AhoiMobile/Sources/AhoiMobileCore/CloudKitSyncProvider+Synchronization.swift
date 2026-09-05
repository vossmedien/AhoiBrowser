import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    /// Schedules the custom zone through CKSyncEngine and then sends pending
    /// local changes. No network call is made until this method is invoked or
    /// automatic sync is enabled by the caller.
    public func prepare() async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let engine = try activeEngine()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        statusLock.withLock { zonePreparationInProgress = true }
        defer { statusLock.withLock { zonePreparationInProgress = false } }
        setStatus(.init(phase: .preparing, detail: SyncText(
            "sync.status.preparing_zone", "Preparing sync zone"
        )))
        engine.state.add(pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))])
        do {
            try await engine.sendChanges()
            // A serialized CKSyncEngine already owns its pending queue. Only a
            // brand-new or deliberately rebuilt engine needs the durable
            // transport snapshot rehydrated.
            try await rehydrateTransportIfRequired(using: engine)
            setStatus(.init(phase: .idle, detail: SyncText("sync.status.ready", "Ready")))
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            throw error
        }
    }

    /// Fetches encrypted envelopes without sending local changes first. The
    /// bridge can therefore run the authenticated field merge before the first
    /// upload, rather than reporting success with a newly queued merge.
    @discardableResult
    public func fetchChanges() async throws -> UInt64 {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        if statusLock.withLock({ statePersistenceBlocked }) {
            try await rebuildEngineForFullRefetch()
        }
        let hasPersistentQuarantine = !(await quarantineStore.allQuarantined()).isEmpty
        let passID = try beginBoundedSyncPass(
            persistentlyBlocked: hasPersistentQuarantine
        )
        let engine = try activeEngine()
        do {
            try await engine.fetchChanges(
                .init(scope: .zoneIDs([zoneID]))
            )
            // Do not hand fetched envelopes to the domain bridge unless the
            // account identity and engine are still the ones that opened this
            // pass. An iCloud switch during the network await is fail-closed.
            try await ensureAccountContinuity()
            guard (try activeEngine()) === engine,
                  statusLock.withLock({
                      accountContinuityVerified && !accountTransitionPending
                  }) else {
                throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
            }
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            finishBoundedSyncPass(passID)
            throw error
        }
        return passID
    }

    /// Flushes changes that were produced by the authenticated domain merge
    /// after the fetch phase. Keeping this separate prevents a disjoint-field
    /// merge from remaining pending after the public sync operation reports
    /// success.
    public func sendPendingChanges(passID: UInt64) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        // A state rebuild intentionally starts with no pending record queue.
        // Rehydrate it exactly once after fetch/domain merge and immediately
        // after a fail-closed account identity check. Normal sends must flush
        // only changes already made pending by a real local/domain mutation.
        try await ensureAccountContinuity()
        let engine = try activeEngine()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        try await rehydrateTransportIfRequired(using: engine)
        guard (try activeEngine()) === engine else {
            throw CloudKitSyncProviderError.unavailable
        }
        guard let windowPassID = beginOutboundBatchWindow(passID: passID) else {
            if statusLock.withLock({
                boundedSyncPassActive && boundedSyncPassID == passID &&
                    boundedSyncPassOutboundBlocked
            }) {
                return
            }
            throw CloudKitSyncProviderError.boundedSyncPassRequired
        }
        defer { endOutboundBatchWindow(windowPassID) }
        do {
            try await engine.sendChanges(
                .init(scope: .zoneIDs([zoneID]))
            )
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            finishBoundedSyncPass(windowPassID)
            throw error
        }
    }

    /// Ends a bounded sync pass honestly. Server conflicts produced by the
    /// final send may queue another record or fetched envelope; those remain a
    /// visible retry state instead of being mislabeled as fully synced.
    public func finalizeBoundedSync(passID: UInt64) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        let activitySnapshot = try statusLock.withLock {
            guard boundedSyncPassActive, boundedSyncPassID == passID else {
                throw CloudKitSyncProviderError.boundedSyncPassRequired
            }
            return (
                activityEpoch,
                boundedSyncPassID,
                currentStatus.phase,
                boundedSyncPassCompletionBlocked
            )
        }
        let pendingFetched = try await recordStore.fetchedRecords().count
        let engine = try activeEngine()
        let pendingRecords = engine.state.pendingRecordZoneChanges.count
        let persistentQuarantine = !(await quarantineStore.allQuarantined()).isEmpty
        var shouldRequestFollowUp = false
        if persistentQuarantine {
            setStatus(.init(
                phase: .quarantined,
                detail: SyncText(
                    "sync.status.record_quarantined",
                    "At least one CloudKit record was quarantined"
                )
            ))
        } else if pendingRecords > 0 || pendingFetched > 0 {
            setRetryScheduledUnlessBlocked()
            shouldRequestFollowUp = true
        } else {
            let completed = setSyncedIfActivityUnchanged(
                activitySnapshot.0,
                passID: activitySnapshot.1,
                observedPhase: activitySnapshot.2,
                observedBlocked: activitySnapshot.3
            )
            if !completed {
                setRetryScheduledUnlessBlocked()
                shouldRequestFollowUp = true
            }
        }
        finishBoundedSyncPass(activitySnapshot.1)
        if shouldRequestFollowUp, status().phase == .retryScheduled {
            requestEventDrivenSyncIfUnbounded()
        }
    }

    func enqueue(
        _ record: SyncRecord,
        authorization: SyncAuthorizationContext = .init()
    ) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try bookmarkTransportAuthorization.authorize(record)
        try boundary.authorize(record, context: authorization)
        try await recordStore.upsert(record)
        if record.dataClass == .developerAsset {
            statusLock.withLock {
                developerAssetAuthorizationMutationEpoch &+= 1
                authorizedDeveloperAssetIDs.insert(record.entityID)
                developerAssetAuthorizationLastMutation[record.entityID] =
                    developerAssetAuthorizationMutationEpoch
            }
        }
        let engine = try activeEngine()
        let cloudID = CKRecord.ID(
            recordName: record.recordID.uuidString.lowercased(),
            zoneID: zoneID
        )
        engine.state.add(pendingRecordZoneChanges: [.saveRecord(cloudID)])
        markTransportActivity()
        requestEventDrivenSyncIfUnbounded()
    }

    func currentDeveloperAssetAuthorizationMutationEpoch() -> UInt64 {
        statusLock.withLock { developerAssetAuthorizationMutationEpoch }
    }

    /// Seeds only missing or newer domain-authority records in one durable
    /// record-store transaction. `authorizedDeveloperAssetIDs` includes reused
    /// envelopes too, allowing the outbound boundary to be reconstructed at
    /// launch without resealing or uploading unchanged developer assets.
    func enqueueLocalSnapshot(
        _ records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        scanStartedAtMutationEpoch: UInt64
    ) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        let effectiveAuthorizedIDs = statusLock.withLock {
            Set(authorizedDeveloperAssetIDs.filter { recordID in
                guard let lastMutation = developerAssetAuthorizationLastMutation[recordID],
                      lastMutation > scanStartedAtMutationEpoch else {
                    return true
                }
                return self.authorizedDeveloperAssetIDs.contains(recordID)
            })
        }
        let effectiveRecords = records.filter { record in
            record.dataClass != .developerAsset || record.tombstone != nil ||
                effectiveAuthorizedIDs.contains(record.entityID)
        }
        let authorization = SyncAuthorizationContext(
            optedInDeveloperAssetIDs: effectiveAuthorizedIDs
        )
        for record in effectiveRecords {
            try bookmarkTransportAuthorization.authorize(record)
            try boundary.authorize(record, context: authorization)
        }
        let engine = try activeEngine()
        let deferredAuthorizedIDs = statusLock.withLock {
            deferredDeveloperAssetRehydrationIDs
                .intersection(effectiveAuthorizedIDs)
        }
        for recordID in deferredAuthorizedIDs {
            guard let record = try await recordStore.record(for: recordID),
                  record.recordID == recordID,
                  record.entityID == recordID,
                  record.dataClass == .developerAsset else {
                throw CloudKitSyncProviderError.transportRehydrationRecordMissing
            }
            try boundary.authorize(record, context: authorization)
            _ = try codec.encode(record, zoneID: zoneID)
        }
        try await recordStore.upsert(effectiveRecords)
        statusLock.withLock {
            let knownBeforeScan = self.authorizedDeveloperAssetIDs.filter { recordID in
                (developerAssetAuthorizationLastMutation[recordID] ?? 0) <=
                    scanStartedAtMutationEpoch
            }
            self.authorizedDeveloperAssetIDs.subtract(knownBeforeScan)
            for recordID in effectiveAuthorizedIDs
                where (developerAssetAuthorizationLastMutation[recordID] ?? 0) <=
                    scanStartedAtMutationEpoch {
                self.authorizedDeveloperAssetIDs.insert(recordID)
            }
            developerAssetAuthorizationReady = true
            deferredDeveloperAssetRehydrationIDs.removeAll(keepingCapacity: false)
        }
        let pendingRecordIDs = Set(effectiveRecords.map(\.recordID))
            .union(deferredAuthorizedIDs)
        let changes = pendingRecordIDs.map { recordID in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(
                CKRecord.ID(
                    recordName: recordID.uuidString.lowercased(),
                    zoneID: zoneID
                )
            )
        }
        engine.state.add(pendingRecordZoneChanges: changes)
        guard !changes.isEmpty else { return }
        markTransportActivity()
        requestEventDrivenSyncIfUnbounded()
    }

    /// Commits local winners produced by one authenticated domain-import page in
    /// a single transport-store write. Developer-asset IDs are runtime-authorized
    /// only after the domain snapshot has already committed; this method never
    /// marks the complete launch-time authorization scan as finished.
    func commitImportedDomainResults(
        records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        revokedDeveloperAssetIDs: Set<UUID> = []
    ) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        let authorization = SyncAuthorizationContext(
            optedInDeveloperAssetIDs: authorizedDeveloperAssetIDs
        )
        for record in records {
            try bookmarkTransportAuthorization.authorize(record)
            try boundary.authorize(record, context: authorization)
        }
        let syncEngine = try activeEngine()
        try await recordStore.upsert(records)
        statusLock.withLock {
            if !authorizedDeveloperAssetIDs.isEmpty || !revokedDeveloperAssetIDs.isEmpty {
                developerAssetAuthorizationMutationEpoch &+= 1
                let epoch = developerAssetAuthorizationMutationEpoch
                for recordID in authorizedDeveloperAssetIDs.union(revokedDeveloperAssetIDs) {
                    developerAssetAuthorizationLastMutation[recordID] = epoch
                }
            }
            self.authorizedDeveloperAssetIDs.formUnion(authorizedDeveloperAssetIDs)
            self.authorizedDeveloperAssetIDs.subtract(revokedDeveloperAssetIDs)
        }
        let revokedChanges = revokedDeveloperAssetIDs.map { recordID in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(CKRecord.ID(
                recordName: recordID.uuidString.lowercased(),
                zoneID: zoneID
            ))
        }
        syncEngine.state.remove(pendingRecordZoneChanges: revokedChanges)
        let changes = records.map { record in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(CKRecord.ID(
                recordName: record.recordID.uuidString.lowercased(),
                zoneID: zoneID
            ))
        }
        syncEngine.state.add(pendingRecordZoneChanges: changes)
        guard !changes.isEmpty else { return }
        markTransportActivity()
        requestEventDrivenSyncIfUnbounded()
    }

}
#endif
