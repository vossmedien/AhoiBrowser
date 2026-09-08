import CoreFoundation
import Foundation
import AhoiCloudKitSpike

#if canImport(CloudKit)
import CloudKit

public enum CloudKitKeyBootstrapError: Error, Equatable, Sendable {
    case invalidConfiguration
    case accountChanged
    case accountUnavailable
    case corruptClaim
    case conflictingClaims
    case zoneCreationFailed(Int)
    case fetchFailed(Int)
    case sendFailed(Int)
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
        guard !isShutdown && authorization() else { throw CloudKitKeyBootstrapError.accountUnavailable }
        // This API promises a full snapshot. Clearing fetchedClaim while
        // reusing an incremental CKSyncEngine token could report false emptiness.
        if let oldEngine = engine {
            engine = nil
            await oldEngine.cancelOperations()
        }
        guard !isShutdown && authorization() else { throw CloudKitKeyBootstrapError.accountUnavailable }
        let engine = makeEngineIfRequired()
        fetchedClaim = nil
        fetchedDomainRecords = false
        fetchError = nil
        zoneExists = true
        do {
            try await engine.fetchChanges(.init(scope: .zoneIDs([zoneID])))
        } catch let cloudError as CKError {
            if cloudError.code == .zoneNotFound || cloudError.code == .userDeletedZone {
                zoneExists = false
            } else {
                throw mapFetchError(cloudError)
            }
        }
        if let fetchError { throw fetchError }
        return .init(
            zoneExists: zoneExists,
            claim: fetchedClaim,
            hasEncryptedDomainRecords: fetchedDomainRecords
        )
    }

    public func ensureZone() async throws {
        guard !isShutdown && authorization() else { throw CloudKitKeyBootstrapError.accountUnavailable }
        let engine = makeEngineIfRequired()
        zoneSaveError = nil
        engine.state.add(
            pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))]
        )
        do {
            try await engine.sendChanges()
        } catch let cloudError as CKError {
            throw CloudKitKeyBootstrapError.zoneCreationFailed(
                cloudError.code.rawValue
            )
        }
        if let zoneSaveError { throw zoneSaveError }
    }

    public func createClaim(
        keyVersion: UInt32,
        keySHA256: String,
        accepted: @escaping @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    ) async throws -> CompanionBootstrapClaimResult {
        guard !isShutdown, authorization(), keyVersion > 0,
              CompanionBootstrapClaim(keyVersion: keyVersion, serverChangeTag: "pending",
                                      keySHA256: keySHA256).hasKeyCommitment else {
            throw CloudKitKeyBootstrapError.invalidConfiguration
        }
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
            throw CloudKitKeyBootstrapError.sendFailed(cloudError.code.rawValue)
        }
        if let sendError { throw sendError }
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
        authorization() && !isShutdown ? fetchedClaim : nil
    }

    public func handleEvent(
        _ event: CKSyncEngine.Event,
        syncEngine: CKSyncEngine
    ) async {
        guard engine === syncEngine && authorization() else { return }
        switch event {
        case .accountChange:
            fetchError = .accountChanged
            sendError = .accountChanged
            zoneSaveError = .accountChanged
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
                if error.code == .zoneNotFound || error.code == .userDeletedZone {
                    zoneExists = false
                } else {
                    fetchError = mapFetchError(error)
                }
            }
        case let .sentDatabaseChanges(changes):
            for failure in changes.failedZoneSaves where failure.zone.zoneID == zoneID {
                zoneSaveError = .zoneCreationFailed(failure.error.code.rawValue)
            }
        case let .sentRecordZoneChanges(changes):
            await acceptSentChanges(changes)
        case .stateUpdate, .fetchedDatabaseChanges, .willFetchChanges,
             .willFetchRecordZoneChanges, .didFetchChanges, .willSendChanges,
             .didSendChanges:
            break
        @unknown default:
            fetchError = .fetchFailed(CKError.internalError.rawValue)
        }
    }

    public func nextRecordZoneChangeBatch(
        _ context: CKSyncEngine.SendChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.RecordZoneChangeBatch? {
        guard engine === syncEngine,
              authorization(),
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
        guard engine === syncEngine && authorization() else {
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
                try await acceptedHandler(receipt)
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
                sendError = .sendFailed(failure.error.code.rawValue)
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

    private func mapFetchError(_ error: CKError) -> CloudKitKeyBootstrapError {
        switch error.code {
        case .notAuthenticated, .permissionFailure:
            .accountUnavailable
        default:
            .fetchFailed(error.code.rawValue)
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
