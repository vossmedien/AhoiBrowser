import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    /// Account switches are a privacy boundary: records from the previous
    /// account remain local but are not uploaded to the new account until the
    /// product obtains explicit confirmation.
    public func confirmAccountTransition(allowLocalUpload: Bool) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        let currentAccountIdentifier = try await container.userRecordID().recordName
        try updateSafetyState(
            accountTransitionPending: !allowLocalUpload,
            lastKnownAccountIdentifier: currentAccountIdentifier
        )
        guard allowLocalUpload else {
            setStatus(.init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_upload_not_approved",
                    "Local data remains isolated until upload is approved"
                )
            ))
            return
        }
        do {
            // A confirmed account switch must not reuse the previous account's
            // serialized engine tokens, pending-change history, or server
            // change tags.
            do {
                try await systemFieldsStore.clear()
            } catch {
                markStatePersistenceFailure()
                throw error
            }
            try await rebuildEngineForFullRefetch()
            try await prepare()
        } catch {
            try? updateSafetyState(accountTransitionPending: true)
            throw error
        }
        setStatus(.init(phase: .idle, detail: SyncText(
            "sync.status.account_change_confirmed", "Account change confirmed"
        )))
    }

    /// Recreates a missing/purged custom zone only after explicit recovery.
    /// Local records are retained and requeued; no physical delete is issued.
    public func confirmZoneRecovery() async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        let engine = try activeEngine()
        do {
            try await systemFieldsStore.clear()
        } catch {
            markStatePersistenceFailure()
            throw error
        }
        engine.state.add(pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))])
        try await engine.sendChanges()
        statusLock.withLock { transportRehydrationRequired = true }
        try await rehydrateTransportIfRequired(using: engine)
        _ = try activeEngine()
        try updateSafetyState(zoneRecoveryPending: false)
        setStatus(.init(phase: .idle, detail: SyncText(
            "sync.status.zone_restored", "Sync zone restored"
        )))
    }

    public func pendingRecordCount() -> Int {
        statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress, let engine else { return 0 }
            return engine.state.pendingRecordZoneChanges.count
        }
    }

    public func cancel() async {
        let engineToCancel = statusLock.withLock { () -> CKSyncEngine? in
            if isInvalidated { return nil }
            isInvalidated = true
            accountVerificationTask?.task.cancel()
            accountVerificationTask = nil
            accountVerificationGeneration &+= 1
            accountContinuityVerified = false
            eventDrivenSyncHandler = nil
            outboundBatchWindowDepth = 0
            return engine
        }
        await engineToCancel?.cancelOperations()
        await waitForActivityDrain()
        // Existing delegate/record-provider activities may have queued work
        // after the first cancellation. Once the drain completes, no new
        // activity can begin because invalidation is already visible.
        let finalEngine = statusLock.withLock { engine }
        await finalEngine?.cancelOperations()
    }

    private struct AccountVerificationTicket {
        let verificationGeneration: UInt64
        let engineGeneration: UInt64
        let engine: CKSyncEngine
        let needsInitialBindingAudit: Bool
    }

    /// Verifies the opaque CloudKit account identity before any fetch or
    /// upload. Every proof is bound to both a monotonic verification ticket and
    /// the exact CKSyncEngine generation. An older async completion can therefore
    /// never reopen a gate that a newer account check or engine replacement has
    /// already closed.
    func ensureAccountContinuity() async throws {
        let verification = try statusLock.withLock { () -> (
            generation: UInt64,
            engineGeneration: UInt64,
            engine: CKSyncEngine,
            task: Task<Void, Error>
        ) in
            guard !isInvalidated, !engineReplacementInProgress, let engine else {
                throw CloudKitSyncProviderError.unavailable
            }
            if let active = accountVerificationTask,
               active.engineGeneration == engineGeneration,
               active.engine === engine {
                return (
                    active.verificationGeneration,
                    active.engineGeneration,
                    active.engine,
                    active.task
                )
            }
            accountVerificationGeneration &+= 1
            accountContinuityVerified = false
            let ticket = AccountVerificationTicket(
                verificationGeneration: accountVerificationGeneration,
                engineGeneration: engineGeneration,
                engine: engine,
                needsInitialBindingAudit: lastKnownAccountIdentifier == nil
            )
            let task = Task { [weak self] in
                guard let self else {
                    throw CloudKitSyncProviderError.unavailable
                }
                try Task.checkCancellation()
                try await self.performAccountContinuityVerification(ticket)
            }
            accountVerificationTask = (
                verificationGeneration: ticket.verificationGeneration,
                engineGeneration: ticket.engineGeneration,
                engine: ticket.engine,
                task: task
            )
            return (
                ticket.verificationGeneration,
                ticket.engineGeneration,
                ticket.engine,
                task
            )
        }
        do {
            try await verification.task.value
        } catch {
            statusLock.withLock {
                if accountVerificationTask?.verificationGeneration ==
                    verification.generation {
                    accountVerificationTask = nil
                }
            }
            throw error
        }
        try statusLock.withLock {
            guard !isInvalidated,
                  !engineReplacementInProgress,
                  accountVerificationGeneration == verification.generation,
                  engineGeneration == verification.engineGeneration,
                  engine === verification.engine,
                  accountContinuityVerified else {
                throw CloudKitSyncProviderError.unavailable
            }
            if accountVerificationTask?.verificationGeneration == verification.generation {
                accountVerificationTask = nil
            } else if accountVerificationTask != nil {
                // A newer verification has already closed the gate. An older
                // waiter may not return success into prepare/fetch/send.
                throw CloudKitSyncProviderError.unavailable
            }
        }
    }

    private func performAccountContinuityVerification(
        _ ticket: AccountVerificationTicket
    ) async throws {
        let currentIdentifier: String
        do {
            currentIdentifier = try await container.userRecordID().recordName
        } catch {
            let isCurrent = statusLock.withLock {
                accountVerificationGeneration == ticket.verificationGeneration &&
                    engineGeneration == ticket.engineGeneration &&
                    engine === ticket.engine
            }
            if isCurrent { setStatus(classify(error).status) }
            throw error
        }

        let hasUnboundPersistentState: Bool
        if ticket.needsInitialBindingAudit {
            do {
                let hasEngineState = try stateStore.load() != nil
                let hasRecords = !(try await recordStore.allRecords()).isEmpty
                let hasFetchedInbox = !(try await recordStore.fetchedRecords()).isEmpty
                hasUnboundPersistentState = hasEngineState || hasRecords || hasFetchedInbox
            } catch {
                markStatePersistenceFailure()
                throw error
            }
        } else {
            hasUnboundPersistentState = false
        }

        let accountMismatch = try statusLock.withLock { () -> Bool in
            guard !isInvalidated, !engineReplacementInProgress,
                  accountVerificationGeneration == ticket.verificationGeneration,
                  engineGeneration == ticket.engineGeneration,
                  engine === ticket.engine else {
                throw CloudKitSyncProviderError.unavailable
            }
            if let known = lastKnownAccountIdentifier, known != currentIdentifier {
                accountTransitionPending = true
                accountContinuityVerified = false
                do {
                    try stateStore.saveSafetyState(.init(
                        accountTransitionPending: true,
                        zoneRecoveryPending: zoneRecoveryPending,
                        lastKnownAccountIdentifier: known
                    ))
                } catch {
                    // A failed privacy-sidecar write is itself fail-closed.
                    zoneRecoveryPending = true
                    try? stateStore.saveSafetyState(.init(
                        accountTransitionPending: true,
                        zoneRecoveryPending: true,
                        lastKnownAccountIdentifier: known
                    ))
                    throw error
                }
                return true
            }
            if lastKnownAccountIdentifier == nil {
                if hasUnboundPersistentState {
                    accountTransitionPending = true
                    accountContinuityVerified = false
                    do {
                        try stateStore.saveSafetyState(.init(
                            accountTransitionPending: true,
                            zoneRecoveryPending: zoneRecoveryPending,
                            lastKnownAccountIdentifier: nil
                        ))
                    } catch {
                        zoneRecoveryPending = true
                        try? stateStore.saveSafetyState(.init(
                            accountTransitionPending: true,
                            zoneRecoveryPending: true,
                            lastKnownAccountIdentifier: nil
                        ))
                        throw error
                    }
                    return true
                }
                do {
                    try stateStore.saveSafetyState(.init(
                        accountTransitionPending: accountTransitionPending,
                        zoneRecoveryPending: zoneRecoveryPending,
                        lastKnownAccountIdentifier: currentIdentifier
                    ))
                    lastKnownAccountIdentifier = currentIdentifier
                } catch {
                    accountTransitionPending = true
                    zoneRecoveryPending = true
                    try? stateStore.saveSafetyState(.init(
                        accountTransitionPending: true,
                        zoneRecoveryPending: true,
                        lastKnownAccountIdentifier: nil
                    ))
                    throw error
                }
            }
            accountContinuityVerified = !accountTransitionPending
            return accountTransitionPending
        }
        if accountMismatch {
            setStatus(.init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_changed",
                    "iCloud account changed; local data was retained"
                )
            ))
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
    }

    func rebuildEngineForFullRefetch() async throws {
        try await ensureAccountContinuity()
        let previousEngine = try statusLock.withLock { () -> CKSyncEngine in
            guard !isInvalidated,
                  !engineReplacementInProgress,
                  !boundedSyncPassActive,
                  activeActivityCount == 1,
                  let engine else {
                throw CloudKitSyncProviderError.boundedSyncPassAlreadyActive
            }
            engineReplacementInProgress = true
            statePersistenceBlocked = true
            return engine
        }

        var replacementInstalled = false
        defer {
            if !replacementInstalled {
                statusLock.withLock { engineReplacementInProgress = false }
            }
        }

        await previousEngine.cancelOperations()
        do {
            try stateStore.clear()
        } catch {
            markStatePersistenceFailure()
            throw error
        }

        var replacementConfiguration = CKSyncEngine.Configuration(
            database: database,
            stateSerialization: nil,
            delegate: self
        )
        replacementConfiguration.automaticallySync = configuration.automaticallySync
        replacementConfiguration.subscriptionID = configuration.subscriptionID
        let replacementEngine = CKSyncEngine(replacementConfiguration)
        try statusLock.withLock {
            guard !isInvalidated,
                  engineReplacementInProgress,
                  engine === previousEngine,
                  !boundedSyncPassActive,
                  activeActivityCount == 1 else {
                throw CloudKitSyncProviderError.unavailable
            }
            engine = replacementEngine
            accountVerificationTask?.task.cancel()
            accountVerificationTask = nil
            engineGeneration &+= 1
            accountVerificationGeneration &+= 1
            engineReplacementInProgress = false
            statePersistenceBlocked = false
            transportRehydrationRequired = true
            accountContinuityVerified = false
            activityEpoch &+= 1
        }
        replacementInstalled = true
        // Bind a fresh proof to the replacement engine itself. Reusing the old
        // engine's successful account await would reopen the exact race this
        // generation boundary is designed to prevent.
        try await ensureAccountContinuity()
    }

}
#endif
