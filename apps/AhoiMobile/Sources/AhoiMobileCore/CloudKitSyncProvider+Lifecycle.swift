import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    func markStatePersistenceFailure() {
        statusLock.withLock { statePersistenceBlocked = true }
        setStatus(.init(
            phase: .failed,
            detail: SyncText(
                "sync.status.state_persistence_failed",
                "CloudKit progress was not committed; a full refetch is required"
            )
        ))
    }

    struct ClassifiedError {
        let status: CloudKitSyncStatus
    }

    func classify(_ error: Error) -> ClassifiedError {
        guard let cloudError = error as? CKError else {
            return .init(status: .init(phase: .failed, detail: error.localizedDescription))
        }
        let retryAfter = cloudError.retryAfterSeconds
        switch cloudError.code {
        case .notAuthenticated, .permissionFailure:
            return .init(status: .init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_required",
                    "iCloud sign-in or permission is required"
                ),
                retryAfterSeconds: retryAfter
            ))
        case .networkUnavailable, .networkFailure:
            return .init(status: .init(
                phase: .offline,
                detail: SyncText(
                    "sync.status.offline",
                    "CloudKit is offline; local changes remain pending"
                ),
                retryAfterSeconds: retryAfter
            ))
        case .requestRateLimited, .serviceUnavailable, .zoneBusy, .limitExceeded:
            return .init(status: .init(
                phase: .retryScheduled,
                detail: SyncText(
                    "sync.status.temporary_limit",
                    "CloudKit reported a temporary limit"
                ),
                retryAfterSeconds: retryAfter
            ))
        case .changeTokenExpired, .zoneNotFound, .userDeletedZone:
            return .init(status: .init(
                phase: .retryScheduled,
                detail: SyncText(
                    "sync.status.zone_reinitialization",
                    "CloudKit zone requires reinitialization"
                ),
                retryAfterSeconds: retryAfter
            ))
        default:
            return .init(status: .init(
                phase: .failed,
                detail: CompanionL10n.format(
                    "sync.status.error_code",
                    fallback: "CloudKit error: %d",
                    cloudError.code.rawValue
                ),
                retryAfterSeconds: retryAfter
            ))
        }
    }

    func beginActivity() -> Bool {
        statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress else { return false }
            activeActivityCount += 1
            activityEpoch &+= 1
            return true
        }
    }

    func beginActivity(for syncEngine: CKSyncEngine) -> Bool {
        statusLock.withLock {
            guard !isInvalidated,
                  !engineReplacementInProgress,
                  engine === syncEngine else {
                return false
            }
            activeActivityCount += 1
            activityEpoch &+= 1
            return true
        }
    }

    func endActivity() {
        let waiters = statusLock.withLock { () -> [CheckedContinuation<Void, Never>] in
            precondition(activeActivityCount > 0)
            activeActivityCount -= 1
            activityEpoch &+= 1
            guard isInvalidated, activeActivityCount == 0 else { return [] }
            let completed = activityDrainWaiters
            activityDrainWaiters.removeAll()
            return completed
        }
        waiters.forEach { $0.resume() }
    }

    func waitForActivityDrain() async {
        await withCheckedContinuation { continuation in
            let resumeImmediately = statusLock.withLock { () -> Bool in
                if activeActivityCount == 0 { return true }
                activityDrainWaiters.append(continuation)
                return false
            }
            if resumeImmediately { continuation.resume() }
        }
    }

    func markTransportActivity() {
        statusLock.withLock { activityEpoch &+= 1 }
    }

    func beginBoundedSyncPass(persistentlyBlocked: Bool) throws -> UInt64 {
        try statusLock.withLock {
            guard !isInvalidated else {
                throw CloudKitSyncProviderError.unavailable
            }
            guard !boundedSyncPassActive else {
                throw CloudKitSyncProviderError.boundedSyncPassAlreadyActive
            }
            boundedSyncPassID &+= 1
            boundedSyncPassActive = true
            boundedSyncPassCompletionBlocked = persistentlyBlocked
            boundedSyncPassOutboundBlocked = false
            outboundBatchWindowDepth = 0
            activityEpoch &+= 1
            if persistentlyBlocked {
                currentStatus = .init(
                    phase: .quarantined,
                    detail: SyncText(
                        "sync.status.record_quarantined",
                        "At least one CloudKit record was quarantined"
                    )
                )
            } else {
                currentStatus = .init(
                    phase: .syncing,
                    detail: SyncText("sync.status.syncing", "Syncing with CloudKit")
                )
            }
            return boundedSyncPassID
        }
    }

    func finishBoundedSyncPass(_ passID: UInt64) {
        statusLock.withLock {
            guard boundedSyncPassActive, boundedSyncPassID == passID else { return }
            boundedSyncPassActive = false
            outboundBatchWindowDepth = 0
            activityEpoch &+= 1
        }
    }

    func beginOutboundBatchWindow(passID: UInt64) -> UInt64? {
        statusLock.withLock {
            guard !isInvalidated,
                  boundedSyncPassActive,
                  boundedSyncPassID == passID,
                  accountContinuityVerified,
                  !accountTransitionPending,
                  !boundedSyncPassOutboundBlocked else {
                return nil
            }
            outboundBatchWindowDepth += 1
            activityEpoch &+= 1
            if !boundedSyncPassCompletionBlocked {
                currentStatus = .init(
                    phase: .syncing,
                    detail: SyncText(
                        "sync.status.sending_merged_changes",
                        "Sending merged changes"
                    )
                )
            }
            return boundedSyncPassID
        }
    }

    func endOutboundBatchWindow(_ passID: UInt64) {
        statusLock.withLock {
            guard boundedSyncPassID == passID, outboundBatchWindowDepth > 0 else { return }
            outboundBatchWindowDepth -= 1
            activityEpoch &+= 1
        }
    }

    func canProvideRecordBatch(
        for passID: UInt64,
        syncEngine: CKSyncEngine
    ) -> Bool {
        statusLock.withLock {
            !isInvalidated && !engineReplacementInProgress &&
                accountContinuityVerified && !accountTransitionPending &&
                engine === syncEngine && boundedSyncPassActive &&
                boundedSyncPassID == passID && !boundedSyncPassOutboundBlocked &&
                outboundBatchWindowDepth > 0
        }
    }

    func requestEventDrivenSyncIfUnbounded() {
        let handler = statusLock.withLock { () -> (@Sendable () -> Void)? in
            guard !isInvalidated, !boundedSyncPassActive else { return nil }
            return eventDrivenSyncHandler
        }
        handler?()
    }

    func setRetryScheduledUnlessBlocked() {
        statusLock.withLock {
            guard !isInvalidated,
                  !boundedSyncPassCompletionBlocked,
                  !Self.blocksSuccessfulCompletion(currentStatus.phase) else {
                return
            }
            boundedSyncPassCompletionBlocked = true
            boundedSyncPassOutboundBlocked = true
            currentStatus = .init(
                phase: .retryScheduled,
                detail: SyncText(
                    "sync.status.pending_after_bounded_pass",
                    "Changes remain pending; another sync is required"
                )
            )
            activityEpoch &+= 1
        }
    }

    @discardableResult
    func setSyncedIfActivityUnchanged(
        _ expectedActivityEpoch: UInt64,
        passID: UInt64,
        observedPhase: CloudKitSyncPhase,
        observedBlocked: Bool
    ) -> Bool {
        statusLock.withLock {
            guard !isInvalidated,
                  activeActivityCount == 1,
                  activityEpoch == expectedActivityEpoch,
                  boundedSyncPassActive,
                  boundedSyncPassID == passID,
                  boundedSyncPassCompletionBlocked == observedBlocked,
                  !boundedSyncPassCompletionBlocked,
                  currentStatus.phase == observedPhase,
                  !Self.blocksSuccessfulCompletion(currentStatus.phase) else {
                return false
            }
            currentStatus = .init(
                phase: .idle,
                detail: SyncText("sync.status.synced", "Synced")
            )
            return true
        }
    }

    static func blocksSuccessfulCompletion(
        _ phase: CloudKitSyncPhase
    ) -> Bool {
        switch phase {
        case .offline, .accountRequired, .retryScheduled, .quarantined, .failed:
            return true
        case .idle, .preparing, .syncing, .conflictResolved:
            return false
        }
    }

    static func blocksOutboundTransport(_ phase: CloudKitSyncPhase) -> Bool {
        switch phase {
        case .offline, .accountRequired, .retryScheduled, .failed:
            return true
        case .idle, .preparing, .syncing, .conflictResolved, .quarantined:
            return false
        }
    }

    func setStatus(_ status: CloudKitSyncStatus) {
        statusLock.withLock {
            if Self.blocksSuccessfulCompletion(status.phase), boundedSyncPassActive {
                boundedSyncPassCompletionBlocked = true
                if Self.blocksOutboundTransport(status.phase) {
                    boundedSyncPassOutboundBlocked = true
                }
            }
            guard !(boundedSyncPassActive && boundedSyncPassCompletionBlocked &&
                    !Self.blocksSuccessfulCompletion(status.phase)) else {
                return
            }
            currentStatus = status
            activityEpoch &+= 1
        }
    }

    func activeEngine() throws -> CKSyncEngine {
        try statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress, let engine else {
                throw CloudKitSyncProviderError.unavailable
            }
            return engine
        }
    }

    func updateSafetyState(
        accountTransitionPending account: Bool? = nil,
        zoneRecoveryPending zone: Bool? = nil,
        lastKnownAccountIdentifier accountIdentifier: String? = nil
    ) throws {
        try statusLock.withLock {
            if let account {
                accountTransitionPending = account
                if account { accountContinuityVerified = false }
            }
            if let zone { zoneRecoveryPending = zone }
            if let accountIdentifier { lastKnownAccountIdentifier = accountIdentifier }
            do {
                try stateStore.saveSafetyState(.init(
                    accountTransitionPending: accountTransitionPending,
                    zoneRecoveryPending: zoneRecoveryPending,
                    lastKnownAccountIdentifier: lastKnownAccountIdentifier
                ))
            } catch {
                accountTransitionPending = true
                zoneRecoveryPending = true
                try? stateStore.saveSafetyState(.init(
                    accountTransitionPending: true,
                    zoneRecoveryPending: true,
                    lastKnownAccountIdentifier: lastKnownAccountIdentifier
                ))
                throw error
            }
        }
    }
}
#endif
