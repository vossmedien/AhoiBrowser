import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    public func allRecords() async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        return try await recordStore.allRecords()
    }

    /// Rehydrates a bounded caller-owned record index without depending on the
    /// global transport ordering. Missing IDs are omitted and never replaced by
    /// unrelated records.
    public func records(forRecordIDs recordIDs: [UUID]) async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        var records: [SyncRecord] = []
        for recordID in Set(recordIDs).sorted(by: {
            $0.uuidString < $1.uuidString
        }) {
            if let record = try await recordStore.record(for: recordID) {
                records.append(record)
            }
        }
        return records
    }

    /// Internal post-failure probe for the bridge's two-store enqueue. It does
    /// not require a live CKSyncEngine because it only inspects the durable
    /// encrypted record cache.
    func locallyPersistedRecord(forRecordID recordID: UUID) async throws -> SyncRecord? {
        try await recordStore.record(for: recordID)
    }

    /// Returns opaque CloudKit envelopes that still need to cross the
    /// authenticated plaintext/domain merge boundary. These are deliberately
    /// separate from the single transport snapshot used by CKSyncEngine.
    public func pendingFetchedRecords() async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        return try await recordStore.fetchedRecords()
    }

    /// Returns only durable transport snapshots that are actual quarantine
    /// recovery candidates. Normal sync passes must never replay the complete
    /// transport cache into the domain repository: every domain upsert persists
    /// a full local snapshot and rebuilding all records on every pass therefore
    /// becomes quadratic as history grows.
    public func pendingQuarantineRecoveryRecords() async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        var candidates = statusLock.withLock { quarantineResolutionCandidates }
        let quarantined = await quarantineStore.allQuarantined()
        let fetched = try await recordStore.fetchedRecords()
        let fetchedByID = Dictionary(grouping: fetched, by: \.recordID)
        for (recordID, reason) in quarantined
            where reason != "physical_delete_without_validated_tombstone" &&
                candidates[recordID] == nil {
            guard let entry = await quarantineStore.entry(for: recordID) else { continue }
            let durableCandidate: SyncRecord?
            if let fetchedCandidate = fetchedByID[recordID]?.last {
                durableCandidate = fetchedCandidate
            } else {
                durableCandidate = try await recordStore.record(for: recordID)
            }
            guard let durableCandidate else { continue }
            let candidate = QuarantineResolutionCandidate(
                record: durableCandidate,
                generation: entry.generation
            )
            candidates[recordID] = candidate
            statusLock.withLock {
                quarantineResolutionCandidates[recordID] = candidate
            }
        }
        var recoverable: [SyncRecord] = []
        for (recordID, candidate) in candidates {
            guard let entry = await quarantineStore.entry(for: recordID),
                  entry.reason != "physical_delete_without_validated_tombstone",
                  entry.generation == candidate.generation,
                  try await recordStore.record(for: recordID) == candidate.record else {
                continue
            }
            recoverable.append(candidate.record)
        }
        return recoverable
    }

    /// Removes exactly the envelope that completed domain import. A distinct
    /// version (including distinct encrypted bytes) remains pending.
    public func acknowledgeFetchedRecord(_ record: SyncRecord) async throws {
        try await acknowledgeFetchedRecords([record])
    }

    public func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        try await recordStore.acknowledgeFetchedRecords(records)
    }

    public func quarantineImportedRecord(_ record: SyncRecord, reason: String) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        let entry: SyncQuarantineEntry
        do {
            entry = try await quarantineStore.quarantine(
                recordID: record.recordID,
                reason: reason
            )
        } catch {
            markStatePersistenceFailure()
            throw error
        }
        statusLock.withLock {
            if entry.reason == "physical_delete_without_validated_tombstone" {
                quarantineResolutionCandidates.removeValue(forKey: record.recordID)
            } else {
                quarantineResolutionCandidates[record.recordID] = .init(
                    record: record,
                    generation: entry.generation
                )
            }
        }
        setStatus(.init(
            phase: .quarantined,
            detail: SyncText(
                "sync.status.record_quarantined",
                "At least one CloudKit record was quarantined"
            )
        ))
    }

    /// Rebuilds CKSyncEngine without its opaque change token, forcing a full
    /// zone refetch. Quarantine evidence is retained until that exact record is
    /// durably staged, authenticated, merged, and acknowledged successfully.
    public func retryQuarantinedRecords() async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await rebuildEngineForFullRefetch()
        setStatus(.init(
            phase: .preparing,
            detail: SyncText(
                "sync.status.quarantine_retry",
                "CloudKit is refetching quarantined records"
            )
        ))
        requestEventDrivenSyncIfUnbounded()
    }

    public func hasPhysicalDeletionQuarantine() async -> Bool {
        (await quarantineStore.allQuarantined()).values.contains(
            "physical_delete_without_validated_tombstone"
        )
    }

    /// Exposes only exact, generation-bound local envelopes for bridge-level
    /// decrypt and domain validation. The provider cannot declare its own opaque
    /// transport payload safe merely because the CKRecord metadata is valid.
    func physicalDeletionRecoveryCandidates() async throws -> [(
        record: SyncRecord,
        generation: UUID
    )] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        let quarantined = await quarantineStore.allQuarantined()
        var candidates: [(record: SyncRecord, generation: UUID)] = []
        for (recordID, reason) in quarantined
            where reason == "physical_delete_without_validated_tombstone" {
            guard let entry = await quarantineStore.entry(for: recordID),
                  entry.reason == "physical_delete_without_validated_tombstone",
                  let local = try await recordStore.record(for: recordID) else {
                continue
            }
            candidates.append((record: local, generation: entry.generation))
        }
        return candidates
    }

    /// Queues one envelope only after CompanionSyncBridge has opened and fully
    /// validated this exact generation. A concurrent store/quarantine change
    /// makes the operation a no-op rather than restoring stale bytes.
    @discardableResult
    func restorePhysicallyDeletedRecord(
        _ record: SyncRecord,
        expectedGeneration: UUID
    ) async throws -> Bool {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let canRecover = statusLock.withLock {
            !boundedSyncPassActive && !accountTransitionPending && !zoneRecoveryPending
        }
        guard canRecover else {
            throw CloudKitSyncProviderError.boundedSyncPassAlreadyActive
        }
        guard let entry = await quarantineStore.entry(for: record.recordID),
              entry.reason == "physical_delete_without_validated_tombstone",
              entry.generation == expectedGeneration,
              try await recordStore.record(for: record.recordID) == record else {
            return false
        }
        try authorizeOutboundRecord(record)
        _ = try codec.encode(record, zoneID: zoneID)
        let validatedEngine = try activeEngine()
        let cloudID = CKRecord.ID(
            recordName: record.recordID.uuidString.lowercased(),
            zoneID: zoneID
        )
        let removed = try await quarantineStore.remove(
            recordID: record.recordID,
            expectedGeneration: expectedGeneration
        )
        guard removed else { return false }
        do {
            let currentEngine = try activeEngine()
            guard currentEngine === validatedEngine else {
                throw CloudKitSyncProviderError.unavailable
            }
            // Keep automatic sync from observing a pending change while the
            // exact durable quarantine generation still blocks its payload.
            currentEngine.state.add(pendingRecordZoneChanges: [.saveRecord(cloudID)])
        } catch {
            _ = try? await quarantineStore.quarantine(
                recordID: record.recordID,
                reason: "physical_delete_without_validated_tombstone"
            )
            throw error
        }
        _ = statusLock.withLock {
            quarantineResolutionCandidates.removeValue(forKey: record.recordID)
        }
        markTransportActivity()
        let hasOtherQuarantine = !(await quarantineStore.allQuarantined()).isEmpty
        setStatus(.init(
            phase: hasOtherQuarantine ? .quarantined : .retryScheduled,
            detail: SyncText(
                "sync.status.physical_delete_restore_queued",
                "Retained local records are ready to be restored to CloudKit"
            )
        ))
        requestEventDrivenSyncIfUnbounded()
        return true
    }

    /// Clears an exact physical-delete generation after the domain bridge has
    /// proved that there is no current local value to restore. This grants no
    /// outbound authorization and queues no save.
    @discardableResult
    func acceptPhysicalDeletion(
        recordID: UUID,
        expectedGeneration: UUID
    ) async throws -> Bool {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        guard let entry = await quarantineStore.entry(for: recordID),
              entry.reason == "physical_delete_without_validated_tombstone",
              entry.generation == expectedGeneration else {
            return false
        }
        let removed = try await quarantineStore.remove(
            recordID: recordID,
            expectedGeneration: expectedGeneration
        )
        guard removed else { return false }
        statusLock.withLock {
            quarantineResolutionCandidates.removeValue(forKey: recordID)
            authorizedDeveloperAssetIDs.remove(recordID)
        }
        return true
    }

    public func resolveQuarantinedRecord(_ record: SyncRecord) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        guard let entry = await quarantineStore.entry(for: record.recordID) else { return }
        // A physical CloudKit deletion has no authenticated tombstone and can
        // never be cleared by an older inbox envelope of the same UUID.
        guard entry.reason != "physical_delete_without_validated_tombstone" else { return }
        let runtimeCandidateMatches = statusLock.withLock({
            guard let candidate = quarantineResolutionCandidates[record.recordID] else {
                return false
            }
            return candidate.record == record && candidate.generation == entry.generation
        })
        let durableInboxMatches: Bool
        if runtimeCandidateMatches {
            durableInboxMatches = true
        } else {
            durableInboxMatches = try await recordStore.fetchedRecords().contains(record)
        }
        guard durableInboxMatches else { return }
        let removed = try await quarantineStore.remove(
            recordID: record.recordID,
            expectedGeneration: entry.generation
        )
        guard removed else { return }
        _ = statusLock.withLock {
            quarantineResolutionCandidates.removeValue(forKey: record.recordID)
        }
        let quarantineIsEmpty = (await quarantineStore.allQuarantined()).isEmpty
        if quarantineIsEmpty {
            statusLock.withLock {
                if boundedSyncPassActive {
                    boundedSyncPassCompletionBlocked = false
                }
                if currentStatus.phase == .quarantined {
                    currentStatus = .init(
                        phase: boundedSyncPassActive ? .syncing : .idle,
                        detail: SyncText(
                            "sync.status.quarantine_resolved",
                            "Quarantined record was validated"
                        )
                    )
                }
                activityEpoch &+= 1
            }
        }
    }

    /// Aborts a bridge-owned pass after a domain import failure. Pending
    /// envelopes and record changes remain durable for the next retry.
    public func abortBoundedSyncPass(_ passID: UInt64) {
        let ownedPassID = statusLock.withLock { () -> UInt64? in
            guard boundedSyncPassActive, boundedSyncPassID == passID else { return nil }
            if !Self.blocksSuccessfulCompletion(currentStatus.phase) {
                boundedSyncPassCompletionBlocked = true
                boundedSyncPassOutboundBlocked = true
                currentStatus = .init(
                    phase: .failed,
                    detail: SyncText(
                        "sync.status.domain_merge_failed",
                        "The local sync merge did not complete"
                    )
                )
                activityEpoch &+= 1
            }
            return boundedSyncPassID
        }
        if let ownedPassID { finishBoundedSyncPass(ownedPassID) }
    }

}
#endif
