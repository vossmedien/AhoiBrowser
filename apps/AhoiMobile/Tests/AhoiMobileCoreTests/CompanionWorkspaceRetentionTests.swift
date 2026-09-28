import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class CompanionWorkspaceRetentionTests: XCTestCase {
    private enum SaveFailure: Error { case simulated }

    private actor FailingStore: LocalCompanionStore {
        private var value: CompanionSnapshot = .empty
        private var failNext = false

        func load() async throws -> CompanionSnapshot { value }
        func save(_ snapshot: CompanionSnapshot) async throws {
            if failNext {
                failNext = false
                throw SaveFailure.simulated
            }
            value = snapshot
        }
        func failNextSave() { failNext = true }
    }

    func testExpiryExactSourceAndAtomicRouteSurviveReload() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiWorkspaceCompaction-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let url = directory.appendingPathComponent("snapshot.json")
        let repository = LocalFirstRepository(store: FileCompanionStore(fileURL: url))
        let source = try await repository.createWorkspace(name: "é")
        let target = try await repository.createWorkspace(name: "Target")
        let offline = LocalFirstRepository(store: InMemoryCompanionStore(
            snapshot: try await repository.currentSnapshot()))
        let merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let late = try await offline.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "Late",
            url: "https://example.test/late")
        _ = try await repository.upsert(late)
        let raw = try DesktopWirePayloadCodec().encode(late)
        let expiry = try XCTUnwrap(merged.tombstone?.purgeAfterMilliseconds)

        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: merged, nowMilliseconds: expiry - 1)
            XCTFail("An unexpired tombstone cannot be compacted")
        } catch {
            XCTAssertEqual(error as? CompanionWorkspaceCompactionError, .notExpiredTombstone)
        }
        var changed = merged
        changed.name = "e\u{301}" // Swift equality can fold this; wire bytes cannot.
        XCTAssertEqual(changed.name, merged.name)
        XCTAssertNotEqual(Data(changed.name.utf8), Data(merged.name.utf8))
        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: changed, nowMilliseconds: expiry)
            XCTFail("The domain record must match the upload candidate exactly")
        } catch {
            XCTAssertEqual(error as? CompanionWorkspaceCompactionError, .sourceChanged)
        }
        let first = try await repository.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: expiry)
        XCTAssertTrue(first)
        let restored = LocalFirstRepository(store: FileCompanionStore(fileURL: url))
        let snapshot = try await restored.currentSnapshot()
        XCTAssertFalse(snapshot.workspaces.contains { $0.id == source.id })
        XCTAssertEqual(snapshot.deletionWatermarks.count, 1)
        XCTAssertEqual(snapshot.deletionWatermarks[0].version, merged.version)
        XCTAssertEqual(snapshot.deletionWatermarks[0].mergedInto, target.id)
        XCTAssertEqual(snapshot.presentationNode(late.id)?.workspaceID, target.id)
        let persistedRaw = try DesktopWirePayloadCodec().encode(
            XCTUnwrap(snapshot.treeNodes.first { $0.id == late.id }))
        XCTAssertEqual(persistedRaw, raw)
        let repeated = try await restored.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: expiry)
        XCTAssertFalse(repeated)
    }

    func testFailedAtomicSaveRetainsPayloadUntilRetry() async throws {
        let store = FailingStore()
        let repository = LocalFirstRepository(store: store)
        let source = try await repository.createWorkspace(name: "Source")
        let target = try await repository.createWorkspace(name: "Target")
        let merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let expiry = try XCTUnwrap(merged.tombstone?.purgeAfterMilliseconds)
        await store.failNextSave()
        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: merged, nowMilliseconds: expiry)
            XCTFail("Injected store failure must stop compaction")
        } catch {
            XCTAssertTrue(error is SaveFailure)
        }
        let afterFailure = try await repository.currentSnapshot()
        XCTAssertEqual(afterFailure.workspaces.first { $0.id == source.id }, merged)
        XCTAssertTrue(afterFailure.deletionWatermarks.isEmpty)
        let persisted = try await store.load()
        XCTAssertEqual(persisted, afterFailure)
        let retried = try await repository.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: expiry)
        XCTAssertTrue(retried)
    }

    func testShortRemotePurgeAfterCannotReduceThirtyDayFloor() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let source = try await repository.createWorkspace(name: "Source")
        let target = try await repository.createWorkspace(name: "Target")
        var merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let original = try XCTUnwrap(merged.tombstone)
        merged.tombstone = Tombstone(
            entityID: original.entityID, deletedAt: original.deletedAt,
            deletedBy: original.deletedBy, originalParentID: original.originalParentID,
            originalOrderKey: original.originalOrderKey,
            purgeAfterMilliseconds: original.deletedAt.physicalMilliseconds + 1)
        var snapshot = try await repository.currentSnapshot()
        let index = try XCTUnwrap(snapshot.workspaces.firstIndex { $0.id == source.id })
        snapshot.workspaces[index] = merged
        try await repository.replace(snapshot)
        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: merged, nowMilliseconds: original.deletedAt.physicalMilliseconds + 1)
            XCTFail("A shortened remote purge hint cannot bypass 30 days")
        } catch {
            XCTAssertEqual(error as? CompanionWorkspaceCompactionError, .notExpiredTombstone)
        }
        let floor = original.deletedAt.physicalMilliseconds + 30 * 24 * 60 * 60 * 1_000
        let compacted = try await repository.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: floor)
        XCTAssertTrue(compacted)
    }

#if DEBUG
    func testTransientCompactionLeaseLossDefersWithoutThrowing() async throws {
        let (bridge, repository, transport, sourceID, expiry) = try await makeMergeBridge()
        transport.setMode(.leaseLost)
        try await bridge.compactAcknowledgedWorkspaces(nowMilliseconds: expiry)
        XCTAssertEqual(transport.compactionCalls, 1)
        let snapshot = try await repository.currentSnapshot()
        XCTAssertNotNil(snapshot.workspaces.first { $0.id == sourceID }?.tombstone)
        XCTAssertTrue(snapshot.deletionWatermarks.isEmpty)
        // performBoundedSyncNow runs the same maintenance; it must not fail.
        try await bridge.syncNow()
    }

    func testCacheRefusalAfterDomainCommitRetriesIdempotently() async throws {
        let (bridge, repository, transport, sourceID, expiry) = try await makeMergeBridge()
        transport.setMode(.refuseAfterCommit)
        try await bridge.compactAcknowledgedWorkspaces(nowMilliseconds: expiry)
        var snapshot = try await repository.currentSnapshot()
        XCTAssertFalse(snapshot.workspaces.contains { $0.id == sourceID })
        XCTAssertEqual(snapshot.deletionWatermarks.map(\.entityID), [sourceID.rawValue])
        let cached = try await transport.locallyPersistedRecord(
            forRecordID: sourceID.rawValue)
        XCTAssertNotNil(cached?.tombstone)
        transport.setMode(.commitOnly)
        // The committed watermark plus the retained cache copy re-enter the
        // exact idempotent domain branch instead of failing or resurrecting.
        try await bridge.compactAcknowledgedWorkspaces(nowMilliseconds: expiry)
        XCTAssertEqual(transport.compactionCalls, 2)
        snapshot = try await repository.currentSnapshot()
        XCTAssertFalse(snapshot.workspaces.contains { $0.id == sourceID })
        XCTAssertEqual(snapshot.deletionWatermarks.count, 1)
    }

    private func makeMergeBridge() async throws -> (
        CompanionSyncBridge, LocalFirstRepository, LeaseFaultTransport, WorkspaceID, UInt64
    ) {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let source = try await repository.createWorkspace(name: "Source")
        let target = try await repository.createWorkspace(name: "Target")
        let merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let tombstone = try XCTUnwrap(merged.tombstone)
        let floor = tombstone.deletedAt.physicalMilliseconds + 30 * 24 * 60 * 60 * 1_000
        let transport = LeaseFaultTransport(
            base: CompanionSyncVisibleTestTransport(recordStore: InMemorySyncRecordStore()))
        let sealer = KeychainCompanionPayloadSealer(
            configuration: .init(service: "retention-lease-test", account: "fixture",
                                 keyVersion: 1),
            keyLoader: { Data(repeating: 0x42, count: 32) })
        let bridge = CompanionSyncBridge(
            repository: repository, transport: transport, sealer: sealer)
        try await bridge.enqueueLocalSnapshot()
        let cached = try await transport.locallyPersistedRecord(
            forRecordID: source.id.rawValue)
        XCTAssertNotNil(cached?.tombstone)
        return (bridge, repository, transport, source.id,
                max(floor, tombstone.purgeAfterMilliseconds))
    }
#endif
}

#if DEBUG
/// Forwards to the visible in-memory transport but replaces the receipt-backed
/// compaction lease with deterministic lease/cache faults. It never grants a
/// real CloudKit acknowledgement.
private final class LeaseFaultTransport: CompanionSyncTransporting, @unchecked Sendable {
    enum Mode { case leaseLost, refuseAfterCommit, commitOnly }

    private let base: CompanionSyncVisibleTestTransport
    private let lock = NSLock()
    private var mode = Mode.leaseLost
    private var calls = 0

    init(base: CompanionSyncVisibleTestTransport) { self.base = base }

    var compactionCalls: Int { lock.withLock { calls } }
    func setMode(_ mode: Mode) { lock.withLock { self.mode = mode } }

    func compactAcknowledgedWorkspace(
        _ record: SyncRecord,
        domainCommit: @Sendable () async throws -> Void
    ) async throws -> Bool {
        let mode = lock.withLock { () -> Mode in calls += 1; return self.mode }
        switch mode {
        case .leaseLost:
            throw CloudKitSyncProviderError.unavailable
        case .refuseAfterCommit:
            try await domainCommit()
            throw UploadedTombstoneReceiptError.pendingFetchedEnvelope
        case .commitOnly:
            try await domainCommit()
            return false
        }
    }

    func setBookmarkCategoryApproved(_ approved: Bool) {
        base.setBookmarkCategoryApproved(approved)
    }
    func status() -> CloudKitSyncStatus { base.status() }
    func allRecords() async throws -> [SyncRecord] { try await base.allRecords() }
    func records(forRecordIDs recordIDs: [UUID]) async throws -> [SyncRecord] {
        try await base.records(forRecordIDs: recordIDs)
    }
    func locallyPersistedRecord(forRecordID recordID: UUID) async throws -> SyncRecord? {
        try await base.locallyPersistedRecord(forRecordID: recordID)
    }
    func enqueue(
        _ record: SyncRecord, authorization: SyncAuthorizationContext
    ) async throws {
        try await base.enqueue(record, authorization: authorization)
    }
    func currentDeveloperAssetAuthorizationMutationEpoch() -> UInt64 {
        base.currentDeveloperAssetAuthorizationMutationEpoch()
    }
    func enqueueLocalSnapshot(
        _ records: [SyncRecord], authorizedDeveloperAssetIDs: Set<UUID>,
        scanStartedAtMutationEpoch: UInt64
    ) async throws {
        try await base.enqueueLocalSnapshot(
            records, authorizedDeveloperAssetIDs: authorizedDeveloperAssetIDs,
            scanStartedAtMutationEpoch: scanStartedAtMutationEpoch)
    }
    func commitImportedDomainResults(
        records: [SyncRecord], authorizedDeveloperAssetIDs: Set<UUID>,
        revokedDeveloperAssetIDs: Set<UUID>
    ) async throws {
        try await base.commitImportedDomainResults(
            records: records, authorizedDeveloperAssetIDs: authorizedDeveloperAssetIDs,
            revokedDeveloperAssetIDs: revokedDeveloperAssetIDs)
    }
    func fetchChanges() async throws -> UInt64 { try await base.fetchChanges() }
    func sendPendingChanges(passID: UInt64) async throws {
        try await base.sendPendingChanges(passID: passID)
    }
    func finalizeBoundedSync(passID: UInt64) async throws {
        try await base.finalizeBoundedSync(passID: passID)
    }
    func abortBoundedSyncPass(_ passID: UInt64) { base.abortBoundedSyncPass(passID) }
    func beginDomainMergeActivity() throws { try base.beginDomainMergeActivity() }
    func endDomainMergeActivity() { base.endDomainMergeActivity() }
    func pendingFetchedRecords() async throws -> [SyncRecord] {
        try await base.pendingFetchedRecords()
    }
    func pendingQuarantineRecoveryRecords() async throws -> [SyncRecord] {
        try await base.pendingQuarantineRecoveryRecords()
    }
    func quarantineImportedRecord(_ record: SyncRecord, reason: String) async throws {
        try await base.quarantineImportedRecord(record, reason: reason)
    }
    func resolveQuarantinedRecord(_ record: SyncRecord) async throws {
        try await base.resolveQuarantinedRecord(record)
    }
    func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws {
        try await base.acknowledgeFetchedRecords(records)
    }
    func hasPhysicalDeletionQuarantine() async -> Bool {
        await base.hasPhysicalDeletionQuarantine()
    }
    func physicalDeletionRecoveryCandidates() async throws -> [(
        record: SyncRecord, generation: UUID
    )] {
        try await base.physicalDeletionRecoveryCandidates()
    }
    func restorePhysicallyDeletedRecord(
        _ record: SyncRecord, expectedGeneration: UUID
    ) async throws -> Bool {
        try await base.restorePhysicallyDeletedRecord(
            record, expectedGeneration: expectedGeneration)
    }
    func acceptPhysicalDeletion(
        recordID: UUID, expectedGeneration: UUID
    ) async throws -> Bool {
        try await base.acceptPhysicalDeletion(
            recordID: recordID, expectedGeneration: expectedGeneration)
    }
}
#endif
