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

private final class CloudKitAccountInvalidation: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    private var observer: NSObjectProtocol?

    var isInvalidated: Bool {
        lock.lock()
        defer { lock.unlock() }
        return value
    }

    func invalidate() {
        lock.lock()
        value = true
        lock.unlock()
    }

    func start() {
        let observer = NotificationCenter.default.addObserver(
            forName: .CKAccountChanged,
            object: nil,
            queue: nil
        ) { @Sendable [weak self] _ in
            self?.invalidate()
        }
        lock.lock()
        self.observer = observer
        lock.unlock()
    }

    func stop() {
        lock.lock()
        let observer = self.observer
        self.observer = nil
        lock.unlock()
        if let observer {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    deinit {
        stop()
    }
}

@available(iOS 17.0, macOS 14.0, *)
public actor CloudKitKeyBootstrapTransport: CompanionKeyBootstrapTransport {
    public static let claimRecordType = "AhoiKeyBootstrapClaim"
    public static let claimRecordName = "payload-key-bootstrap-v1"
    public static let keyVersionField = "keyVersion"
    public static let keySHA256Field = "keySHA256"

    private let container: CKContainer
    private let database: CKDatabase
    private let zoneID: CKRecordZone.ID
    private var fetchedClaim: CompanionBootstrapClaim?
    private var fetchedDomainRecords = false
    private var isShutdown = false
    private var boundAccountRecordName: String?
    private let accountInvalidation: CloudKitAccountInvalidation
    private let authorization: @Sendable () -> Bool

    public init(containerIdentifier: String, zoneName: String,
                subscriptionID: String? = nil,
                authorization: @escaping @Sendable () -> Bool = { true }) throws {
        guard containerIdentifier.hasPrefix("iCloud."),
              containerIdentifier.count > "iCloud.".count,
              !zoneName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw CloudKitKeyBootstrapError.invalidConfiguration
        }
        let container = CKContainer(identifier: containerIdentifier)
        self.container = container
        self.database = container.privateCloudDatabase
        _ = subscriptionID
        self.authorization = authorization
        let accountInvalidation = CloudKitAccountInvalidation()
        self.accountInvalidation = accountInvalidation
        self.zoneID = CKRecordZone.ID(
            zoneName: zoneName,
            ownerName: CKCurrentUserDefaultName
        )
        accountInvalidation.start()
    }

    deinit {
        accountInvalidation.stop()
    }

    public func inspectRemote() async throws -> CompanionBootstrapRemoteSnapshot {
        try requireActiveContinuity()
        try await verifyAccountContinuity()
        // A bootstrap snapshot comes directly from the server rather than
        // incremental engine state or the absence of delegate callbacks.
        try requireActiveContinuity()
        fetchedClaim = nil
        fetchedDomainRecords = false
        guard try await fetchRequestedZone(phase: .fetch) != nil else {
            try await verifyAccountContinuity()
            return .init(zoneExists: false, claim: nil, hasEncryptedDomainRecords: false)
        }
        try await scanRequestedZone()
        try requireActiveContinuity()
        try await verifyAccountContinuity()
        return .init(
            zoneExists: true,
            claim: fetchedClaim,
            hasEncryptedDomainRecords: fetchedDomainRecords
        )
    }

    public func ensureZone() async throws {
        try requireActiveContinuity()
        try await verifyAccountContinuity()
        let savedZone: CKRecordZone
        do {
            savedZone = try await database.save(CKRecordZone(zoneID: zoneID))
        } catch let cloudError as CKError {
            throw mapCloudKitError(cloudError, phase: .zoneCreate)
        }
        try requireActiveContinuity()
        guard savedZone.zoneID == zoneID else {
            throw CloudKitKeyBootstrapError.cloudKit(.init(
                phase: .zoneCreate,
                code: CKError.internalError.rawValue
            ))
        }
        try await verifyAccountContinuity()
        guard try await fetchRequestedZone(phase: .zoneCreate) != nil else {
            throw CloudKitKeyBootstrapError.cloudKit(.init(
                phase: .zoneCreate,
                code: CKError.zoneNotFound.rawValue
            ))
        }
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
        let recordID = claimRecordID
        let record = CKRecord(recordType: Self.claimRecordType, recordID: recordID)
        record[Self.keyVersionField] = NSNumber(value: keyVersion)
        record[Self.keySHA256Field] = keySHA256 as CKRecordValue
        do {
            let response = try await database.modifyRecords(
                saving: [record],
                deleting: [],
                savePolicy: .ifServerRecordUnchanged,
                atomically: true
            )
            try requireActiveContinuity()
            try await verifyAccountContinuity()
            guard response.deleteResults.isEmpty,
                  response.saveResults.count == 1,
                  let result = response.saveResults[recordID] else {
                throw CloudKitKeyBootstrapError.missingSendResult
            }
            switch result {
            case .success(let savedRecord):
                let claim = try Self.decodeClaim(savedRecord, zoneID: zoneID)
                let receipt = CompanionBootstrapClaimReceipt(
                    keyVersion: claim.keyVersion,
                    serverChangeTag: claim.serverChangeTag
                )
                do {
                    try await accepted(receipt)
                } catch let error as CloudKitKeyBootstrapError {
                    throw error
                } catch {
                    throw CloudKitKeyBootstrapError.receiptPersistenceFailed
                }
                try requireActiveContinuity()
                return .created(receipt)
            case .failure(let error):
                if let existing = try existingClaim(from: error) {
                    return .existing(existing)
                }
                throw mapCloudKitError(error, phase: .claimSend)
            }
        } catch let error as CloudKitKeyBootstrapError {
            throw error
        } catch let cloudError as CKError {
            if let itemError = exactClaimPartialError(
                in: cloudError,
                recordID: recordID
            ) {
                if let existing = try existingClaim(from: itemError) {
                    return .existing(existing)
                }
                throw mapCloudKitError(itemError, phase: .claimSend)
            }
            if let existing = try existingClaim(from: cloudError) {
                return .existing(existing)
            }
            throw mapCloudKitError(cloudError, phase: .claimSend)
        }
    }

    public func shutdown() async {
        isShutdown = true
        accountInvalidation.stop()
        clearOperationState()
    }

    public func verifiedClaim() -> CompanionBootstrapClaim? {
        authorization() && !isShutdown && !accountInvalidation.isInvalidated
            ? fetchedClaim
            : nil
    }

    private var claimRecordID: CKRecord.ID {
        CKRecord.ID(recordName: Self.claimRecordName, zoneID: zoneID)
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

    private func fetchRequestedZone(
        phase: CloudKitKeyBootstrapPhase
    ) async throws -> CKRecordZone? {
        try requireActiveContinuity()
        do {
            let zone = try await database.recordZone(for: zoneID)
            try requireActiveContinuity()
            guard zone.zoneID == zoneID else {
                throw CloudKitKeyBootstrapError.cloudKit(.init(
                    phase: phase,
                    code: CKError.internalError.rawValue
                ))
            }
            return zone
        } catch let bootstrapError as CloudKitKeyBootstrapError {
            throw bootstrapError
        } catch let cloudError as CKError {
            if isConfirmedMissingRequestedZone(cloudError) { return nil }
            throw mapCloudKitError(cloudError, phase: phase)
        }
    }

    private func scanRequestedZone() async throws {
        var token: CKServerChangeToken?
        var moreComing = true
        while moreComing {
            try requireActiveContinuity()
            do {
                let page = try await database.recordZoneChanges(
                    inZoneWith: zoneID,
                    since: token,
                    desiredKeys: [Self.keyVersionField, Self.keySHA256Field],
                    resultsLimit: nil
                )
                try requireActiveContinuity()
                for result in page.modificationResultsByID.values {
                    switch result {
                    case .success(let modification):
                        try acceptFetchedRecord(modification.record)
                    case .failure(let error):
                        guard let cloudError = error as? CKError else {
                            throw CloudKitKeyBootstrapError.cloudKit(.init(
                                phase: .fetch,
                                code: CKError.internalError.rawValue
                            ))
                        }
                        throw mapCloudKitError(cloudError, phase: .fetch)
                    }
                }
                token = page.changeToken
                moreComing = page.moreComing
            } catch let bootstrapError as CloudKitKeyBootstrapError {
                throw bootstrapError
            } catch let cloudError as CKError {
                throw mapCloudKitError(cloudError, phase: .fetch)
            }
        }
    }

    private func existingClaim(from error: Error) throws -> CompanionBootstrapClaim? {
        let cocoaError = error as NSError
        guard cocoaError.domain == CKErrorDomain,
              cocoaError.code == CKError.serverRecordChanged.rawValue else {
            return nil
        }
        guard let cloudError = error as? CKError,
              let serverRecord = cloudError.serverRecord else {
            throw CloudKitKeyBootstrapError.missingSendResult
        }
        return try Self.decodeClaim(serverRecord, zoneID: zoneID)
    }

    private func exactClaimPartialError(
        in error: CKError,
        recordID: CKRecord.ID
    ) -> Error? {
        guard error.code == .partialFailure,
              let partialErrors = error.partialErrorsByItemID,
              partialErrors.count == 1,
              let (itemID, itemError) = partialErrors.first,
              let itemRecordID = itemID.base as? CKRecord.ID,
              itemRecordID == recordID else {
            return nil
        }
        return itemError
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

    private func mapCloudKitError(
        _ error: Error,
        phase: CloudKitKeyBootstrapPhase
    ) -> CloudKitKeyBootstrapError {
        if let cloudError = error as? CKError {
            return mapCloudKitError(cloudError, phase: phase)
        }
        let cocoaError = error as NSError
        guard cocoaError.domain == CKErrorDomain else {
            return .cloudKit(.init(
                phase: phase,
                code: CKError.internalError.rawValue
            ))
        }
        return .cloudKit(.init(phase: phase, code: cocoaError.code))
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
            current = try await container.userRecordID()
        } catch let cloudError as CKError {
            throw mapCloudKitError(cloudError, phase: .accountCheck)
        }
        try requireActiveContinuity()
        if let boundAccountRecordName {
            guard boundAccountRecordName == current.recordName else {
                accountInvalidation.invalidate()
                throw CloudKitKeyBootstrapError.accountChanged
            }
        } else {
            boundAccountRecordName = current.recordName
        }
    }

    private func requireActiveContinuity() throws {
        if accountInvalidation.isInvalidated {
            throw CloudKitKeyBootstrapError.accountChanged
        }
        guard !isShutdown && authorization() else {
            throw CloudKitKeyBootstrapError.localAuthorizationUnavailable
        }
    }

    private func clearOperationState() {
        fetchedClaim = nil
        fetchedDomainRecords = false
    }
}

#endif
