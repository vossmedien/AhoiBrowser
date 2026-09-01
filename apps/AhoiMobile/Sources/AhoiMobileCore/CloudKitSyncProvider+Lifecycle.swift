import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
struct CloudKitPersistenceDecision {
    let primaryError: (any Error)?
    let fallbackAttempted: Bool
    let fallbackError: (any Error)?

    var primaryCommitted: Bool { primaryError == nil }
    var fallbackCommitted: Bool { fallbackAttempted && fallbackError == nil }
    var requiresTransportBlock: Bool { primaryError != nil }
}

/// Provider-independent persistence seam. Tests can inject storage failures
/// here without constructing CKContainer or CKSyncEngine, while production
/// paths consume the same explicit decision before advancing transport state.
@available(iOS 17.0, macOS 14.0, *)
enum CloudKitStatePersistence {
    static func persistSafetyState(
        _ desiredState: CloudKitSyncSafetyState,
        failClosedState: CloudKitSyncSafetyState,
        in store: any SyncEngineStateStore
    ) -> CloudKitPersistenceDecision {
        do {
            try store.saveSafetyState(desiredState)
            return .init(
                primaryError: nil,
                fallbackAttempted: false,
                fallbackError: nil
            )
        } catch let primaryError {
            do {
                try store.saveSafetyState(failClosedState)
                return .init(
                    primaryError: primaryError,
                    fallbackAttempted: true,
                    fallbackError: nil
                )
            } catch let fallbackError {
                return .init(
                    primaryError: primaryError,
                    fallbackAttempted: true,
                    fallbackError: fallbackError
                )
            }
        }
    }

    static func clearEngineState(
        in store: any SyncEngineStateStore
    ) -> CloudKitPersistenceDecision {
        do {
            try store.clear()
            return .init(
                primaryError: nil,
                fallbackAttempted: false,
                fallbackError: nil
            )
        } catch {
            return .init(
                primaryError: error,
                fallbackAttempted: false,
                fallbackError: nil
            )
        }
    }
}

@available(iOS 17.0, macOS 14.0, *)
struct CloudKitAccountTransitionPlan {
    let accountTransitionPending: Bool
    let shouldRebuildTransport: Bool
    let declinedStatus: CloudKitSyncStatus?
}

/// Pure policy seam for account, zone, and retry decisions. Production and
/// entitlement-free tests consume the same result without constructing a
/// CKContainer or touching a live iCloud account.
@available(iOS 17.0, macOS 14.0, *)
enum CloudKitRecoveryPolicy {
    static func accountTransitionPlan(
        allowLocalUpload: Bool
    ) -> CloudKitAccountTransitionPlan {
        guard allowLocalUpload else {
            return .init(
                accountTransitionPending: true,
                shouldRebuildTransport: false,
                declinedStatus: .init(
                    phase: .accountRequired,
                    detail: SyncText(
                        "sync.status.account_upload_not_approved",
                        "Local data remains isolated until upload is approved"
                    )
                )
            )
        }
        return .init(
            accountTransitionPending: false,
            shouldRebuildTransport: true,
            declinedStatus: nil
        )
    }

    static func validateZoneRecovery(
        accountTransitionPending: Bool
    ) throws {
        guard !accountTransitionPending else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
    }

    static func status(for error: Error) -> CloudKitSyncStatus {
        guard let cloudError = cloudErrorComponents(for: error) else {
            return .init(phase: .failed, detail: error.localizedDescription)
        }
        switch cloudError.code {
        case .notAuthenticated, .permissionFailure:
            return .init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_required",
                    "iCloud sign-in or permission is required"
                ),
                retryAfterSeconds: cloudError.retryAfterSeconds
            )
        case .networkUnavailable, .networkFailure:
            return .init(
                phase: .offline,
                detail: SyncText(
                    "sync.status.offline",
                    "CloudKit is offline; local changes remain pending"
                ),
                retryAfterSeconds: cloudError.retryAfterSeconds
            )
        case .requestRateLimited, .serviceUnavailable, .zoneBusy, .limitExceeded:
            return .init(
                phase: .retryScheduled,
                detail: SyncText(
                    "sync.status.temporary_limit",
                    "CloudKit reported a temporary limit"
                ),
                retryAfterSeconds: cloudError.retryAfterSeconds
            )
        case .changeTokenExpired, .zoneNotFound, .userDeletedZone:
            return .init(
                phase: .retryScheduled,
                detail: SyncText(
                    "sync.status.zone_reinitialization",
                    "CloudKit zone requires reinitialization"
                ),
                retryAfterSeconds: cloudError.retryAfterSeconds
            )
        default:
            return .init(
                phase: .failed,
                detail: CompanionL10n.format(
                    "sync.status.error_code",
                    fallback: "CloudKit error: %d",
                    cloudError.code.rawValue
                ),
                retryAfterSeconds: cloudError.retryAfterSeconds
            )
        }
    }

    private static func cloudErrorComponents(
        for error: Error
    ) -> (code: CKError.Code, retryAfterSeconds: Double?)? {
        if let cloudError = error as? CKError {
            return (cloudError.code, cloudError.retryAfterSeconds)
        }
        let nsError = error as NSError
        guard nsError.domain == CKErrorDomain,
              let code = CKError.Code(rawValue: nsError.code) else {
            return nil
        }
        let retryAfter = (nsError.userInfo[CKErrorRetryAfterKey] as? NSNumber)?.doubleValue
        return (code, retryAfter)
    }
}

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider {
    func markStatePersistenceFailure() {
        statusLock.withLock {
            statePersistenceBlocked = true
            accountContinuityVerified = false
        }
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
        .init(status: CloudKitRecoveryPolicy.status(for: error))
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
        let decision = statusLock.withLock { () -> CloudKitPersistenceDecision in
            if let account {
                accountTransitionPending = account
                if account { accountContinuityVerified = false }
            }
            if let zone { zoneRecoveryPending = zone }
            if let accountIdentifier { lastKnownAccountIdentifier = accountIdentifier }
            let desiredState = CloudKitSyncSafetyState(
                accountTransitionPending: accountTransitionPending,
                zoneRecoveryPending: zoneRecoveryPending,
                lastKnownAccountIdentifier: lastKnownAccountIdentifier
            )
            let failClosedState = CloudKitSyncSafetyState(
                accountTransitionPending: true,
                zoneRecoveryPending: true,
                lastKnownAccountIdentifier: lastKnownAccountIdentifier
            )
            let decision = CloudKitStatePersistence.persistSafetyState(
                desiredState,
                failClosedState: failClosedState,
                in: stateStore
            )
            if decision.requiresTransportBlock {
                accountTransitionPending = true
                accountContinuityVerified = false
                zoneRecoveryPending = true
            }
            return decision
        }
        guard let primaryError = decision.primaryError else { return }
        markStatePersistenceFailure()
        throw primaryError
    }
}
#endif
