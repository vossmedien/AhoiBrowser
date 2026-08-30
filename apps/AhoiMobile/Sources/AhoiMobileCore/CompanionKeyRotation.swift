import CryptoKit
import Foundation
import AhoiCloudKitSpike

public enum CompanionKeyRotationStage: String, Codable, Equatable, Sendable {
    case installingNextKey
    case resealingLocalRecords
    case awaitingRemoteAcknowledgement
    case revokingPreviousKey
    case completed
}

public struct CompanionKeyRotationAcknowledgement: Codable, Equatable, Sendable {
    public let planID: UUID
    public let keyVersion: UInt32
    public let recordIDs: [UUID]
    public let recordsDigest: Data
    public let readBackAt: Date

    public init(
        planID: UUID,
        keyVersion: UInt32,
        recordIDs: [UUID],
        recordsDigest: Data,
        readBackAt: Date
    ) {
        self.planID = planID
        self.keyVersion = keyVersion
        self.recordIDs = recordIDs.sorted(by: Self.sortIDs)
        self.recordsDigest = recordsDigest
        self.readBackAt = readBackAt
    }

    private static func sortIDs(_ lhs: UUID, _ rhs: UUID) -> Bool {
        lhs.uuidString < rhs.uuidString
    }
}

/// Durable, restart-safe progress for one key-family transition. A record ID
/// enters `resealedRecordIDs` only after the local store has read the exact
/// replacement envelope back. The acknowledgement is persisted before the
/// previous key can be removed.
public struct CompanionKeyRotationPlan: Codable, Equatable, Sendable {
    public let id: UUID
    public let currentVersion: UInt32
    public let nextVersion: UInt32
    public let startedAt: Date
    public let transitionEndsAt: Date
    public private(set) var stage: CompanionKeyRotationStage
    public private(set) var scheduledRecordIDs: [UUID]
    public private(set) var resealedRecordIDs: [UUID]
    public private(set) var acknowledgement: CompanionKeyRotationAcknowledgement?

    public init(
        id: UUID = UUID(),
        currentVersion: UInt32,
        nextVersion: UInt32,
        startedAt: Date,
        transitionEndsAt: Date
    ) {
        self.id = id
        self.currentVersion = currentVersion
        self.nextVersion = nextVersion
        self.startedAt = startedAt
        self.transitionEndsAt = transitionEndsAt
        self.stage = .installingNextKey
        self.scheduledRecordIDs = []
        self.resealedRecordIDs = []
        self.acknowledgement = nil
    }

    public var remainingRecordIDs: [UUID] {
        let resealed = Set(resealedRecordIDs)
        return scheduledRecordIDs.filter { !resealed.contains($0) }
    }

    public var permitsEncryptedDomainRecords: Bool {
        stage == .completed
    }

    public var lifecycleStatus: CompanionKeyLifecycleStatus {
        stage == .completed
            ? .ready(keyVersion: nextVersion)
            : .rotation(
                current: currentVersion,
                next: nextVersion,
                transitionEndsAt: transitionEndsAt
            )
    }

    mutating func setStage(_ stage: CompanionKeyRotationStage) {
        self.stage = stage
    }

    mutating func mergeScheduledRecordIDs(_ recordIDs: [UUID]) {
        scheduledRecordIDs = Array(Set(scheduledRecordIDs).union(recordIDs))
            .sorted(by: Self.sortIDs)
    }

    mutating func markResealed(_ recordID: UUID) {
        resealedRecordIDs = Array(Set(resealedRecordIDs).union([recordID]))
            .sorted(by: Self.sortIDs)
    }

    mutating func markNeedsReseal(_ recordID: UUID) {
        resealedRecordIDs.removeAll { $0 == recordID }
    }

    mutating func setAcknowledgement(
        _ acknowledgement: CompanionKeyRotationAcknowledgement
    ) {
        self.acknowledgement = acknowledgement
    }

    private static func sortIDs(_ lhs: UUID, _ rhs: UUID) -> Bool {
        lhs.uuidString < rhs.uuidString
    }
}

public enum CompanionKeyRotationError: Error, Equatable, Sendable {
    case invalidVersions
    case invalidTransitionDeadline
    case rotationPlanMissing
    case conflictingRotationPlan
    case corruptRotationJournal
    case unsupportedRotationJournalVersion(UInt32)
    case journalReadbackMismatch
    case currentKeyMissing(UInt32)
    case nextKeyMissing(UInt32)
    case transitionExpired
    case unsupportedRecordKeyVersion(recordID: UUID, keyVersion: UInt32)
    case localRecordMissing(UUID)
    case localRecordReadbackMismatch(UUID)
    case remoteReadbackIdentityMismatch
    case remoteReadbackRecordsMismatch
    case acknowledgementMissing
    case localStateChangedAfterAcknowledgement
    case previousKeyStillPresent(UInt32)
}

private struct CompanionKeyRotationJournalEnvelope: Codable, Equatable {
    static let currentFormatVersion: UInt32 = 1

    let formatVersion: UInt32
    let plan: CompanionKeyRotationPlan

    init(plan: CompanionKeyRotationPlan) {
        self.formatVersion = Self.currentFormatVersion
        self.plan = plan
    }
}

public protocol CompanionKeyRotationJournalStoring: Sendable {
    func loadRotationPlan() async throws -> CompanionKeyRotationPlan?
    func saveRotationPlan(_ plan: CompanionKeyRotationPlan) async throws
}

public actor FileCompanionKeyRotationJournalStore:
    CompanionKeyRotationJournalStoring {
    private let fileURL: URL
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(fileURL: URL) {
        self.fileURL = fileURL
        self.encoder = JSONEncoder()
        self.encoder.outputFormatting = [.sortedKeys]
        self.decoder = JSONDecoder()
    }

    public func loadRotationPlan() throws -> CompanionKeyRotationPlan? {
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            return nil
        }
        do {
            let envelope = try decoder.decode(
                CompanionKeyRotationJournalEnvelope.self,
                from: Data(contentsOf: fileURL)
            )
            guard envelope.formatVersion ==
                    CompanionKeyRotationJournalEnvelope.currentFormatVersion else {
                throw CompanionKeyRotationError.unsupportedRotationJournalVersion(
                    envelope.formatVersion
                )
            }
            return envelope.plan
        } catch let error as CompanionKeyRotationError {
            throw error
        } catch {
            throw CompanionKeyRotationError.corruptRotationJournal
        }
    }

    public func saveRotationPlan(_ plan: CompanionKeyRotationPlan) throws {
        let data: Data
        do {
            data = try encoder.encode(
                CompanionKeyRotationJournalEnvelope(plan: plan)
            )
        } catch {
            throw CompanionKeyRotationError.corruptRotationJournal
        }
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fileURL, options: [.atomic])
        guard try loadRotationPlan() == plan else {
            throw CompanionKeyRotationError.journalReadbackMismatch
        }
    }
}

public protocol CompanionPayloadKeyRotationStoring: Sendable {
    func hasCanonicalKey(version: UInt32) async throws -> Bool
    func installCanonicalKey(
        version: UInt32,
        replacing currentVersion: UInt32,
        generator: @escaping @Sendable () throws -> Data
    ) async throws
    func removeCanonicalKey(version: UInt32) async throws
}

public protocol CompanionKeyRotationSealing: Sendable {
    /// Implementations must authenticate values already sealed with `next`
    /// and return those bytes unchanged so a crash-resume cannot churn them.
    func reseal(
        _ value: EncryptedValue,
        currentVersion: UInt32,
        nextVersion: UInt32
    ) throws -> EncryptedValue
}

public struct CompanionKeyRotationManifest: Codable, Equatable, Sendable {
    public let planID: UUID
    public let currentVersion: UInt32
    public let nextVersion: UInt32
    public let transitionEndsAt: Date
    public let recordIDs: [UUID]
    public let recordsDigest: Data
}

public struct CompanionKeyRotationRemoteReadback: Equatable, Sendable {
    public let planID: UUID
    public let keyVersion: UInt32
    public let records: [SyncRecord]
    public let readBackAt: Date

    public init(
        planID: UUID,
        keyVersion: UInt32,
        records: [SyncRecord],
        readBackAt: Date
    ) {
        self.planID = planID
        self.keyVersion = keyVersion
        self.records = records
        self.readBackAt = readBackAt
    }
}

/// The implementation must publish idempotently by `manifest.planID`, reject a
/// competing rotation identity, wait for all enrolled non-revoked writers to
/// acknowledge the next version, fetch the provider state, and return only
/// that readback. Merely accepting this device's upload is not acknowledgement.
public protocol CompanionKeyRotationRemoteAcknowledging: Sendable {
    func publishAndReadBack(
        records: [SyncRecord],
        manifest: CompanionKeyRotationManifest
    ) async throws -> CompanionKeyRotationRemoteReadback
}

/// Runs while normal encrypted-domain writes are disabled. Every durable
/// boundary is ordered so replaying after a process crash is safe: install is
/// read-backed, local envelopes are read-backed one at a time, remote bytes are
/// compared exactly, and only then is the previous key removed.
public actor CompanionKeyRotationCoordinator {
    private let journalStore: any CompanionKeyRotationJournalStoring
    private let keyStore: any CompanionPayloadKeyRotationStoring
    private let recordStore: any LocalSyncRecordStore
    private let sealer: any CompanionKeyRotationSealing
    private let remote: any CompanionKeyRotationRemoteAcknowledging
    private let generator: @Sendable () throws -> Data
    private let now: @Sendable () -> Date

    public init(
        journalStore: any CompanionKeyRotationJournalStoring,
        keyStore: any CompanionPayloadKeyRotationStoring,
        recordStore: any LocalSyncRecordStore,
        sealer: any CompanionKeyRotationSealing,
        remote: any CompanionKeyRotationRemoteAcknowledging,
        generator: @escaping @Sendable () throws -> Data,
        now: @escaping @Sendable () -> Date = { Date() }
    ) {
        self.journalStore = journalStore
        self.keyStore = keyStore
        self.recordStore = recordStore
        self.sealer = sealer
        self.remote = remote
        self.generator = generator
        self.now = now
    }

    public func currentPlan() async throws -> CompanionKeyRotationPlan? {
        guard let plan = try await journalStore.loadRotationPlan() else {
            return nil
        }
        try validatePlan(plan)
        return plan
    }

    public func begin(
        currentVersion: UInt32,
        nextVersion: UInt32,
        transitionEndsAt: Date
    ) async throws -> CompanionKeyRotationPlan {
        guard currentVersion > 0,
              nextVersion > 0,
              nextVersion > currentVersion else {
            throw CompanionKeyRotationError.invalidVersions
        }
        let startedAt = now()
        guard transitionEndsAt > startedAt else {
            throw CompanionKeyRotationError.invalidTransitionDeadline
        }

        if let existing = try await journalStore.loadRotationPlan() {
            try validatePlan(existing)
            if existing.currentVersion == currentVersion,
               existing.nextVersion == nextVersion {
                return try await advance(existing)
            }
            guard existing.stage == .completed,
                  existing.nextVersion == currentVersion else {
                throw CompanionKeyRotationError.conflictingRotationPlan
            }
        }
        guard try await keyStore.hasCanonicalKey(version: currentVersion) else {
            throw CompanionKeyRotationError.currentKeyMissing(currentVersion)
        }
        let plan = CompanionKeyRotationPlan(
            currentVersion: currentVersion,
            nextVersion: nextVersion,
            startedAt: startedAt,
            transitionEndsAt: transitionEndsAt
        )
        try await journalStore.saveRotationPlan(plan)
        return try await advance(plan)
    }

    public func resume() async throws -> CompanionKeyRotationPlan {
        guard let plan = try await journalStore.loadRotationPlan() else {
            throw CompanionKeyRotationError.rotationPlanMissing
        }
        return try await advance(plan)
    }

    private func advance(
        _ initialPlan: CompanionKeyRotationPlan
    ) async throws -> CompanionKeyRotationPlan {
        try validatePlan(initialPlan)
        var plan = initialPlan
        while true {
            switch plan.stage {
            case .installingNextKey:
                try ensureBeforeDeadline(plan)
                guard try await keyStore.hasCanonicalKey(
                    version: plan.currentVersion
                ) else {
                    throw CompanionKeyRotationError.currentKeyMissing(
                        plan.currentVersion
                    )
                }
                try await keyStore.installCanonicalKey(
                    version: plan.nextVersion,
                    replacing: plan.currentVersion,
                    generator: generator
                )
                guard try await keyStore.hasCanonicalKey(
                    version: plan.nextVersion
                ) else {
                    throw CompanionKeyRotationError.nextKeyMissing(plan.nextVersion)
                }
                plan.setStage(.resealingLocalRecords)
                try await journalStore.saveRotationPlan(plan)

            case .resealingLocalRecords:
                try ensureBeforeDeadline(plan)
                plan = try await refreshSchedule(plan)
                for recordID in plan.remainingRecordIDs {
                    plan = try await resealRecord(recordID, plan: plan)
                }
                let refreshed = try await refreshSchedule(plan)
                if !refreshed.remainingRecordIDs.isEmpty {
                    plan = refreshed
                    continue
                }
                plan = refreshed
                _ = try await validatedLocalRecords(for: plan)
                plan.setStage(.awaitingRemoteAcknowledgement)
                try await journalStore.saveRotationPlan(plan)

            case .awaitingRemoteAcknowledgement:
                try ensureBeforeDeadline(plan)
                let records = try await validatedLocalRecords(for: plan)
                let manifest = try makeManifest(plan: plan, records: records)
                let readback = try await remote.publishAndReadBack(
                    records: records,
                    manifest: manifest
                )
                let acknowledgement = try validate(
                    readback: readback,
                    manifest: manifest,
                    expectedRecords: records
                )
                plan.setAcknowledgement(acknowledgement)
                plan.setStage(.revokingPreviousKey)
                try await journalStore.saveRotationPlan(plan)

            case .revokingPreviousKey:
                guard let acknowledgement = plan.acknowledgement else {
                    throw CompanionKeyRotationError.acknowledgementMissing
                }
                let records = try await validatedLocalRecords(for: plan)
                let manifest = try makeManifest(plan: plan, records: records)
                guard acknowledgement.planID == manifest.planID,
                      acknowledgement.keyVersion == manifest.nextVersion,
                      acknowledgement.recordIDs == manifest.recordIDs,
                      acknowledgement.recordsDigest == manifest.recordsDigest else {
                    throw CompanionKeyRotationError
                        .localStateChangedAfterAcknowledgement
                }
                try await keyStore.removeCanonicalKey(
                    version: plan.currentVersion
                )
                guard !(try await keyStore.hasCanonicalKey(
                    version: plan.currentVersion
                )) else {
                    throw CompanionKeyRotationError.previousKeyStillPresent(
                        plan.currentVersion
                    )
                }
                guard try await keyStore.hasCanonicalKey(
                    version: plan.nextVersion
                ) else {
                    throw CompanionKeyRotationError.nextKeyMissing(plan.nextVersion)
                }
                plan.setStage(.completed)
                try await journalStore.saveRotationPlan(plan)

            case .completed:
                guard try await keyStore.hasCanonicalKey(
                    version: plan.nextVersion
                ) else {
                    throw CompanionKeyRotationError.nextKeyMissing(plan.nextVersion)
                }
                guard !(try await keyStore.hasCanonicalKey(
                    version: plan.currentVersion
                )) else {
                    throw CompanionKeyRotationError.previousKeyStillPresent(
                        plan.currentVersion
                    )
                }
                return plan
            }
        }
    }

    private func ensureBeforeDeadline(_ plan: CompanionKeyRotationPlan) throws {
        guard now() <= plan.transitionEndsAt else {
            throw CompanionKeyRotationError.transitionExpired
        }
    }

    private func validatePlan(_ plan: CompanionKeyRotationPlan) throws {
        let scheduled = plan.scheduledRecordIDs
        let resealed = plan.resealedRecordIDs
        guard plan.currentVersion > 0,
              plan.nextVersion > 0,
              plan.nextVersion > plan.currentVersion,
              plan.transitionEndsAt > plan.startedAt,
              scheduled == Array(Set(scheduled)).sorted(by: sortIDs),
              resealed == Array(Set(resealed)).sorted(by: sortIDs),
              Set(resealed).isSubset(of: Set(scheduled)) else {
            throw CompanionKeyRotationError.corruptRotationJournal
        }
        switch plan.stage {
        case .installingNextKey, .resealingLocalRecords:
            guard plan.acknowledgement == nil else {
                throw CompanionKeyRotationError.corruptRotationJournal
            }
        case .awaitingRemoteAcknowledgement:
            guard plan.acknowledgement == nil,
                  plan.remainingRecordIDs.isEmpty else {
                throw CompanionKeyRotationError.corruptRotationJournal
            }
        case .revokingPreviousKey, .completed:
            guard let acknowledgement = plan.acknowledgement,
                  plan.remainingRecordIDs.isEmpty,
                  acknowledgement.planID == plan.id,
                  acknowledgement.keyVersion == plan.nextVersion,
                  acknowledgement.recordIDs == scheduled,
                  acknowledgement.recordsDigest.count == 32 else {
                throw CompanionKeyRotationError.corruptRotationJournal
            }
        }
    }

    private func sortIDs(_ lhs: UUID, _ rhs: UUID) -> Bool {
        lhs.uuidString < rhs.uuidString
    }

    private func refreshSchedule(
        _ initialPlan: CompanionKeyRotationPlan
    ) async throws -> CompanionKeyRotationPlan {
        var plan = initialPlan
        let records = try await recordStore.allRecords()
        let recordsByID = Dictionary(uniqueKeysWithValues: records.map {
            ($0.recordID, $0)
        })
        for scheduledID in plan.scheduledRecordIDs
            where recordsByID[scheduledID] == nil {
            throw CompanionKeyRotationError.localRecordMissing(scheduledID)
        }
        plan.mergeScheduledRecordIDs(records.map(\.recordID))
        for record in records {
            try validateRecordVersion(record, plan: plan)
            if record.encryptedValue.keyVersion == plan.currentVersion {
                plan.markNeedsReseal(record.recordID)
            } else {
                plan.markResealed(record.recordID)
            }
        }
        if plan != initialPlan {
            try await journalStore.saveRotationPlan(plan)
        }
        return plan
    }

    private func resealRecord(
        _ recordID: UUID,
        plan initialPlan: CompanionKeyRotationPlan
    ) async throws -> CompanionKeyRotationPlan {
        var plan = initialPlan
        guard let record = try await recordStore.record(for: recordID) else {
            throw CompanionKeyRotationError.localRecordMissing(recordID)
        }
        try validateRecordVersion(record, plan: plan)
        if record.encryptedValue.keyVersion == plan.nextVersion {
            _ = try sealer.reseal(
                record.encryptedValue,
                currentVersion: plan.currentVersion,
                nextVersion: plan.nextVersion
            )
            plan.markResealed(recordID)
            try await journalStore.saveRotationPlan(plan)
            return plan
        }
        let encryptedValue = try sealer.reseal(
            record.encryptedValue,
            currentVersion: plan.currentVersion,
            nextVersion: plan.nextVersion
        )
        guard encryptedValue.keyVersion == plan.nextVersion else {
            throw CompanionKeyRotationError.unsupportedRecordKeyVersion(
                recordID: recordID,
                keyVersion: encryptedValue.keyVersion
            )
        }
        let replacement = replacingEncryptedValue(
            of: record,
            with: encryptedValue
        )
        let outcomes = try await recordStore.mergeRecords(
            [replacement],
            policy: .authoritativeIncoming
        )
        guard outcomes.count == 1,
              outcomes[0].snapshot == replacement,
              try await recordStore.record(for: recordID) == replacement else {
            throw CompanionKeyRotationError.localRecordReadbackMismatch(recordID)
        }
        plan.markResealed(recordID)
        try await journalStore.saveRotationPlan(plan)
        return plan
    }

    private func validatedLocalRecords(
        for plan: CompanionKeyRotationPlan
    ) async throws -> [SyncRecord] {
        let records = try await recordStore.allRecords()
            .sorted { $0.recordID.uuidString < $1.recordID.uuidString }
        guard records.map(\.recordID) == plan.scheduledRecordIDs else {
            throw CompanionKeyRotationError.localStateChangedAfterAcknowledgement
        }
        for record in records {
            guard record.encryptedValue.keyVersion == plan.nextVersion else {
                throw CompanionKeyRotationError.unsupportedRecordKeyVersion(
                    recordID: record.recordID,
                    keyVersion: record.encryptedValue.keyVersion
                )
            }
            _ = try sealer.reseal(
                record.encryptedValue,
                currentVersion: plan.currentVersion,
                nextVersion: plan.nextVersion
            )
        }
        return records
    }

    private func validateRecordVersion(
        _ record: SyncRecord,
        plan: CompanionKeyRotationPlan
    ) throws {
        guard record.encryptedValue.keyVersion == plan.currentVersion ||
                record.encryptedValue.keyVersion == plan.nextVersion else {
            throw CompanionKeyRotationError.unsupportedRecordKeyVersion(
                recordID: record.recordID,
                keyVersion: record.encryptedValue.keyVersion
            )
        }
    }

    private func replacingEncryptedValue(
        of record: SyncRecord,
        with encryptedValue: EncryptedValue
    ) -> SyncRecord {
        SyncRecord(
            recordID: record.recordID,
            entityID: record.entityID,
            schemaVersion: record.schemaVersion,
            dataClass: record.dataClass,
            modifiedAt: record.modifiedAt,
            originatingDevice: record.originatingDevice,
            orderKey: record.orderKey,
            encryptedValue: encryptedValue,
            tombstone: record.tombstone
        )
    }

    private func makeManifest(
        plan: CompanionKeyRotationPlan,
        records: [SyncRecord]
    ) throws -> CompanionKeyRotationManifest {
        CompanionKeyRotationManifest(
            planID: plan.id,
            currentVersion: plan.currentVersion,
            nextVersion: plan.nextVersion,
            transitionEndsAt: plan.transitionEndsAt,
            recordIDs: records.map(\.recordID),
            recordsDigest: try recordsDigest(records)
        )
    }

    private func validate(
        readback: CompanionKeyRotationRemoteReadback,
        manifest: CompanionKeyRotationManifest,
        expectedRecords: [SyncRecord]
    ) throws -> CompanionKeyRotationAcknowledgement {
        guard readback.planID == manifest.planID,
              readback.keyVersion == manifest.nextVersion else {
            throw CompanionKeyRotationError.remoteReadbackIdentityMismatch
        }
        let records = readback.records.sorted {
            $0.recordID.uuidString < $1.recordID.uuidString
        }
        guard records == expectedRecords,
              try recordsDigest(records) == manifest.recordsDigest else {
            throw CompanionKeyRotationError.remoteReadbackRecordsMismatch
        }
        return CompanionKeyRotationAcknowledgement(
            planID: manifest.planID,
            keyVersion: manifest.nextVersion,
            recordIDs: manifest.recordIDs,
            recordsDigest: manifest.recordsDigest,
            readBackAt: readback.readBackAt
        )
    }

    private func recordsDigest(_ records: [SyncRecord]) throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        let canonicalRecords = records.sorted {
            $0.recordID.uuidString < $1.recordID.uuidString
        }
        return Data(SHA256.hash(data: try encoder.encode(canonicalRecords)))
    }
}
