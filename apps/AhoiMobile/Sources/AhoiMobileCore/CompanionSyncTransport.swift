import Foundation
import AhoiCloudKitSpike

/// Internal bridge boundary shared by the real CloudKit transport and the
/// explicit DEBUG visible-E2E projection. The public bridge API remains bound
/// to `CloudKitSyncProvider`; production never substitutes a fake provider.
protocol CompanionSyncTransporting: AnyObject, Sendable {
    func setBookmarkCategoryApproved(_ approved: Bool)
    func status() -> CloudKitSyncStatus
    func allRecords() async throws -> [SyncRecord]
    func records(forRecordIDs recordIDs: [UUID]) async throws -> [SyncRecord]
    func locallyPersistedRecord(forRecordID recordID: UUID) async throws -> SyncRecord?
    func enqueue(
        _ record: SyncRecord,
        authorization: SyncAuthorizationContext
    ) async throws
    func currentDeveloperAssetAuthorizationMutationEpoch() -> UInt64
    func enqueueLocalSnapshot(
        _ records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        scanStartedAtMutationEpoch: UInt64
    ) async throws
    func commitImportedDomainResults(
        records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        revokedDeveloperAssetIDs: Set<UUID>
    ) async throws
    func fetchChanges() async throws -> UInt64
    func sendPendingChanges(passID: UInt64) async throws
    func finalizeBoundedSync(passID: UInt64) async throws
    func abortBoundedSyncPass(_ passID: UInt64)
    func beginDomainMergeActivity() throws
    func endDomainMergeActivity()
    func pendingFetchedRecords() async throws -> [SyncRecord]
    func pendingQuarantineRecoveryRecords() async throws -> [SyncRecord]
    func quarantineImportedRecord(_ record: SyncRecord, reason: String) async throws
    func resolveQuarantinedRecord(_ record: SyncRecord) async throws
    func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws
    func hasPhysicalDeletionQuarantine() async -> Bool
    func physicalDeletionRecoveryCandidates() async throws -> [(
        record: SyncRecord,
        generation: UUID
    )]
    func restorePhysicallyDeletedRecord(
        _ record: SyncRecord,
        expectedGeneration: UUID
    ) async throws -> Bool
    func acceptPhysicalDeletion(
        recordID: UUID,
        expectedGeneration: UUID
    ) async throws -> Bool
}

extension CompanionSyncTransporting {
    func enqueue(_ record: SyncRecord) async throws {
        try await enqueue(record, authorization: .init())
    }
}

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider: CompanionSyncTransporting {}

#if DEBUG
/// Deterministic transport for the explicit visible Sync UI journey. It keeps
/// the production repository, payload codec, boundary, and bridge in play but
/// deliberately creates neither CKContainer nor network traffic.
final class CompanionSyncVisibleTestTransport: CompanionSyncTransporting,
    @unchecked Sendable {
    private let recordStore: InMemorySyncRecordStore
    private let quarantineStore: InMemorySyncQuarantineStore
    private let boundary: SyncBoundary
    private let lock = NSLock()
    private var pendingRecordIDs = Set<UUID>()
    private var authorizationMutationEpoch: UInt64 = 0
    private var nextPassID: UInt64 = 1
    private var activePassID: UInt64?
    private var domainMergeActivityCount = 0
    private let bookmarkTransportAuthorization = BookmarkTransportAuthorization()

    init(
        recordStore: InMemorySyncRecordStore,
        quarantineStore: InMemorySyncQuarantineStore = .init(),
        boundary: SyncBoundary = .init()
    ) {
        self.recordStore = recordStore
        self.quarantineStore = quarantineStore
        self.boundary = boundary
    }

    func status() -> CloudKitSyncStatus {
        .init(phase: .idle, detail: "Visible E2E in-memory transport")
    }

    func setBookmarkCategoryApproved(_ approved: Bool) {
        bookmarkTransportAuthorization.setApproved(approved)
    }

    func allRecords() async throws -> [SyncRecord] {
        try await recordStore.allRecords()
    }

    func records(forRecordIDs recordIDs: [UUID]) async throws -> [SyncRecord] {
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

    func locallyPersistedRecord(forRecordID recordID: UUID) async throws -> SyncRecord? {
        try await recordStore.record(for: recordID)
    }

    func enqueue(
        _ record: SyncRecord,
        authorization: SyncAuthorizationContext
    ) async throws {
        try bookmarkTransportAuthorization.authorize(record)
        try boundary.authorize(record, context: authorization)
        try await recordStore.upsert(record)
        lock.withLock {
            pendingRecordIDs.insert(record.recordID)
            if record.dataClass == .developerAsset {
                authorizationMutationEpoch &+= 1
            }
        }
    }

    func currentDeveloperAssetAuthorizationMutationEpoch() -> UInt64 {
        lock.withLock { authorizationMutationEpoch }
    }

    func enqueueLocalSnapshot(
        _ records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        scanStartedAtMutationEpoch _: UInt64
    ) async throws {
        let allowedRecords = records.filter { record in
            record.dataClass != .developerAsset || record.tombstone != nil ||
                authorizedDeveloperAssetIDs.contains(record.entityID)
        }
        let authorization = SyncAuthorizationContext(
            optedInDeveloperAssetIDs: authorizedDeveloperAssetIDs
        )
        for record in allowedRecords {
            try bookmarkTransportAuthorization.authorize(record)
            try boundary.authorize(record, context: authorization)
        }
        try await recordStore.upsert(allowedRecords)
        lock.withLock {
            pendingRecordIDs.formUnion(allowedRecords.map(\.recordID))
        }
    }

    func commitImportedDomainResults(
        records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        revokedDeveloperAssetIDs: Set<UUID>
    ) async throws {
        let authorization = SyncAuthorizationContext(
            optedInDeveloperAssetIDs: authorizedDeveloperAssetIDs
        )
        for record in records {
            try bookmarkTransportAuthorization.authorize(record)
            try boundary.authorize(record, context: authorization)
        }
        try await recordStore.upsert(records)
        lock.withLock {
            pendingRecordIDs.subtract(revokedDeveloperAssetIDs)
            pendingRecordIDs.formUnion(records.map(\.recordID))
            if !authorizedDeveloperAssetIDs.isEmpty || !revokedDeveloperAssetIDs.isEmpty {
                authorizationMutationEpoch &+= 1
            }
        }
    }

    func fetchChanges() async throws -> UInt64 {
        try lock.withLock {
            guard activePassID == nil else {
                throw CloudKitSyncProviderError.boundedSyncPassAlreadyActive
            }
            let passID = nextPassID
            nextPassID &+= 1
            activePassID = passID
            return passID
        }
    }

    func sendPendingChanges(passID: UInt64) async throws {
        try requireActivePass(passID)
        lock.withLock { pendingRecordIDs.removeAll(keepingCapacity: true) }
    }

    func finalizeBoundedSync(passID: UInt64) async throws {
        try lock.withLock {
            guard activePassID == passID else {
                throw CloudKitSyncProviderError.boundedSyncPassRequired
            }
            activePassID = nil
        }
    }

    func abortBoundedSyncPass(_ passID: UInt64) {
        lock.withLock {
            if activePassID == passID { activePassID = nil }
        }
    }

    func beginDomainMergeActivity() throws {
        lock.withLock { domainMergeActivityCount += 1 }
    }

    func endDomainMergeActivity() {
        lock.withLock {
            precondition(domainMergeActivityCount > 0)
            domainMergeActivityCount -= 1
        }
    }

    func pendingFetchedRecords() async throws -> [SyncRecord] {
        try await recordStore.fetchedRecords()
    }

    func pendingQuarantineRecoveryRecords() async throws -> [SyncRecord] {
        let quarantined = await quarantineStore.allQuarantined()
        var records: [SyncRecord] = []
        for (recordID, reason) in quarantined.sorted(by: {
            $0.key.uuidString < $1.key.uuidString
        }) where reason != "physical_delete_without_validated_tombstone" {
            if let record = try await recordStore.record(for: recordID) {
                records.append(record)
            }
        }
        return records
    }

    func quarantineImportedRecord(_ record: SyncRecord, reason: String) async throws {
        _ = await quarantineStore.quarantine(recordID: record.recordID, reason: reason)
    }

    func resolveQuarantinedRecord(_ record: SyncRecord) async throws {
        guard let entry = await quarantineStore.entry(for: record.recordID),
              entry.reason != "physical_delete_without_validated_tombstone" else {
            return
        }
        _ = await quarantineStore.remove(
            recordID: record.recordID,
            expectedGeneration: entry.generation
        )
    }

    func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws {
        try await recordStore.acknowledgeFetchedRecords(records)
    }

    func hasPhysicalDeletionQuarantine() async -> Bool {
        (await quarantineStore.allQuarantined()).values.contains(
            "physical_delete_without_validated_tombstone"
        )
    }

    func physicalDeletionRecoveryCandidates() async throws -> [(
        record: SyncRecord,
        generation: UUID
    )] {
        let quarantined = await quarantineStore.allQuarantined()
        var candidates: [(record: SyncRecord, generation: UUID)] = []
        for (recordID, reason) in quarantined.sorted(by: {
            $0.key.uuidString < $1.key.uuidString
        }) where reason == "physical_delete_without_validated_tombstone" {
            guard let entry = await quarantineStore.entry(for: recordID),
                  let record = try await recordStore.record(for: recordID) else {
                continue
            }
            candidates.append((record: record, generation: entry.generation))
        }
        return candidates
    }

    func restorePhysicallyDeletedRecord(
        _ record: SyncRecord,
        expectedGeneration: UUID
    ) async throws -> Bool {
        guard let entry = await quarantineStore.entry(for: record.recordID),
              entry.reason == "physical_delete_without_validated_tombstone",
              entry.generation == expectedGeneration,
              try await recordStore.record(for: record.recordID) == record else {
            return false
        }
        try boundary.authorize(record)
        let removed = await quarantineStore.remove(
            recordID: record.recordID,
            expectedGeneration: expectedGeneration
        )
        if removed {
            _ = lock.withLock { pendingRecordIDs.insert(record.recordID) }
        }
        return removed
    }

    func acceptPhysicalDeletion(
        recordID: UUID,
        expectedGeneration: UUID
    ) async throws -> Bool {
        await quarantineStore.remove(
            recordID: recordID,
            expectedGeneration: expectedGeneration
        )
    }

    func pendingRecordCount() -> Int {
        lock.withLock { pendingRecordIDs.count }
    }

    private func requireActivePass(_ passID: UInt64) throws {
        try lock.withLock {
            guard activePassID == passID else {
                throw CloudKitSyncProviderError.boundedSyncPassRequired
            }
        }
    }
}
#endif
