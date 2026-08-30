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

    private let containerIdentifier: String
    private let zoneID: CKRecordZone.ID
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

    public init(containerIdentifier: String, zoneName: String) throws {
        guard containerIdentifier.hasPrefix("iCloud."),
              containerIdentifier.count > "iCloud.".count,
              !zoneName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw CloudKitKeyBootstrapError.invalidConfiguration
        }
        self.containerIdentifier = containerIdentifier
        self.zoneID = CKRecordZone.ID(
            zoneName: zoneName,
            ownerName: CKCurrentUserDefaultName
        )
    }

    public func inspectRemote() async throws -> CompanionBootstrapRemoteSnapshot {
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
        accepted: @escaping @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    ) async throws -> CompanionBootstrapClaimResult {
        guard keyVersion > 0 else {
            throw CloudKitKeyBootstrapError.invalidConfiguration
        }
        let engine = makeEngineIfRequired()
        let recordID = claimRecordID
        let record = CKRecord(recordType: Self.claimRecordType, recordID: recordID)
        record[Self.keyVersionField] = NSNumber(value: keyVersion)
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
        let active = engine
        engine = nil
        await active?.cancelOperations()
        clearOperationState()
    }

    public func handleEvent(
        _ event: CKSyncEngine.Event,
        syncEngine: CKSyncEngine
    ) async {
        guard engine === syncEngine else { return }
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
        guard engine === syncEngine else {
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
        configuration.subscriptionID = nil
        let created = CKSyncEngine(configuration)
        engine = created
        return created
    }

    private func acceptFetchedRecord(_ record: CKRecord) throws {
        if record.recordType == Self.claimRecordType {
            guard record.recordID == claimRecordID else {
                throw CloudKitKeyBootstrapError.conflictingClaims
            }
            let claim = try decodeClaim(record)
            if let fetchedClaim, fetchedClaim != claim {
                throw CloudKitKeyBootstrapError.conflictingClaims
            }
            fetchedClaim = claim
            return
        }
        if record.recordType == AppleCloudKitRecordCodec.recordType,
           record.encryptedValues[AppleCloudKitRecordCodec.Fields.encryptedValue] != nil {
            fetchedDomainRecords = true
        }
    }

    private func acceptSentChanges(
        _ changes: CKSyncEngine.Event.SentRecordZoneChanges
    ) async {
        for saved in changes.savedRecords where saved.recordID == claimRecordID {
            do {
                let claim = try decodeClaim(saved)
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
                    existingClaim = try decodeClaim(serverRecord)
                } catch {
                    sendError = .corruptClaim
                }
            } else {
                sendError = .sendFailed(failure.error.code.rawValue)
            }
        }
    }

    private func decodeClaim(_ record: CKRecord) throws -> CompanionBootstrapClaim {
        guard record.recordType == Self.claimRecordType,
              record.recordID == claimRecordID,
              let number = record[Self.keyVersionField] as? NSNumber,
              number.int64Value > 0,
              number.uint64Value <= UInt64(UInt32.max),
              let changeTag = record.recordChangeTag,
              !changeTag.isEmpty else {
            throw CloudKitKeyBootstrapError.corruptClaim
        }
        return .init(
            keyVersion: number.uint32Value,
            serverChangeTag: changeTag
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
