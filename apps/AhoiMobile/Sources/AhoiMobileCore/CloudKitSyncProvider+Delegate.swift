import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    public func handleEvent(
        _ event: CKSyncEngine.Event,
        syncEngine: CKSyncEngine
    ) async {
        guard beginActivity(for: syncEngine) else { return }
        defer { endActivity() }
        markTransportActivity()
        switch event {
        case let .stateUpdate(update):
            let canPersistState = statusLock.withLock {
                accountContinuityVerified && !accountTransitionPending &&
                    !statePersistenceBlocked && engine === syncEngine
            }
            guard canPersistState else {
                // A fresh CKSyncEngine may emit its initial serialization before
                // the asynchronous account proof has completed. Discarding that
                // unbound snapshot is intentional; prepare/rebuild will produce
                // another state update after continuity is verified. Treating it
                // as a persistence failure creates a permanent bootstrap loop.
                return
            }
            do {
                try stateStore.save(update.stateSerialization)
            } catch {
                markStatePersistenceFailure()
            }
        case let .accountChange(change):
            let requiresTransitionBoundary = statusLock.withLock {
                switch change.changeType {
                case let .signIn(currentUser):
                    // CKSyncEngine also reports sign-in when a fresh or
                    // deliberately rebuilt engine attaches to the already
                    // verified account. Initial binding is audited by
                    // ensureAccountContinuity(); a matching replay must not
                    // reopen the explicit account-switch confirmation gate.
                    guard let known = lastKnownAccountIdentifier else {
                        return false
                    }
                    return accountTransitionPending ||
                        known != currentUser.recordName
                case .signOut, .switchAccounts:
                    return true
                @unknown default:
                    return true
                }
            }
            guard requiresTransitionBoundary else { return }
            // CKSyncEngine resets its pending state on account changes. Keep
            // local records, clear the persisted engine token, and require a
            // new account-aware setup instead of deleting user data.
            statusLock.withLock {
                accountVerificationTask?.task.cancel()
                accountVerificationTask = nil
                accountVerificationGeneration &+= 1
                accountContinuityVerified = false
            }
            guard persistAccountTransitionBoundary() else { return }
            setStatus(.init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_changed",
                    "iCloud account changed; local data was retained"
                )
            ))
        case let .fetchedRecordZoneChanges(changes):
            guard statusLock.withLock({
                accountContinuityVerified && !accountTransitionPending &&
                    !statePersistenceBlocked && engine === syncEngine
            }) else {
                markStatePersistenceFailure()
                return
            }
            await applyFetched(changes)
            requestEventDrivenSyncIfUnbounded()
        case let .sentRecordZoneChanges(changes):
            guard statusLock.withLock({
                accountContinuityVerified && !accountTransitionPending &&
                    !statePersistenceBlocked && engine === syncEngine
            }) else {
                markStatePersistenceFailure()
                return
            }
            await applySent(changes, syncEngine: syncEngine)
        case let .didFetchRecordZoneChanges(result):
            if let error = result.error {
                if error.code == .zoneNotFound ||
                    error.code == .userDeletedZone {
                    guard persistZoneRecoveryRequirement() else { return }
                }
                setStatus(classify(error).status)
            }
        case let .didSendChanges(result):
            _ = result
        case let .fetchedDatabaseChanges(changes):
            if !changes.deletions.isEmpty {
                guard persistZoneRecoveryRequirement() else { return }
                setStatus(.init(
                    phase: .accountRequired,
                    detail: SyncText(
                        "sync.status.zone_deleted",
                        "CloudKit zone was deleted; local data was retained"
                    )
                ))
            }
        case .sentDatabaseChanges, .willFetchChanges, .willFetchRecordZoneChanges,
             .didFetchChanges, .willSendChanges:
            break
        @unknown default:
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.unknown_event",
                    "Unknown CKSyncEngine event"
                )
            ))
        }
    }

    public func nextRecordZoneChangeBatch(
        _ context: CKSyncEngine.SendChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.RecordZoneChangeBatch? {
        guard beginActivity(for: syncEngine) else { return nil }
        defer { endActivity() }
        do {
            try await ensureAccountContinuity()
        } catch {
            return nil
        }
        let pending = syncEngine.state.pendingRecordZoneChanges.filter {
            context.options.scope.contains($0)
        }
        let quarantinedIDs = Set((await quarantineStore.allQuarantined()).keys)
        let unsupportedDeletes = pending.filter {
            if case .deleteRecord = $0 { return true }
            return false
        }
        var saves: [CKSyncEngine.PendingRecordZoneChange] = []
        var permanentlyRemoved: [CKSyncEngine.PendingRecordZoneChange] = []
        for change in pending {
            guard case let .saveRecord(cloudID) = change else { continue }
            guard let recordID = UUID(uuidString: cloudID.recordName) else {
                guard await persistQuarantine(
                    recordID: Self.quarantineIdentifier(for: cloudID.recordName),
                    reason: "invalid_pending_record_identifier"
                ) else { return nil }
                permanentlyRemoved.append(change)
                continue
            }
            if quarantinedIDs.contains(recordID) {
                permanentlyRemoved.append(change)
                continue
            }
            let local: SyncRecord?
            do {
                local = try await recordStore.record(for: recordID)
            } catch {
                markStatePersistenceFailure()
                return nil
            }
            guard let local else {
                guard await persistQuarantine(
                    recordID: recordID,
                    reason: "missing_local_transport_record"
                ) else { return nil }
                permanentlyRemoved.append(change)
                continue
            }
            do {
                try authorizeOutboundRecord(local)
                _ = try codec.encode(local, zoneID: zoneID)
            } catch {
                if developerAssetAuthorizationIsPending(for: local, error: error) {
                    continue
                }
                guard await persistQuarantine(
                    recordID: recordID,
                    reason: "outbound_boundary_validation_failed"
                ) else { return nil }
                permanentlyRemoved.append(change)
                continue
            }
            saves.append(change)
        }
        if !permanentlyRemoved.isEmpty {
            syncEngine.state.remove(pendingRecordZoneChanges: permanentlyRemoved)
        }
        let gate = statusLock.withLock { () -> (passID: UInt64?, requestHostPass: Bool) in
            let safe = accountContinuityVerified &&
                !accountTransitionPending && !zoneRecoveryPending &&
                !zonePreparationInProgress
            let passID = safe && boundedSyncPassActive &&
                !boundedSyncPassOutboundBlocked && outboundBatchWindowDepth > 0
                ? boundedSyncPassID
                : nil
            return (
                passID,
                safe && !boundedSyncPassActive &&
                    (!unsupportedDeletes.isEmpty || !saves.isEmpty)
            )
        }
        let passID = gate.passID
        guard let passID else {
            if gate.requestHostPass {
                requestEventDrivenSyncIfUnbounded()
            }
            return nil
        }
        guard !unsupportedDeletes.isEmpty || !saves.isEmpty else { return nil }

        // A previous engine state must never smuggle a raw delete back into a
        // provider whose public contract is tombstone-only.
        if !unsupportedDeletes.isEmpty {
            var durableDeletes: [CKSyncEngine.PendingRecordZoneChange] = []
            for change in unsupportedDeletes {
                guard case let .deleteRecord(cloudID) = change,
                      await persistQuarantine(
                        recordID: Self.quarantineIdentifier(for: cloudID.recordName),
                        reason: "raw_pending_delete_discarded"
                      ) else { return nil }
                durableDeletes.append(change)
            }
            syncEngine.state.remove(pendingRecordZoneChanges: durableDeletes)
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.raw_delete_discarded",
                    "Raw CloudKit record deletion was discarded"
                )
            ))
            return nil
        }

        guard !saves.isEmpty else { return nil }
        return await .init(pendingChanges: saves) { [weak self, recordStore, codec, zoneID] cloudID in
            guard let self, self.beginActivity(for: syncEngine) else { return nil }
            defer { self.endActivity() }
            guard self.canProvideRecordBatch(for: passID, syncEngine: syncEngine) else {
                return nil
            }
            guard let rawID = UUID(uuidString: cloudID.recordName),
                  !(await self.quarantineStore.allQuarantined()).keys.contains(rawID) else {
                return nil
            }
            let local: SyncRecord
            do {
                guard let stored = try await recordStore.record(for: rawID) else {
                    return nil
                }
                local = stored
            } catch {
                self.markStatePersistenceFailure()
                return nil
            }
            guard self.canProvideRecordBatch(for: passID, syncEngine: syncEngine) else {
                return nil
            }
            do {
                try self.authorizeOutboundRecord(local)
            } catch {
                return nil
            }
            do {
                let baseRecord = try await self.baseRecord(
                    for: rawID,
                    expectedCloudID: cloudID
                )
                return try codec.encode(
                    local,
                    zoneID: zoneID,
                    baseRecord: baseRecord
                )
            } catch {
                self.markStatePersistenceFailure()
                return nil
            }
        }
    }

    public func nextFetchChangesOptions(
        _ context: CKSyncEngine.FetchChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.FetchChangesOptions {
        _ = context
        guard beginActivity(for: syncEngine) else {
            return .init(scope: .zoneIDs([]))
        }
        defer { endActivity() }
        do {
            try await ensureAccountContinuity()
        } catch {
            return .init(scope: .zoneIDs([]))
        }
        guard statusLock.withLock({
            accountContinuityVerified && !accountTransitionPending &&
                !zoneRecoveryPending && engine === syncEngine
        }) else { return .init(scope: .zoneIDs([])) }
        return .init(scope: .zoneIDs([zoneID]))
    }

    private func applyFetched(
        _ changes: CKSyncEngine.Event.FetchedRecordZoneChanges
    ) async {
        var resolvedConflict = false
        var validatedModifications: [(record: SyncRecord, serverRecord: CKRecord)] = []
        for modification in changes.modifications {
            let recordID = Self.quarantineIdentifier(
                for: modification.record.recordID.recordName
            )
            let incoming: SyncRecord
            do {
                incoming = try codec.decode(modification.record)
                try authorizeInboundTransportEnvelope(incoming)
            } catch {
                await persistQuarantine(
                    recordID: recordID,
                    reason: "invalid_or_denied_cloudkit_record"
                )
                continue
            }
            validatedModifications.append((incoming, modification.record))
        }
        if !validatedModifications.isEmpty {
            do {
                let retentions = try await Self.retainFetchedEnvelopes(
                    validatedModifications.map(\.record),
                    in: recordStore
                )
                try await persistSystemFields(
                    for: validatedModifications.map(\.serverRecord)
                )
                for retained in retentions {
                    await markResolutionCandidateIfQuarantined(retained.record)
                    resolvedConflict = resolvedConflict || retained.existing != nil &&
                        retained.snapshot != retained.existing
                }
            } catch {
                // Boundary-valid data hit a local persistence failure. Never
                // quarantine it as corrupt; keep the inbox and block the token.
                markStatePersistenceFailure()
            }
        }
        var physicallyDeletedRecordIDs = Set<UUID>()
        for deletion in changes.deletions {
            let recordID = Self.quarantineIdentifier(for: deletion.recordID.recordName)
            if await persistQuarantine(
                recordID: recordID,
                reason: "physical_delete_without_validated_tombstone"
            ), (await quarantineStore.entry(for: recordID))?.reason ==
                "physical_delete_without_validated_tombstone" {
                physicallyDeletedRecordIDs.insert(recordID)
            }
        }
        if !physicallyDeletedRecordIDs.isEmpty {
            do {
                // A physically deleted server row has no valid change tag. A
                // user-approved restore must therefore create it anew.
                try await systemFieldsStore.remove(
                    recordIDs: physicallyDeletedRecordIDs
                )
            } catch {
                markStatePersistenceFailure()
            }
        }
        if resolvedConflict && statusLock.withLock({
            !Self.blocksSuccessfulCompletion(currentStatus.phase)
        }) {
            setStatus(.init(
                phase: .conflictResolved,
                detail: SyncText(
                    "sync.status.conflict_merged",
                    "Conflict was merged deterministically"
                )
            ))
        }
    }

    private func applySent(
        _ changes: CKSyncEngine.Event.SentRecordZoneChanges,
        syncEngine: CKSyncEngine
    ) async {
        if !changes.savedRecords.isEmpty {
            do {
                try await persistSystemFields(for: changes.savedRecords)
            } catch {
                markStatePersistenceFailure()
            }
        }
        guard !changes.failedRecordSaves.isEmpty || !changes.failedRecordDeletes.isEmpty else {
            return
        }

        for failure in changes.failedRecordSaves {
            if failure.error.code == .serverRecordChanged,
               let serverRecord = failure.error.serverRecord {
                if await reconcileServerRecord(serverRecord, syncEngine: syncEngine) {
                    setStatus(.init(
                        phase: .conflictResolved,
                        detail: SyncText(
                            "sync.status.conflict_merged",
                            "Conflict was merged deterministically"
                        )
                    ))
                }
                // A successful reconciliation has already retained the server
                // envelope, persisted its change tag, and queued the merged
                // retry. Do not overwrite that state with a generic failure.
                continue
            }
            let classified = classify(failure.error)
            setStatus(classified.status)
            let cloudError = failure.error
            if cloudError.code == .zoneNotFound || cloudError.code == .userDeletedZone {
                guard persistZoneRecoveryRequirement() else { return }
            }
        }
        if !changes.failedRecordDeletes.isEmpty {
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.raw_deletes_forbidden",
                    "Raw CloudKit record deletions remain forbidden"
                )
            ))
        }
    }

    private func reconcileServerRecord(
        _ serverRecord: CKRecord,
        syncEngine: CKSyncEngine
    ) async -> Bool {
        let recordID = Self.quarantineIdentifier(for: serverRecord.recordID.recordName)
        let incoming: SyncRecord
        do {
            incoming = try codec.decode(serverRecord)
            try authorizeInboundTransportEnvelope(incoming)
        } catch {
            await persistQuarantine(
                recordID: recordID,
                reason: "cloudkit_conflict_record_validation_failed"
            )
            return false
        }
        do {
            _ = try await Self.retainFetchedEnvelope(incoming, in: recordStore)
            try await persistSystemFields(for: [serverRecord])
            await markResolutionCandidateIfQuarantined(incoming)
            syncEngine.state.add(pendingRecordZoneChanges: [
                .saveRecord(serverRecord.recordID)
            ])
            markTransportActivity()
            return true
        } catch {
            markStatePersistenceFailure()
            return false
        }
    }

    private func baseRecord(
        for recordID: UUID,
        expectedCloudID: CKRecord.ID
    ) async throws -> CKRecord? {
        guard let data = try await systemFieldsStore.data(for: recordID) else {
            return nil
        }
        do {
            let record = try CloudKitSystemFieldsCodec.decode(data)
            guard record.recordType == AppleCloudKitRecordCodec.recordType,
                  record.recordID == expectedCloudID else {
                throw CloudKitSystemFieldsStoreError.invalidArchive
            }
            return record
        } catch {
            // A corrupt or stale sidecar is recoverable: remove it and let the
            // server-record-changed path fetch a fresh change tag. Failure to
            // remove it remains fail-closed to avoid an endless retry loop.
            try await systemFieldsStore.remove(recordIDs: [recordID])
            return nil
        }
    }

    private func persistSystemFields(for records: [CKRecord]) async throws {
        var values: [UUID: Data] = [:]
        values.reserveCapacity(records.count)
        for record in records {
            guard record.recordType == AppleCloudKitRecordCodec.recordType,
                  record.recordID.zoneID == zoneID,
                  let recordID = UUID(uuidString: record.recordID.recordName) else {
                throw CloudKitSystemFieldsStoreError.invalidArchive
            }
            values[recordID] = CloudKitSystemFieldsCodec.encode(record)
        }
        try await systemFieldsStore.upsert(values)
    }

    @discardableResult
    func persistQuarantine(recordID: UUID, reason: String) async -> Bool {
        do {
            _ = try await quarantineStore.quarantine(recordID: recordID, reason: reason)
            _ = statusLock.withLock {
                quarantineResolutionCandidates.removeValue(forKey: recordID)
            }
            setStatus(.init(
                phase: .quarantined,
                detail: SyncText(
                    "sync.status.record_quarantined",
                    "At least one CloudKit record was quarantined"
                )
            ))
            return true
        } catch {
            // Without durable quarantine metadata, persisting the advanced
            // CKSyncEngine token would permanently hide the rejected record.
            markStatePersistenceFailure()
            return false
        }
    }

    private func markResolutionCandidateIfQuarantined(_ record: SyncRecord) async {
        guard let entry = await quarantineStore.entry(for: record.recordID),
              entry.reason != "physical_delete_without_validated_tombstone" else {
            return
        }
        statusLock.withLock {
            quarantineResolutionCandidates[record.recordID] = .init(
                record: record,
                generation: entry.generation
            )
        }
    }

    private static func quarantineIdentifier(for recordName: String) -> UUID {
        if let identifier = UUID(uuidString: recordName) { return identifier }
#if canImport(CryptoKit)
        var bytes = Array(SHA256.hash(data: Data(recordName.utf8)).prefix(16))
        bytes[6] = (bytes[6] & 0x0f) | 0x50
        bytes[8] = (bytes[8] & 0x3f) | 0x80
        return UUID(uuid: (
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5], bytes[6], bytes[7],
            bytes[8], bytes[9], bytes[10], bytes[11],
            bytes[12], bytes[13], bytes[14], bytes[15]
        ))
#else
        return UUID()
#endif
    }

}
#endif
