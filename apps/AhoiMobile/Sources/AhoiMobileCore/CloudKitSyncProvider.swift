import Foundation
import AhoiCloudKitSpike

private func SyncText(_ key: String, _ fallback: String) -> String {
    CompanionL10n.string(key, fallback: fallback)
}

#if canImport(CloudKit)
import CloudKit

public struct CloudKitSyncConfiguration: Hashable, Sendable {
    public let containerIdentifier: String
    public let zoneName: String
    public let automaticallySync: Bool
    public let subscriptionID: String?

    public init(
        containerIdentifier: String,
        zoneName: String = "AhoiBrowserSyncZone",
        automaticallySync: Bool = true,
        subscriptionID: String? = nil
    ) {
        self.containerIdentifier = containerIdentifier
        self.zoneName = zoneName
        self.automaticallySync = automaticallySync
        self.subscriptionID = subscriptionID
    }
}

public enum CloudKitSyncProviderError: Error, Equatable, Sendable {
    case invalidContainerIdentifier
    case invalidZoneName
    case unavailable
    case rawRecordDeletionUnsupported
    case accountTransitionRequiresConfirmation
    case zoneRecoveryRequiresConfirmation
}

public enum CloudKitSyncPhase: String, Codable, Sendable {
    case idle
    case preparing
    case syncing
    case offline
    case accountRequired
    case retryScheduled
    case conflictResolved
    case quarantined
    case failed
}

public struct CloudKitSyncStatus: Codable, Equatable, Sendable {
    public let phase: CloudKitSyncPhase
    public let detail: String
    public let retryAfterSeconds: Double?
    public let updatedAt: Date

    public init(
        phase: CloudKitSyncPhase,
        detail: String,
        retryAfterSeconds: Double? = nil,
        updatedAt: Date = Date()
    ) {
        self.phase = phase
        self.detail = detail
        self.retryAfterSeconds = retryAfterSeconds
        self.updatedAt = updatedAt
    }
}

/// CKSyncEngine serializes delegate events. This store keeps its state across
/// launches exactly as Apple requires, while allowing tests to avoid disk and
/// iCloud entirely.
public protocol SyncEngineStateStore: Sendable {
    func load() throws -> CKSyncEngine.State.Serialization?
    func save(_ serialization: CKSyncEngine.State.Serialization) throws
    func clear() throws
    func loadSafetyState() throws -> CloudKitSyncSafetyState
    func saveSafetyState(_ state: CloudKitSyncSafetyState) throws
}

public struct CloudKitSyncSafetyState: Codable, Equatable, Sendable {
    public var accountTransitionPending: Bool
    public var zoneRecoveryPending: Bool

    public init(
        accountTransitionPending: Bool = false,
        zoneRecoveryPending: Bool = false
    ) {
        self.accountTransitionPending = accountTransitionPending
        self.zoneRecoveryPending = zoneRecoveryPending
    }
}

public final class InMemorySyncEngineStateStore: SyncEngineStateStore, @unchecked Sendable {
    private let lock = NSLock()
    private var serialization: CKSyncEngine.State.Serialization?
    private var safetyState: CloudKitSyncSafetyState

    public init(
        serialization: CKSyncEngine.State.Serialization? = nil,
        safetyState: CloudKitSyncSafetyState = .init()
    ) {
        self.serialization = serialization
        self.safetyState = safetyState
    }

    public func load() throws -> CKSyncEngine.State.Serialization? {
        lock.withLock { serialization }
    }

    public func save(_ serialization: CKSyncEngine.State.Serialization) throws {
        lock.withLock { self.serialization = serialization }
    }

    public func clear() throws {
        lock.withLock { serialization = nil }
    }

    public func loadSafetyState() throws -> CloudKitSyncSafetyState {
        lock.withLock { safetyState }
    }

    public func saveSafetyState(_ state: CloudKitSyncSafetyState) throws {
        lock.withLock { safetyState = state }
    }
}

public final class FileSyncEngineStateStore: SyncEngineStateStore, @unchecked Sendable {
    private let fileURL: URL
    private let safetyURL: URL
    private let lock = NSLock()
    private let encoder = JSONEncoder()
    private let decoder = JSONDecoder()

    public init(fileURL: URL) {
        self.fileURL = fileURL
        self.safetyURL = fileURL.appendingPathExtension("safety")
        encoder.outputFormatting = [.sortedKeys]
    }

    public func load() throws -> CKSyncEngine.State.Serialization? {
        try lock.withLock {
            guard FileManager.default.fileExists(atPath: fileURL.path) else {
                return nil
            }
            return try decoder.decode(
                CKSyncEngine.State.Serialization.self,
                from: Data(contentsOf: fileURL)
            )
        }
    }

    public func save(_ serialization: CKSyncEngine.State.Serialization) throws {
        try lock.withLock {
            let data = try encoder.encode(serialization)
            try FileManager.default.createDirectory(
                at: fileURL.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try data.write(to: fileURL, options: [.atomic])
        }
    }

    public func clear() throws {
        try lock.withLock {
            guard FileManager.default.fileExists(atPath: fileURL.path) else { return }
            try FileManager.default.removeItem(at: fileURL)
        }
    }

    public func loadSafetyState() throws -> CloudKitSyncSafetyState {
        try lock.withLock {
            guard FileManager.default.fileExists(atPath: safetyURL.path) else {
                return .init()
            }
            return try decoder.decode(
                CloudKitSyncSafetyState.self,
                from: Data(contentsOf: safetyURL)
            )
        }
    }

    public func saveSafetyState(_ state: CloudKitSyncSafetyState) throws {
        try lock.withLock {
            let data = try encoder.encode(state)
            try FileManager.default.createDirectory(
                at: safetyURL.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try data.write(to: safetyURL, options: [.atomic])
        }
    }
}

/// Native private-database/custom-zone provider. It deliberately does not
/// expose raw record deletion: user deletion is represented by an encrypted
/// tombstone record and is reconciled by the shared boundary before upload.
@available(iOS 17.0, macOS 14.0, *)
public final class CloudKitSyncProvider: NSObject, @unchecked Sendable, CKSyncEngineDelegate {
    private let configuration: CloudKitSyncConfiguration
    private let boundary: SyncBoundary
    private let recordStore: any LocalSyncRecordStore
    private let stateStore: any SyncEngineStateStore
    private let quarantineStore: any SyncQuarantineStore
    private let codec = AppleCloudKitRecordCodec()
    private let zoneID: CKRecordZone.ID
    private let database: CKDatabase
    private let statusLock = NSLock()
    private var currentStatus = CloudKitSyncStatus(
        phase: .idle,
        detail: CompanionL10n.string(
            "sync.status.not_yet_synced",
            fallback: "Not synced yet"
        )
    )
    private var engine: CKSyncEngine?
    private var accountTransitionPending = false
    private var zoneRecoveryPending = false
    private var zonePreparationInProgress = false

    public init(
        configuration: CloudKitSyncConfiguration,
        recordStore: any LocalSyncRecordStore,
        stateStore: any SyncEngineStateStore,
        quarantineStore: any SyncQuarantineStore = InMemorySyncQuarantineStore(),
        boundary: SyncBoundary = .init()
    ) throws {
        guard configuration.containerIdentifier.hasPrefix("iCloud."),
              configuration.containerIdentifier.count > "iCloud.".count else {
            throw CloudKitSyncProviderError.invalidContainerIdentifier
        }
        guard !configuration.zoneName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw CloudKitSyncProviderError.invalidZoneName
        }

        self.configuration = configuration
        self.boundary = boundary
        self.recordStore = recordStore
        self.stateStore = stateStore
        self.quarantineStore = quarantineStore
        let safety = try stateStore.loadSafetyState()
        self.accountTransitionPending = safety.accountTransitionPending
        self.zoneRecoveryPending = safety.zoneRecoveryPending
        let container = CKContainer(identifier: configuration.containerIdentifier)
        self.database = container.privateCloudDatabase
        self.zoneID = CKRecordZone.ID(
            zoneName: configuration.zoneName,
            ownerName: CKCurrentUserDefaultName
        )
        super.init()

        var engineConfiguration = CKSyncEngine.Configuration(
            database: database,
            stateSerialization: try stateStore.load(),
            delegate: self
        )
        engineConfiguration.automaticallySync = configuration.automaticallySync
        engineConfiguration.subscriptionID = configuration.subscriptionID
        self.engine = CKSyncEngine(engineConfiguration)
    }

    public func status() -> CloudKitSyncStatus {
        statusLock.withLock { currentStatus }
    }

    public func safetyState() -> CloudKitSyncSafetyState {
        statusLock.withLock {
            .init(
                accountTransitionPending: accountTransitionPending,
                zoneRecoveryPending: zoneRecoveryPending
            )
        }
    }

    public func engineDescription() -> String? {
        engine?.description
    }

    /// Schedules the custom zone through CKSyncEngine and then sends pending
    /// local changes. No network call is made until this method is invoked or
    /// automatic sync is enabled by the caller.
    public func prepare() async throws {
        guard let engine else { throw CloudKitSyncProviderError.unavailable }
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        statusLock.withLock { zonePreparationInProgress = true }
        defer { statusLock.withLock { zonePreparationInProgress = false } }
        setStatus(.init(phase: .preparing, detail: SyncText(
            "sync.status.preparing_zone", "Preparing sync zone"
        )))
        engine.state.add(pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))])
        do {
            try await engine.sendChanges()
            setStatus(.init(phase: .idle, detail: SyncText("sync.status.ready", "Ready")))
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            throw error
        }
    }

    public func syncNow() async throws {
        guard let engine else { throw CloudKitSyncProviderError.unavailable }
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        setStatus(.init(phase: .syncing, detail: SyncText(
            "sync.status.syncing", "Syncing with CloudKit"
        )))
        do {
            try await engine.fetchChanges(
                .init(scope: .zoneIDs([zoneID]))
            )
            try await engine.sendChanges(
                .init(scope: .zoneIDs([zoneID]))
            )
            setStatus(.init(phase: .idle, detail: SyncText(
                "sync.status.synced", "Synced"
            )))
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            throw error
        }
    }

    public func enqueue(_ record: SyncRecord) async throws {
        try boundary.authorize(record)
        guard let engine else { throw CloudKitSyncProviderError.unavailable }
        try await recordStore.upsert(record)
        let cloudID = CKRecord.ID(
            recordName: record.recordID.uuidString.lowercased(),
            zoneID: zoneID
        )
        engine.state.add(pendingRecordZoneChanges: [.saveRecord(cloudID)])
    }

    public func allRecords() async throws -> [SyncRecord] {
        try await recordStore.allRecords()
    }

    /// Returns opaque CloudKit envelopes that still need to cross the
    /// authenticated plaintext/domain merge boundary. These are deliberately
    /// separate from the single transport snapshot used by CKSyncEngine.
    public func pendingFetchedRecords() async throws -> [SyncRecord] {
        try await recordStore.fetchedRecords()
    }

    /// Removes exactly the envelope that completed domain import. A distinct
    /// version (including distinct encrypted bytes) remains pending.
    public func acknowledgeFetchedRecord(_ record: SyncRecord) async throws {
        try await recordStore.acknowledgeFetchedRecord(record)
    }

    public func quarantineImportedRecord(_ recordID: UUID, reason: String) async {
        await quarantineStore.quarantine(recordID: recordID, reason: reason)
    }

    /// Account switches are a privacy boundary: records from the previous
    /// account remain local but are not uploaded to the new account until the
    /// product obtains explicit confirmation.
    public func confirmAccountTransition(allowLocalUpload: Bool) async throws {
        guard let engine else { throw CloudKitSyncProviderError.unavailable }
        if allowLocalUpload {
            try await requeueLocalRecords(in: engine)
        }
        try updateSafetyState(accountTransitionPending: false)
        do {
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
        guard let engine else { throw CloudKitSyncProviderError.unavailable }
        engine.state.add(pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))])
        try await engine.sendChanges()
        try await requeueLocalRecords(in: engine)
        try updateSafetyState(zoneRecoveryPending: false)
        setStatus(.init(phase: .idle, detail: SyncText(
            "sync.status.zone_restored", "Sync zone restored"
        )))
    }

    public func pendingRecordCount() -> Int {
        engine?.state.pendingRecordZoneChanges.count ?? 0
    }

    public func cancel() async {
        await engine?.cancelOperations()
    }

    public func handleEvent(
        _ event: CKSyncEngine.Event,
        syncEngine: CKSyncEngine
    ) async {
        switch event {
        case let .stateUpdate(update):
            do {
                try stateStore.save(update.stateSerialization)
            } catch {
                setStatus(.init(
                    phase: .failed,
                    detail: SyncText(
                        "sync.status.state_save_failed",
                        "Sync state could not be saved"
                    )
                ))
            }
        case .accountChange:
            // CKSyncEngine resets its pending state on account changes. Keep
            // local records, clear the persisted engine token, and require a
            // new account-aware setup instead of deleting user data.
            try? stateStore.clear()
            try? updateSafetyState(accountTransitionPending: true)
            setStatus(.init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_changed",
                    "iCloud account changed; local data was retained"
                )
            ))
        case let .fetchedRecordZoneChanges(changes):
            await applyFetched(changes)
        case let .sentRecordZoneChanges(changes):
            await applySent(changes, syncEngine: syncEngine)
        case let .didFetchRecordZoneChanges(result):
            if let error = result.error {
                if error.code == .zoneNotFound ||
                    error.code == .userDeletedZone {
                    try? updateSafetyState(zoneRecoveryPending: true)
                }
                setStatus(classify(error).status)
            }
        case let .didSendChanges(result):
            _ = result
        case let .fetchedDatabaseChanges(changes):
            if !changes.deletions.isEmpty {
                try? updateSafetyState(zoneRecoveryPending: true)
                setStatus(.init(
                    phase: .accountRequired,
                    detail: SyncText(
                        "sync.status.zone_deleted",
                        "CloudKit zone was deleted; local data was retained"
                    )
                ))
            }
        case .sentDatabaseChanges, .willFetchChanges, .willFetchRecordZoneChanges,
             .didFetchChanges, .willSendChanges:
            break
        @unknown default:
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.unknown_event",
                    "Unknown CKSyncEngine event"
                )
            ))
        }
    }

    public func nextRecordZoneChangeBatch(
        _ context: CKSyncEngine.SendChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.RecordZoneChangeBatch? {
        guard !statusLock.withLock({
            accountTransitionPending || zoneRecoveryPending || zonePreparationInProgress
        }) else {
            return nil
        }
        let pending = syncEngine.state.pendingRecordZoneChanges.filter {
            context.options.scope.contains($0)
        }
        guard !pending.isEmpty else { return nil }

        // A previous engine state must never smuggle a raw delete back into a
        // provider whose public contract is tombstone-only.
        let unsupportedDeletes = pending.filter {
            if case .deleteRecord = $0 { return true }
            return false
        }
        if !unsupportedDeletes.isEmpty {
            syncEngine.state.remove(pendingRecordZoneChanges: unsupportedDeletes)
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.raw_delete_discarded",
                    "Raw CloudKit record deletion was discarded"
                )
            ))
        }

        let saves = pending.filter {
            if case .saveRecord = $0 { return true }
            return false
        }
        guard !saves.isEmpty else { return nil }
        return await .init(pendingChanges: saves) { [recordStore, codec, zoneID, boundary] cloudID in
            guard let rawID = UUID(uuidString: cloudID.recordName),
                  let local = try? await recordStore.record(for: rawID),
                  (try? boundary.authorize(local)) != nil else {
                return nil
            }
            return try? codec.encode(local, zoneID: zoneID)
        }
    }

    public func nextFetchChangesOptions(
        _ context: CKSyncEngine.FetchChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.FetchChangesOptions {
        _ = context
        _ = syncEngine
        return .init(scope: .zoneIDs([zoneID]))
    }

    private func applyFetched(
        _ changes: CKSyncEngine.Event.FetchedRecordZoneChanges
    ) async {
        var resolvedConflict = false
        for modification in changes.modifications {
            do {
                let incoming = try codec.decode(modification.record)
                try boundary.authorize(incoming)
                let retained = try await Self.retainFetchedEnvelope(
                    incoming,
                    in: recordStore
                )
                resolvedConflict = resolvedConflict || retained.existing != nil &&
                    retained.snapshot != retained.existing
            } catch {
                await quarantineStore.quarantine(
                    recordID: UUID(uuidString: modification.record.recordID.recordName) ?? UUID(),
                    reason: "invalid_or_denied_cloudkit_record"
                )
                setStatus(.init(
                    phase: .quarantined,
                    detail: SyncText(
                        "sync.status.record_quarantined",
                        "At least one CloudKit record was quarantined"
                    )
                ))
            }
        }
        for deletion in changes.deletions {
            if let recordID = UUID(uuidString: deletion.recordID.recordName) {
                await quarantineStore.quarantine(
                    recordID: recordID,
                    reason: "physical_delete_without_validated_tombstone"
                )
            }
        }
        if resolvedConflict {
            setStatus(.init(
                phase: .conflictResolved,
                detail: SyncText(
                    "sync.status.conflict_merged",
                    "Conflict was merged deterministically"
                )
            ))
        }
    }

    private func applySent(
        _ changes: CKSyncEngine.Event.SentRecordZoneChanges,
        syncEngine: CKSyncEngine
    ) async {
        _ = changes.savedRecords
        guard !changes.failedRecordSaves.isEmpty || !changes.failedRecordDeletes.isEmpty else {
            return
        }

        for failure in changes.failedRecordSaves {
            if let serverRecord = failure.error.serverRecord {
                await reconcileServerRecord(serverRecord, syncEngine: syncEngine)
            }
            let classified = classify(failure.error)
            setStatus(classified.status)
            let cloudError = failure.error
            if cloudError.code == .zoneNotFound || cloudError.code == .userDeletedZone {
                try? updateSafetyState(zoneRecoveryPending: true)
            }
        }
        if !changes.failedRecordDeletes.isEmpty {
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.raw_deletes_forbidden",
                    "Raw CloudKit record deletions remain forbidden"
                )
            ))
        }
    }

    private func reconcileServerRecord(
        _ serverRecord: CKRecord,
        syncEngine: CKSyncEngine
    ) async {
        do {
            let incoming = try codec.decode(serverRecord)
            try boundary.authorize(incoming)
            _ = try await Self.retainFetchedEnvelope(incoming, in: recordStore)
            syncEngine.state.add(pendingRecordZoneChanges: [
                .saveRecord(serverRecord.recordID)
            ])
        } catch {
            await quarantineStore.quarantine(
                recordID: UUID(uuidString: serverRecord.recordID.recordName) ?? UUID(),
                reason: "cloudkit_conflict_record_merge_failed"
            )
        }
    }

    /// Durably stages every distinct inbound encrypted envelope before choosing
    /// the one transport snapshot CKSyncEngine can address by record ID. The
    /// snapshot choice is not a convergence decision: CompanionSyncBridge must
    /// decrypt and import every staged candidate before acknowledging it.
    @discardableResult
    static func retainFetchedEnvelope(
        _ incoming: SyncRecord,
        in recordStore: any LocalSyncRecordStore
    ) async throws -> (existing: SyncRecord?, snapshot: SyncRecord) {
        let existing = try await recordStore.record(for: incoming.recordID)
        if existing != incoming {
            // Persist the lossless inbox first. If the process stops before the
            // transport snapshot write, the domain candidate remains durable.
            try await recordStore.stageFetchedRecord(incoming)
        }
        let snapshot = resolveTransportSnapshot(existing, incoming)
        if snapshot != existing {
            try await recordStore.upsert(snapshot)
        }
        return (existing, snapshot)
    }

    /// Chooses only the opaque record used for future CloudKit upload. LWW is
    /// safe here because retainFetchedEnvelope has already preserved `rhs` for
    /// the authenticated domain/field merge, even when `lhs` wins this choice.
    static func resolveTransportSnapshot(
        _ lhs: SyncRecord?,
        _ rhs: SyncRecord
    ) -> SyncRecord {
        guard let lhs else { return rhs }
        guard let left = try? VersionedValue(
            value: lhs.encryptedValue,
            modifiedAt: lhs.modifiedAt,
            originatingDevice: lhs.originatingDevice,
            isTombstone: lhs.tombstone != nil
        ), let right = try? VersionedValue(
            value: rhs.encryptedValue,
            modifiedAt: rhs.modifiedAt,
            originatingDevice: rhs.originatingDevice,
            isTombstone: rhs.tombstone != nil
        ) else {
            return lhs.modifiedAt < rhs.modifiedAt ? rhs : lhs
        }
        return LastWriterWinsResolver().resolve(left, right) == right ? rhs : lhs
    }

    private func requeueLocalRecords(in syncEngine: CKSyncEngine) async throws {
        let changes = try await recordStore.allRecords().map { record in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(
                CKRecord.ID(
                    recordName: record.recordID.uuidString.lowercased(),
                    zoneID: zoneID
                )
            )
        }
        syncEngine.state.add(pendingRecordZoneChanges: changes)
    }

    private struct ClassifiedError {
        let status: CloudKitSyncStatus
    }

    private func classify(_ error: Error) -> ClassifiedError {
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

    private func setStatus(_ status: CloudKitSyncStatus) {
        statusLock.withLock { currentStatus = status }
    }

    private func updateSafetyState(
        accountTransitionPending account: Bool? = nil,
        zoneRecoveryPending zone: Bool? = nil
    ) throws {
        try statusLock.withLock {
            if let account { accountTransitionPending = account }
            if let zone { zoneRecoveryPending = zone }
            do {
                try stateStore.saveSafetyState(.init(
                    accountTransitionPending: accountTransitionPending,
                    zoneRecoveryPending: zoneRecoveryPending
                ))
            } catch {
                accountTransitionPending = true
                zoneRecoveryPending = true
                try? stateStore.saveSafetyState(.init(
                    accountTransitionPending: true,
                    zoneRecoveryPending: true
                ))
                throw error
            }
        }
    }
}

#else

/// CloudKit is an Apple-platform capability. The app remains local-first on
/// non-Apple hosts; no fake network provider is compiled into production.
public enum CloudKitSyncProviderUnavailable {
    public static let reason = SyncText(
        "sync.status.sdk_unavailable",
        "CloudKit SDK is unavailable"
    )
}

#endif
