import CoreFoundation
import Foundation
import AhoiCloudKitSpike

#if canImport(CloudKit)
import CloudKit

public enum CloudKitKeyBootstrapPhase: String, Equatable, Sendable {
    case accountCheck = "account-check"
    case fetch
    case zoneCreate = "zone-create"
    case claimSend = "claim-send"
}

public struct CloudKitKeyBootstrapFailure: Equatable, Sendable {
    public let phase: CloudKitKeyBootstrapPhase
    public let code: Int
    public let leafCodes: [Int]

    public init(phase: CloudKitKeyBootstrapPhase, code: Int, leafCodes: [Int] = []) {
        self.phase = phase
        self.code = code
        self.leafCodes = Array(Set(leafCodes).sorted().prefix(8))
    }

    public var isAccountOrPermissionFailure: Bool {
        let accessCodes = Set([
            CKError.notAuthenticated.rawValue,
            CKError.permissionFailure.rawValue,
        ])
        if accessCodes.contains(code) { return true }
        return code == CKError.partialFailure.rawValue &&
            !leafCodes.isEmpty && leafCodes.allSatisfy(accessCodes.contains)
    }
}

public enum CloudKitKeyBootstrapError: Error, Equatable, Sendable {
    case invalidConfiguration
    case accountChanged
    case cloudKit(CloudKitKeyBootstrapFailure)
    case localAuthorizationUnavailable
    case corruptClaim
    case conflictingClaims
    case receiptPersistenceFailed
    case missingSendResult
}

@available(iOS 17.0, macOS 14.0, *)
public actor CloudKitKeyBootstrapTransport:
    CompanionKeyBootstrapTransport,
    CKSyncEngineDelegate {
    public static let claimRecordType = "AhoiKeyBootstrapClaim"
    public static let claimRecordName = "payload-key-bootstrap-v1"
    public static let keyVersionField = "keyVersion"
    public static let keySHA256Field = "keySHA256"

    private let containerIdentifier: String
    private let zoneID: CKRecordZone.ID
    private let subscriptionID: String?
    private var engine: CKSyncEngine?
    private var fetchedClaim: CompanionBootstrapClaim?
    private var fetchedDomainRecords = false
    private var fetchError: CloudKitKeyBootstrapError?
    private var zoneExists = true
    private var outboundClaim: CKRecord?
    private var acceptedHandler: (
        @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    )?
    private var sentReceipt: CompanionBootstrapClaimReceipt?
    private var existingClaim: CompanionBootstrapClaim?
    private var sendError: CloudKitKeyBootstrapError?
    private var zoneSaveError: CloudKitKeyBootstrapError?
    private var isShutdown = false
    private var boundAccountRecordName: String?
    private var accountTransitionInvalidated = false
    private let authorization: @Sendable () -> Bool

    public init(containerIdentifier: String, zoneName: String,
                subscriptionID: String? = nil,
                authorization: @escaping @Sendable () -> Bool = { true }) throws {
        guard containerIdentifier.hasPrefix("iCloud."),
              containerIdentifier.count > "iCloud.".count,
              !zoneName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw CloudKitKeyBootstrapError.invalidConfiguration
        }
        self.containerIdentifier = containerIdentifier
        self.subscriptionID = subscriptionID
        self.authorization = authorization
        self.zoneID = CKRecordZone.ID(
            zoneName: zoneName,
            ownerName: CKCurrentUserDefaultName
        )
    }

    public func inspectRemote() async throws -> CompanionBootstrapRemoteSnapshot {
        try requireActiveContinuity()
        try await verifyAccountContinuity()
        // This API promises a full snapshot. Clearing fetchedClaim while
        // reusing an incremental CKSyncEngine token could report false emptiness.
        if let oldEngine = engine {
            await oldEngine.cancelOperations()
            if engine === oldEngine { engine = nil }
        }
        try requireActiveContinuity()
        let engine = makeEngineIfRequired()
        fetchedClaim = nil
        fetchedDomainRecords = false
        fetchError = nil
        zoneExists = true
        do {
            try await engine.fetchChanges(.init(scope: .zoneIDs([zoneID])))
        } catch let cloudError as CKError {
            if let fetchError { throw fetchError }
            if isConfirmedMissingRequestedZone(cloudError) {
                zoneExists = false
            } else {
                throw mapCloudKitError(cloudError, phase: .fetch)
            }
        }
        try requireActiveContinuity()
        if let fetchError { throw fetchError }
        try await verifyAccountContinuity()
        return .init(
            zoneExists: zoneExists,
            claim: fetchedClaim,
            hasEncryptedDomainRecords: fetchedDomainRecords
        )
    }

    public func ensureZone() async throws {
        try requireActiveContinuity()
        try await verifyAccountContinuity()
        let engine = makeEngineIfRequired()
        zoneSaveError = nil
        engine.state.add(
            pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))]
        )
        do {
            try await engine.sendChanges()
        } catch let cloudError as CKError {
            if let zoneSaveError { throw zoneSaveError }
            throw mapCloudKitError(cloudError, phase: .zoneCreate)
        }
        try requireActiveContinuity()
        if let zoneSaveError { throw zoneSaveError }
        try await verifyAccountContinuity()
    }

    public func createClaim(
        keyVersion: UInt32,
        keySHA256: String,
        accepted: @escaping @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    ) async throws -> CompanionBootstrapClaimResult {
        try requireActiveContinuity()
        guard keyVersion > 0,
              CompanionBootstrapClaim(keyVersion: keyVersion, serverChangeTag: "pending",
                                      keySHA256: keySHA256).hasKeyCommitment else {
            throw CloudKitKeyBootstrapError.invalidConfiguration
        }
        try await verifyAccountContinuity()
        let engine = makeEngineIfRequired()
        let recordID = claimRecordID
        let record = CKRecord(recordType: Self.claimRecordType, recordID: recordID)
        record[Self.keyVersionField] = NSNumber(value: keyVersion)
        record[Self.keySHA256Field] = keySHA256 as CKRecordValue
        outboundClaim = record
        acceptedHandler = accepted
        sentReceipt = nil
        existingClaim = nil
        sendError = nil
        engine.state.add(pendingRecordZoneChanges: [.saveRecord(recordID)])
        defer {
            engine.state.remove(pendingRecordZoneChanges: [.saveRecord(recordID)])
            outboundClaim = nil
            acceptedHandler = nil
        }
        do {
            try await engine.sendChanges(.init(scope: .recordIDs([recordID])))
        } catch let cloudError as CKError {
            if let sendError { throw sendError }
            throw mapCloudKitError(cloudError, phase: .claimSend)
        }
        try requireActiveContinuity()
        if let sendError { throw sendError }
        try await verifyAccountContinuity()
        if let sentReceipt { return .created(sentReceipt) }
        if let existingClaim { return .existing(existingClaim) }
        return .indeterminate
    }

    public func shutdown() async {
        isShutdown = true
        let active = engine
        engine = nil
        await active?.cancelOperations()
        clearOperationState()
    }

    public func verifiedClaim() -> CompanionBootstrapClaim? {
        authorization() && !isShutdown && !accountTransitionInvalidated
            ? fetchedClaim
            : nil
    }

    public func handleEvent(
        _ event: CKSyncEngine.Event,
        syncEngine: CKSyncEngine
    ) async {
        guard engine === syncEngine && authorization() else { return }
        switch event {
        case let .accountChange(change):
            let changed: Bool
            switch change.changeType {
            case let .signIn(currentUser):
                if let boundAccountRecordName {
                    changed = boundAccountRecordName != currentUser.recordName
                } else {
                    boundAccountRecordName = currentUser.recordName
                    changed = false
                }
            case .signOut, .switchAccounts:
                changed = true
            @unknown default:
                changed = true
            }
            if changed {
                accountTransitionInvalidated = true
                fetchError = .accountChanged
                sendError = .accountChanged
                zoneSaveError = .accountChanged
            }
        case let .fetchedRecordZoneChanges(changes):
            for modification in changes.modifications {
                do {
                    try acceptFetchedRecord(modification.record)
                } catch let error as CloudKitKeyBootstrapError {
                    fetchError = error
                } catch {
                    fetchError = .corruptClaim
                }
            }
        case let .didFetchRecordZoneChanges(result):
            if let error = result.error {
                if result.zoneID == zoneID && error.code == .zoneNotFound {
                    zoneExists = false
                } else {
                    fetchError = mapCloudKitError(error, phase: .fetch)
                }
            }
        case let .sentDatabaseChanges(changes):
            for failure in changes.failedZoneSaves where failure.zone.zoneID == zoneID {
                zoneSaveError = mapCloudKitError(failure.error, phase: .zoneCreate)
            }
        case let .sentRecordZoneChanges(changes):
            await acceptSentChanges(changes)
        case .stateUpdate, .fetchedDatabaseChanges, .willFetchChanges,
             .willFetchRecordZoneChanges, .didFetchChanges, .willSendChanges,
             .didSendChanges:
            break
        @unknown default:
            fetchError = .cloudKit(.init(
                phase: .fetch,
                code: CKError.internalError.rawValue
            ))
        }
    }

    public func nextRecordZoneChangeBatch(
        _ context: CKSyncEngine.SendChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.RecordZoneChangeBatch? {
        if accountTransitionInvalidated {
            sendError = .accountChanged
            return nil
        }
        guard engine === syncEngine else { return nil }
        guard authorization() else {
            sendError = .localAuthorizationUnavailable
            return nil
        }
        guard
              let outboundClaim,
              context.options.scope.contains(outboundClaim.recordID) else {
            return nil
        }
        return .init(recordsToSave: [outboundClaim], atomicByZone: true)
    }

    public func nextFetchChangesOptions(
        _ context: CKSyncEngine.FetchChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.FetchChangesOptions {
        _ = context
        if accountTransitionInvalidated {
            fetchError = .accountChanged
            return .init(scope: .zoneIDs([]))
        }
        guard engine === syncEngine else {
            return .init(scope: .zoneIDs([]))
        }
        guard authorization() else {
            fetchError = .localAuthorizationUnavailable
            return .init(scope: .zoneIDs([]))
        }
        return .init(scope: .zoneIDs([zoneID]))
    }

    private var claimRecordID: CKRecord.ID {
        CKRecord.ID(recordName: Self.claimRecordName, zoneID: zoneID)
    }

    private func makeEngineIfRequired() -> CKSyncEngine {
        if let engine { return engine }
        let database = CKContainer(identifier: containerIdentifier).privateCloudDatabase
        var configuration = CKSyncEngine.Configuration(
            database: database,
            stateSerialization: nil,
            delegate: self
        )
        configuration.automaticallySync = false
        configuration.subscriptionID = subscriptionID
        let created = CKSyncEngine(configuration)
        engine = created
        return created
    }

    private func acceptFetchedRecord(_ record: CKRecord) throws {
        if record.recordType == Self.claimRecordType {
            guard record.recordID == claimRecordID else {
                throw CloudKitKeyBootstrapError.conflictingClaims
            }
            let claim = try Self.decodeClaim(record, zoneID: zoneID)
            if let fetchedClaim, fetchedClaim != claim {
                throw CloudKitKeyBootstrapError.conflictingClaims
            }
            fetchedClaim = claim
            return
        }
        fetchedDomainRecords = true // Unknown existing records are not an empty zone.
    }

    private func acceptSentChanges(
        _ changes: CKSyncEngine.Event.SentRecordZoneChanges
    ) async {
        for saved in changes.savedRecords where saved.recordID == claimRecordID {
            do {
                let claim = try Self.decodeClaim(saved, zoneID: zoneID)
                let receipt = CompanionBootstrapClaimReceipt(
                    keyVersion: claim.keyVersion,
                    serverChangeTag: claim.serverChangeTag
                )
                guard let acceptedHandler else {
                    sendError = .receiptPersistenceFailed
                    return
                }
                try await verifyAccountContinuity()
                try await acceptedHandler(receipt)
                try requireActiveContinuity()
                sentReceipt = receipt
            } catch let error as CloudKitKeyBootstrapError {
                sendError = error
            } catch {
                sendError = .receiptPersistenceFailed
            }
        }
        for failure in changes.failedRecordSaves
        where failure.record.recordID == claimRecordID {
            if failure.error.code == .serverRecordChanged,
               let serverRecord = failure.error.serverRecord {
                do {
                    existingClaim = try Self.decodeClaim(serverRecord, zoneID: zoneID)
                } catch {
                    sendError = .corruptClaim
                }
            } else {
                sendError = mapCloudKitError(failure.error, phase: .claimSend)
            }
        }
    }

    public nonisolated static func decodeClaim(
        _ record: CKRecord, zoneID: CKRecordZone.ID
    ) throws -> CompanionBootstrapClaim {
        guard record.recordType == Self.claimRecordType,
              record.recordID == CKRecord.ID(recordName: claimRecordName, zoneID: zoneID),
              let number = record[Self.keyVersionField] as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID(),
              number.doubleValue.isFinite,
              number.doubleValue.rounded(.towardZero) == number.doubleValue,
              number.int64Value > 0,
              number.uint64Value <= UInt64(UInt32.max),
              let digest = record[Self.keySHA256Field] as? String,
              let changeTag = record.recordChangeTag,
              !changeTag.isEmpty,
              CompanionBootstrapClaim(keyVersion: number.uint32Value,
                                      serverChangeTag: changeTag, keySHA256: digest).hasKeyCommitment else {
            throw CloudKitKeyBootstrapError.corruptClaim
        }
        return .init(
            keyVersion: number.uint32Value,
            serverChangeTag: changeTag,
            keySHA256: digest
        )
    }

    private func mapCloudKitError(
        _ error: CKError,
        phase: CloudKitKeyBootstrapPhase
    ) -> CloudKitKeyBootstrapError {
        .cloudKit(.init(
            phase: phase,
            code: error.code.rawValue,
            leafCodes: boundedLeafErrorCodes(in: error)
        ))
    }

    /// A missing bootstrap zone is an empty starting point only when CloudKit
    /// confirms `zoneNotFound` for the one zone this fetch explicitly scoped.
    /// User-deleted zones, mixed partial failures and failures for any other
    /// item remain errors so they cannot authorize a fresh claim.
    private func isConfirmedMissingRequestedZone(_ error: CKError) -> Bool {
        if error.code == .zoneNotFound {
            return true // `inspectRemote` scopes this operation to only `zoneID`.
        }
        guard error.code == .partialFailure,
              let partialErrors = error.partialErrorsByItemID,
              partialErrors.count == 1,
              let (itemID, itemError) = partialErrors.first,
              let itemZoneID = itemID.base as? CKRecordZone.ID,
              itemZoneID == zoneID,
              (itemError as NSError).domain == CKErrorDomain,
              (itemError as NSError).code == CKError.zoneNotFound.rawValue else {
            return false
        }
        return true
    }

    /// Preserve only a bounded set of numeric leaf codes. Item identifiers,
    /// descriptions and the original userInfo never leave this method.
    private func boundedLeafErrorCodes(in error: CKError) -> [Int] {
        let maximumCodeCount = 8
        let maximumDepth = 3

        func collect(_ error: CKError, depth: Int) -> [Int] {
            guard depth < maximumDepth,
                  error.code == .partialFailure,
                  let partialErrors = error.partialErrorsByItemID,
                  !partialErrors.isEmpty else {
                return depth == 0 ? [] : [error.code.rawValue]
            }
            let nestedCodes = partialErrors.values.flatMap { itemError -> [Int] in
                let cocoaError = itemError as NSError
                guard cocoaError.domain == CKErrorDomain else { return [] }
                guard cocoaError.code == CKError.partialFailure.rawValue,
                      let cloudError = itemError as? CKError else {
                    return [cocoaError.code]
                }
                return collect(cloudError, depth: depth + 1)
            }
            return nestedCodes
        }

        return Array(Set(collect(error, depth: 0)).sorted().prefix(maximumCodeCount))
    }

    private func verifyAccountContinuity() async throws {
        try requireActiveContinuity()
        let current: CKRecord.ID
        do {
            current = try await CKContainer(
                identifier: containerIdentifier
            ).userRecordID()
        } catch let cloudError as CKError {
            throw mapCloudKitError(cloudError, phase: .accountCheck)
        }
        try requireActiveContinuity()
        if let boundAccountRecordName {
            guard boundAccountRecordName == current.recordName else {
                accountTransitionInvalidated = true
                throw CloudKitKeyBootstrapError.accountChanged
            }
        } else {
            boundAccountRecordName = current.recordName
        }
    }

    private func requireActiveContinuity() throws {
        if accountTransitionInvalidated {
            throw CloudKitKeyBootstrapError.accountChanged
        }
        guard !isShutdown && authorization() else {
            throw CloudKitKeyBootstrapError.localAuthorizationUnavailable
        }
    }

    private func clearOperationState() {
        fetchedClaim = nil
        fetchedDomainRecords = false
        fetchError = nil
        outboundClaim = nil
        acceptedHandler = nil
        sentReceipt = nil
        existingClaim = nil
        sendError = nil
        zoneSaveError = nil
    }
}

#endif
