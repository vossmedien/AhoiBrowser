import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

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
    case boundedSyncPassRequired
    case boundedSyncPassAlreadyActive
    case physicalDeletionRecoveryIncomplete
    case transportRehydrationRecordMissing
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
    /// Opaque CloudKit user-record name for fail-closed account-continuity
    /// checks. It never contains an email address or other profile data.
    public var lastKnownAccountIdentifier: String?

    public init(
        accountTransitionPending: Bool = false,
        zoneRecoveryPending: Bool = false,
        lastKnownAccountIdentifier: String? = nil
    ) {
        self.accountTransitionPending = accountTransitionPending
        self.zoneRecoveryPending = zoneRecoveryPending
        self.lastKnownAccountIdentifier = lastKnownAccountIdentifier
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
    private struct QuarantineResolutionCandidate {
        let record: SyncRecord
        let generation: UUID
    }

    private let configuration: CloudKitSyncConfiguration
    private let boundary: SyncBoundary
    private let recordStore: any LocalSyncRecordStore
    private let stateStore: any SyncEngineStateStore
    private let quarantineStore: any SyncQuarantineStore
    private let systemFieldsStore: any CloudKitSystemFieldsStore
    private let codec = AppleCloudKitRecordCodec()
    private let zoneID: CKRecordZone.ID
    private let container: CKContainer
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
    private var engineReplacementInProgress = false
    private var accountTransitionPending = false
    private var zoneRecoveryPending = false
    private var lastKnownAccountIdentifier: String?
    private var accountContinuityVerified = false
    private var zonePreparationInProgress = false
    private var isInvalidated = false
    private var activeActivityCount = 0
    private var activityDrainWaiters: [CheckedContinuation<Void, Never>] = []
    private var activityEpoch: UInt64 = 0
    private var boundedSyncPassID: UInt64 = 0
    private var boundedSyncPassActive = false
    private var boundedSyncPassCompletionBlocked = false
    private var boundedSyncPassOutboundBlocked = false
    private var outboundBatchWindowDepth = 0
    private var eventDrivenSyncHandler: (@Sendable () -> Void)?
    private var statePersistenceBlocked = false
    private var quarantineResolutionCandidates: [UUID: QuarantineResolutionCandidate] = [:]
    private var authorizedDeveloperAssetIDs: Set<UUID> = []
    private var developerAssetAuthorizationMutationEpoch: UInt64 = 0
    private var developerAssetAuthorizationLastMutation: [UUID: UInt64] = [:]
    /// Developer assets skipped while a fresh CKSyncEngine is rebuilt. They
    /// may be queued only after the bridge has re-proved the corresponding
    /// domain opt-in from the local snapshot.
    private var deferredDeveloperAssetRehydrationIDs: Set<UUID> = []
    private var developerAssetAuthorizationReady = false
    private var transportRehydrationRequired = false
    private var engineGeneration: UInt64 = 1
    private var accountVerificationGeneration: UInt64 = 0
    private var accountVerificationTask: (
        verificationGeneration: UInt64,
        engineGeneration: UInt64,
        engine: CKSyncEngine,
        task: Task<Void, Error>
    )?

    public init(
        configuration: CloudKitSyncConfiguration,
        recordStore: any LocalSyncRecordStore,
        stateStore: any SyncEngineStateStore,
        quarantineStore: any SyncQuarantineStore = InMemorySyncQuarantineStore(),
        systemFieldsStore: any CloudKitSystemFieldsStore =
            InMemoryCloudKitSystemFieldsStore(),
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
        self.systemFieldsStore = systemFieldsStore
        let safety = try stateStore.loadSafetyState()
        self.accountTransitionPending = safety.accountTransitionPending
        self.zoneRecoveryPending = safety.zoneRecoveryPending
        self.lastKnownAccountIdentifier = safety.lastKnownAccountIdentifier
        let container = CKContainer(identifier: configuration.containerIdentifier)
        self.container = container
        self.database = container.privateCloudDatabase
        self.zoneID = CKRecordZone.ID(
            zoneName: configuration.zoneName,
            ownerName: CKCurrentUserDefaultName
        )
        let initialState = try stateStore.load()
        self.transportRehydrationRequired = initialState == nil
        super.init()

        var engineConfiguration = CKSyncEngine.Configuration(
            database: database,
            stateSerialization: initialState,
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
                zoneRecoveryPending: zoneRecoveryPending,
                lastKnownAccountIdentifier: lastKnownAccountIdentifier
            )
        }
    }

    public func engineDescription() -> String? {
        statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress else { return nil }
            return engine?.description
        }
    }

    /// Installs a coalescing host callback for automatic CloudKit fetch/send
    /// events. The provider never performs the plaintext/domain merge itself;
    /// it only asks the owning model to run the same bounded bridge used by
    /// foreground and manual sync.
    public func setEventDrivenSyncHandler(
        _ handler: (@Sendable () -> Void)?
    ) {
        statusLock.withLock {
            eventDrivenSyncHandler = isInvalidated ? nil : handler
        }
    }

    /// Extends provider cancellation over repository/domain import work. The
    /// bridge brackets every import with this lease so a replacement runtime
    /// cannot observe or mutate the shared repository until the old merge has
    /// fully unwound.
    public func beginDomainMergeActivity() throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
    }

    public func endDomainMergeActivity() {
        endActivity()
    }

    /// Schedules the custom zone through CKSyncEngine and then sends pending
    /// local changes. No network call is made until this method is invoked or
    /// automatic sync is enabled by the caller.
    public func prepare() async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let engine = try activeEngine()
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
            // A serialized CKSyncEngine already owns its pending queue. Only a
            // brand-new or deliberately rebuilt engine needs the durable
            // transport snapshot rehydrated.
            try await rehydrateTransportIfRequired(using: engine)
            setStatus(.init(phase: .idle, detail: SyncText("sync.status.ready", "Ready")))
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            throw error
        }
    }

    /// Fetches encrypted envelopes without sending local changes first. The
    /// bridge can therefore run the authenticated field merge before the first
    /// upload, rather than reporting success with a newly queued merge.
    @discardableResult
    public func fetchChanges() async throws -> UInt64 {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        if statusLock.withLock({ statePersistenceBlocked }) {
            try await rebuildEngineForFullRefetch()
        }
        let hasPersistentQuarantine = !(await quarantineStore.allQuarantined()).isEmpty
        let passID = try beginBoundedSyncPass(
            persistentlyBlocked: hasPersistentQuarantine
        )
        let engine = try activeEngine()
        do {
            try await engine.fetchChanges(
                .init(scope: .zoneIDs([zoneID]))
            )
            // Do not hand fetched envelopes to the domain bridge unless the
            // account identity and engine are still the ones that opened this
            // pass. An iCloud switch during the network await is fail-closed.
            try await ensureAccountContinuity()
            guard (try activeEngine()) === engine,
                  statusLock.withLock({
                      accountContinuityVerified && !accountTransitionPending
                  }) else {
                throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
            }
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            finishBoundedSyncPass(passID)
            throw error
        }
        return passID
    }

    /// Flushes changes that were produced by the authenticated domain merge
    /// after the fetch phase. Keeping this separate prevents a disjoint-field
    /// merge from remaining pending after the public sync operation reports
    /// success.
    public func sendPendingChanges(passID: UInt64) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        // A state rebuild intentionally starts with no pending record queue.
        // Rehydrate it exactly once after fetch/domain merge and immediately
        // after a fail-closed account identity check. Normal sends must flush
        // only changes already made pending by a real local/domain mutation.
        try await ensureAccountContinuity()
        let engine = try activeEngine()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        guard !statusLock.withLock({ zoneRecoveryPending }) else {
            throw CloudKitSyncProviderError.zoneRecoveryRequiresConfirmation
        }
        try await rehydrateTransportIfRequired(using: engine)
        guard (try activeEngine()) === engine else {
            throw CloudKitSyncProviderError.unavailable
        }
        guard let windowPassID = beginOutboundBatchWindow(passID: passID) else {
            if statusLock.withLock({
                boundedSyncPassActive && boundedSyncPassID == passID &&
                    boundedSyncPassOutboundBlocked
            }) {
                return
            }
            throw CloudKitSyncProviderError.boundedSyncPassRequired
        }
        defer { endOutboundBatchWindow(windowPassID) }
        do {
            try await engine.sendChanges(
                .init(scope: .zoneIDs([zoneID]))
            )
        } catch {
            let syncError = classify(error)
            setStatus(syncError.status)
            finishBoundedSyncPass(windowPassID)
            throw error
        }
    }

    /// Ends a bounded sync pass honestly. Server conflicts produced by the
    /// final send may queue another record or fetched envelope; those remain a
    /// visible retry state instead of being mislabeled as fully synced.
    public func finalizeBoundedSync(passID: UInt64) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        let activitySnapshot = try statusLock.withLock {
            guard boundedSyncPassActive, boundedSyncPassID == passID else {
                throw CloudKitSyncProviderError.boundedSyncPassRequired
            }
            return (
                activityEpoch,
                boundedSyncPassID,
                currentStatus.phase,
                boundedSyncPassCompletionBlocked
            )
        }
        let pendingFetched = try await recordStore.fetchedRecords().count
        let engine = try activeEngine()
        let pendingRecords = engine.state.pendingRecordZoneChanges.count
        let persistentQuarantine = !(await quarantineStore.allQuarantined()).isEmpty
        var shouldRequestFollowUp = false
        if persistentQuarantine {
            setStatus(.init(
                phase: .quarantined,
                detail: SyncText(
                    "sync.status.record_quarantined",
                    "At least one CloudKit record was quarantined"
                )
            ))
        } else if pendingRecords > 0 || pendingFetched > 0 {
            setRetryScheduledUnlessBlocked()
            shouldRequestFollowUp = true
        } else {
            let completed = setSyncedIfActivityUnchanged(
                activitySnapshot.0,
                passID: activitySnapshot.1,
                observedPhase: activitySnapshot.2,
                observedBlocked: activitySnapshot.3
            )
            if !completed {
                setRetryScheduledUnlessBlocked()
                shouldRequestFollowUp = true
            }
        }
        finishBoundedSyncPass(activitySnapshot.1)
        if shouldRequestFollowUp, status().phase == .retryScheduled {
            requestEventDrivenSyncIfUnbounded()
        }
    }

    func enqueue(
        _ record: SyncRecord,
        authorization: SyncAuthorizationContext = .init()
    ) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try boundary.authorize(record, context: authorization)
        try await recordStore.upsert(record)
        if record.dataClass == .developerAsset {
            statusLock.withLock {
                developerAssetAuthorizationMutationEpoch &+= 1
                authorizedDeveloperAssetIDs.insert(record.entityID)
                developerAssetAuthorizationLastMutation[record.entityID] =
                    developerAssetAuthorizationMutationEpoch
            }
        }
        let engine = try activeEngine()
        let cloudID = CKRecord.ID(
            recordName: record.recordID.uuidString.lowercased(),
            zoneID: zoneID
        )
        engine.state.add(pendingRecordZoneChanges: [.saveRecord(cloudID)])
        markTransportActivity()
        requestEventDrivenSyncIfUnbounded()
    }

    func currentDeveloperAssetAuthorizationMutationEpoch() -> UInt64 {
        statusLock.withLock { developerAssetAuthorizationMutationEpoch }
    }

    /// Seeds only missing or newer domain-authority records in one durable
    /// record-store transaction. `authorizedDeveloperAssetIDs` includes reused
    /// envelopes too, allowing the outbound boundary to be reconstructed at
    /// launch without resealing or uploading unchanged developer assets.
    func enqueueLocalSnapshot(
        _ records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        scanStartedAtMutationEpoch: UInt64
    ) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        let effectiveAuthorizedIDs = statusLock.withLock {
            Set(authorizedDeveloperAssetIDs.filter { recordID in
                guard let lastMutation = developerAssetAuthorizationLastMutation[recordID],
                      lastMutation > scanStartedAtMutationEpoch else {
                    return true
                }
                return self.authorizedDeveloperAssetIDs.contains(recordID)
            })
        }
        let effectiveRecords = records.filter { record in
            record.dataClass != .developerAsset || record.tombstone != nil ||
                effectiveAuthorizedIDs.contains(record.entityID)
        }
        let authorization = SyncAuthorizationContext(
            optedInDeveloperAssetIDs: effectiveAuthorizedIDs
        )
        for record in effectiveRecords {
            try boundary.authorize(record, context: authorization)
        }
        let engine = try activeEngine()
        let deferredAuthorizedIDs = statusLock.withLock {
            deferredDeveloperAssetRehydrationIDs
                .intersection(effectiveAuthorizedIDs)
        }
        for recordID in deferredAuthorizedIDs {
            guard let record = try await recordStore.record(for: recordID),
                  record.recordID == recordID,
                  record.entityID == recordID,
                  record.dataClass == .developerAsset else {
                throw CloudKitSyncProviderError.transportRehydrationRecordMissing
            }
            try boundary.authorize(record, context: authorization)
            _ = try codec.encode(record, zoneID: zoneID)
        }
        try await recordStore.upsert(effectiveRecords)
        statusLock.withLock {
            let knownBeforeScan = self.authorizedDeveloperAssetIDs.filter { recordID in
                (developerAssetAuthorizationLastMutation[recordID] ?? 0) <=
                    scanStartedAtMutationEpoch
            }
            self.authorizedDeveloperAssetIDs.subtract(knownBeforeScan)
            for recordID in effectiveAuthorizedIDs
                where (developerAssetAuthorizationLastMutation[recordID] ?? 0) <=
                    scanStartedAtMutationEpoch {
                self.authorizedDeveloperAssetIDs.insert(recordID)
            }
            developerAssetAuthorizationReady = true
            deferredDeveloperAssetRehydrationIDs.removeAll(keepingCapacity: false)
        }
        let pendingRecordIDs = Set(effectiveRecords.map(\.recordID))
            .union(deferredAuthorizedIDs)
        let changes = pendingRecordIDs.map { recordID in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(
                CKRecord.ID(
                    recordName: recordID.uuidString.lowercased(),
                    zoneID: zoneID
                )
            )
        }
        engine.state.add(pendingRecordZoneChanges: changes)
        guard !changes.isEmpty else { return }
        markTransportActivity()
        requestEventDrivenSyncIfUnbounded()
    }

    /// Commits local winners produced by one authenticated domain-import page in
    /// a single transport-store write. Developer-asset IDs are runtime-authorized
    /// only after the domain snapshot has already committed; this method never
    /// marks the complete launch-time authorization scan as finished.
    func commitImportedDomainResults(
        records: [SyncRecord],
        authorizedDeveloperAssetIDs: Set<UUID>,
        revokedDeveloperAssetIDs: Set<UUID> = []
    ) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        let authorization = SyncAuthorizationContext(
            optedInDeveloperAssetIDs: authorizedDeveloperAssetIDs
        )
        for record in records {
            try boundary.authorize(record, context: authorization)
        }
        let syncEngine = try activeEngine()
        try await recordStore.upsert(records)
        statusLock.withLock {
            if !authorizedDeveloperAssetIDs.isEmpty || !revokedDeveloperAssetIDs.isEmpty {
                developerAssetAuthorizationMutationEpoch &+= 1
                let epoch = developerAssetAuthorizationMutationEpoch
                for recordID in authorizedDeveloperAssetIDs.union(revokedDeveloperAssetIDs) {
                    developerAssetAuthorizationLastMutation[recordID] = epoch
                }
            }
            self.authorizedDeveloperAssetIDs.formUnion(authorizedDeveloperAssetIDs)
            self.authorizedDeveloperAssetIDs.subtract(revokedDeveloperAssetIDs)
        }
        let revokedChanges = revokedDeveloperAssetIDs.map { recordID in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(CKRecord.ID(
                recordName: recordID.uuidString.lowercased(),
                zoneID: zoneID
            ))
        }
        syncEngine.state.remove(pendingRecordZoneChanges: revokedChanges)
        let changes = records.map { record in
            CKSyncEngine.PendingRecordZoneChange.saveRecord(CKRecord.ID(
                recordName: record.recordID.uuidString.lowercased(),
                zoneID: zoneID
            ))
        }
        syncEngine.state.add(pendingRecordZoneChanges: changes)
        guard !changes.isEmpty else { return }
        markTransportActivity()
        requestEventDrivenSyncIfUnbounded()
    }

    public func allRecords() async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        return try await recordStore.allRecords()
    }

    /// Returns opaque CloudKit envelopes that still need to cross the
    /// authenticated plaintext/domain merge boundary. These are deliberately
    /// separate from the single transport snapshot used by CKSyncEngine.
    public func pendingFetchedRecords() async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        return try await recordStore.fetchedRecords()
    }

    /// Returns only durable transport snapshots that are actual quarantine
    /// recovery candidates. Normal sync passes must never replay the complete
    /// transport cache into the domain repository: every domain upsert persists
    /// a full local snapshot and rebuilding all records on every pass therefore
    /// becomes quadratic as history grows.
    public func pendingQuarantineRecoveryRecords() async throws -> [SyncRecord] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        var candidates = statusLock.withLock { quarantineResolutionCandidates }
        let quarantined = await quarantineStore.allQuarantined()
        let fetched = try await recordStore.fetchedRecords()
        let fetchedByID = Dictionary(grouping: fetched, by: \.recordID)
        for (recordID, reason) in quarantined
            where reason != "physical_delete_without_validated_tombstone" &&
                candidates[recordID] == nil {
            guard let entry = await quarantineStore.entry(for: recordID) else { continue }
            let durableCandidate: SyncRecord?
            if let fetchedCandidate = fetchedByID[recordID]?.last {
                durableCandidate = fetchedCandidate
            } else {
                durableCandidate = try await recordStore.record(for: recordID)
            }
            guard let durableCandidate else { continue }
            let candidate = QuarantineResolutionCandidate(
                record: durableCandidate,
                generation: entry.generation
            )
            candidates[recordID] = candidate
            statusLock.withLock {
                quarantineResolutionCandidates[recordID] = candidate
            }
        }
        var recoverable: [SyncRecord] = []
        for (recordID, candidate) in candidates {
            guard let entry = await quarantineStore.entry(for: recordID),
                  entry.reason != "physical_delete_without_validated_tombstone",
                  entry.generation == candidate.generation,
                  try await recordStore.record(for: recordID) == candidate.record else {
                continue
            }
            recoverable.append(candidate.record)
        }
        return recoverable
    }

    /// Removes exactly the envelope that completed domain import. A distinct
    /// version (including distinct encrypted bytes) remains pending.
    public func acknowledgeFetchedRecord(_ record: SyncRecord) async throws {
        try await acknowledgeFetchedRecords([record])
    }

    public func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        try await recordStore.acknowledgeFetchedRecords(records)
    }

    public func quarantineImportedRecord(_ record: SyncRecord, reason: String) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        let entry: SyncQuarantineEntry
        do {
            entry = try await quarantineStore.quarantine(
                recordID: record.recordID,
                reason: reason
            )
        } catch {
            markStatePersistenceFailure()
            throw error
        }
        statusLock.withLock {
            if entry.reason == "physical_delete_without_validated_tombstone" {
                quarantineResolutionCandidates.removeValue(forKey: record.recordID)
            } else {
                quarantineResolutionCandidates[record.recordID] = .init(
                    record: record,
                    generation: entry.generation
                )
            }
        }
        setStatus(.init(
            phase: .quarantined,
            detail: SyncText(
                "sync.status.record_quarantined",
                "At least one CloudKit record was quarantined"
            )
        ))
    }

    /// Rebuilds CKSyncEngine without its opaque change token, forcing a full
    /// zone refetch. Quarantine evidence is retained until that exact record is
    /// durably staged, authenticated, merged, and acknowledged successfully.
    public func retryQuarantinedRecords() async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await rebuildEngineForFullRefetch()
        setStatus(.init(
            phase: .preparing,
            detail: SyncText(
                "sync.status.quarantine_retry",
                "CloudKit is refetching quarantined records"
            )
        ))
        requestEventDrivenSyncIfUnbounded()
    }

    public func hasPhysicalDeletionQuarantine() async -> Bool {
        (await quarantineStore.allQuarantined()).values.contains(
            "physical_delete_without_validated_tombstone"
        )
    }

    /// Exposes only exact, generation-bound local envelopes for bridge-level
    /// decrypt and domain validation. The provider cannot declare its own opaque
    /// transport payload safe merely because the CKRecord metadata is valid.
    func physicalDeletionRecoveryCandidates() async throws -> [(
        record: SyncRecord,
        generation: UUID
    )] {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        let quarantined = await quarantineStore.allQuarantined()
        var candidates: [(record: SyncRecord, generation: UUID)] = []
        for (recordID, reason) in quarantined
            where reason == "physical_delete_without_validated_tombstone" {
            guard let entry = await quarantineStore.entry(for: recordID),
                  entry.reason == "physical_delete_without_validated_tombstone",
                  let local = try await recordStore.record(for: recordID) else {
                continue
            }
            candidates.append((record: local, generation: entry.generation))
        }
        return candidates
    }

    /// Queues one envelope only after CompanionSyncBridge has opened and fully
    /// validated this exact generation. A concurrent store/quarantine change
    /// makes the operation a no-op rather than restoring stale bytes.
    @discardableResult
    func restorePhysicallyDeletedRecord(
        _ record: SyncRecord,
        expectedGeneration: UUID
    ) async throws -> Bool {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        let canRecover = statusLock.withLock {
            !boundedSyncPassActive && !accountTransitionPending && !zoneRecoveryPending
        }
        guard canRecover else {
            throw CloudKitSyncProviderError.boundedSyncPassAlreadyActive
        }
        guard let entry = await quarantineStore.entry(for: record.recordID),
              entry.reason == "physical_delete_without_validated_tombstone",
              entry.generation == expectedGeneration,
              try await recordStore.record(for: record.recordID) == record else {
            return false
        }
        try authorizeOutboundRecord(record)
        _ = try codec.encode(record, zoneID: zoneID)
        let validatedEngine = try activeEngine()
        let cloudID = CKRecord.ID(
            recordName: record.recordID.uuidString.lowercased(),
            zoneID: zoneID
        )
        let removed = try await quarantineStore.remove(
            recordID: record.recordID,
            expectedGeneration: expectedGeneration
        )
        guard removed else { return false }
        do {
            let currentEngine = try activeEngine()
            guard currentEngine === validatedEngine else {
                throw CloudKitSyncProviderError.unavailable
            }
            // Keep automatic sync from observing a pending change while the
            // exact durable quarantine generation still blocks its payload.
            currentEngine.state.add(pendingRecordZoneChanges: [.saveRecord(cloudID)])
        } catch {
            _ = try? await quarantineStore.quarantine(
                recordID: record.recordID,
                reason: "physical_delete_without_validated_tombstone"
            )
            throw error
        }
        _ = statusLock.withLock {
            quarantineResolutionCandidates.removeValue(forKey: record.recordID)
        }
        markTransportActivity()
        let hasOtherQuarantine = !(await quarantineStore.allQuarantined()).isEmpty
        setStatus(.init(
            phase: hasOtherQuarantine ? .quarantined : .retryScheduled,
            detail: SyncText(
                "sync.status.physical_delete_restore_queued",
                "Retained local records are ready to be restored to CloudKit"
            )
        ))
        requestEventDrivenSyncIfUnbounded()
        return true
    }

    /// Clears an exact physical-delete generation after the domain bridge has
    /// proved that there is no current local value to restore. This grants no
    /// outbound authorization and queues no save.
    @discardableResult
    func acceptPhysicalDeletion(
        recordID: UUID,
        expectedGeneration: UUID
    ) async throws -> Bool {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        guard let entry = await quarantineStore.entry(for: recordID),
              entry.reason == "physical_delete_without_validated_tombstone",
              entry.generation == expectedGeneration else {
            return false
        }
        let removed = try await quarantineStore.remove(
            recordID: recordID,
            expectedGeneration: expectedGeneration
        )
        guard removed else { return false }
        statusLock.withLock {
            quarantineResolutionCandidates.removeValue(forKey: recordID)
            authorizedDeveloperAssetIDs.remove(recordID)
        }
        return true
    }

    public func resolveQuarantinedRecord(_ record: SyncRecord) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        guard let entry = await quarantineStore.entry(for: record.recordID) else { return }
        // A physical CloudKit deletion has no authenticated tombstone and can
        // never be cleared by an older inbox envelope of the same UUID.
        guard entry.reason != "physical_delete_without_validated_tombstone" else { return }
        let runtimeCandidateMatches = statusLock.withLock({
            guard let candidate = quarantineResolutionCandidates[record.recordID] else {
                return false
            }
            return candidate.record == record && candidate.generation == entry.generation
        })
        let durableInboxMatches: Bool
        if runtimeCandidateMatches {
            durableInboxMatches = true
        } else {
            durableInboxMatches = try await recordStore.fetchedRecords().contains(record)
        }
        guard durableInboxMatches else { return }
        let removed = try await quarantineStore.remove(
            recordID: record.recordID,
            expectedGeneration: entry.generation
        )
        guard removed else { return }
        _ = statusLock.withLock {
            quarantineResolutionCandidates.removeValue(forKey: record.recordID)
        }
        let quarantineIsEmpty = (await quarantineStore.allQuarantined()).isEmpty
        if quarantineIsEmpty {
            statusLock.withLock {
                if boundedSyncPassActive {
                    boundedSyncPassCompletionBlocked = false
                }
                if currentStatus.phase == .quarantined {
                    currentStatus = .init(
                        phase: boundedSyncPassActive ? .syncing : .idle,
                        detail: SyncText(
                            "sync.status.quarantine_resolved",
                            "Quarantined record was validated"
                        )
                    )
                }
                activityEpoch &+= 1
            }
        }
    }

    /// Aborts a bridge-owned pass after a domain import failure. Pending
    /// envelopes and record changes remain durable for the next retry.
    public func abortBoundedSyncPass(_ passID: UInt64) {
        let ownedPassID = statusLock.withLock { () -> UInt64? in
            guard boundedSyncPassActive, boundedSyncPassID == passID else { return nil }
            if !Self.blocksSuccessfulCompletion(currentStatus.phase) {
                boundedSyncPassCompletionBlocked = true
                boundedSyncPassOutboundBlocked = true
                currentStatus = .init(
                    phase: .failed,
                    detail: SyncText(
                        "sync.status.domain_merge_failed",
                        "The local sync merge did not complete"
                    )
                )
                activityEpoch &+= 1
            }
            return boundedSyncPassID
        }
        if let ownedPassID { finishBoundedSyncPass(ownedPassID) }
    }

    /// Account switches are a privacy boundary: records from the previous
    /// account remain local but are not uploaded to the new account until the
    /// product obtains explicit confirmation.
    public func confirmAccountTransition(allowLocalUpload: Bool) async throws {
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        _ = try activeEngine()
        let currentAccountIdentifier = try await container.userRecordID().recordName
        try updateSafetyState(
            accountTransitionPending: !allowLocalUpload,
            lastKnownAccountIdentifier: currentAccountIdentifier
        )
        guard allowLocalUpload else {
            setStatus(.init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_upload_not_approved",
                    "Local data remains isolated until upload is approved"
                )
            ))
            return
        }
        do {
            // A confirmed account switch must not reuse the previous account's
            // serialized engine tokens, pending-change history, or server
            // change tags.
            do {
                try await systemFieldsStore.clear()
            } catch {
                markStatePersistenceFailure()
                throw error
            }
            try await rebuildEngineForFullRefetch()
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
        guard beginActivity() else { throw CloudKitSyncProviderError.unavailable }
        defer { endActivity() }
        try await ensureAccountContinuity()
        guard !statusLock.withLock({ accountTransitionPending }) else {
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
        let engine = try activeEngine()
        do {
            try await systemFieldsStore.clear()
        } catch {
            markStatePersistenceFailure()
            throw error
        }
        engine.state.add(pendingDatabaseChanges: [.saveZone(CKRecordZone(zoneID: zoneID))])
        try await engine.sendChanges()
        statusLock.withLock { transportRehydrationRequired = true }
        try await rehydrateTransportIfRequired(using: engine)
        _ = try activeEngine()
        try updateSafetyState(zoneRecoveryPending: false)
        setStatus(.init(phase: .idle, detail: SyncText(
            "sync.status.zone_restored", "Sync zone restored"
        )))
    }

    public func pendingRecordCount() -> Int {
        statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress, let engine else { return 0 }
            return engine.state.pendingRecordZoneChanges.count
        }
    }

    public func cancel() async {
        let engineToCancel = statusLock.withLock { () -> CKSyncEngine? in
            if isInvalidated { return nil }
            isInvalidated = true
            accountVerificationTask?.task.cancel()
            accountVerificationTask = nil
            accountVerificationGeneration &+= 1
            accountContinuityVerified = false
            eventDrivenSyncHandler = nil
            outboundBatchWindowDepth = 0
            return engine
        }
        await engineToCancel?.cancelOperations()
        await waitForActivityDrain()
        // Existing delegate/record-provider activities may have queued work
        // after the first cancellation. Once the drain completes, no new
        // activity can begin because invalidation is already visible.
        let finalEngine = statusLock.withLock { engine }
        await finalEngine?.cancelOperations()
    }

    public func handleEvent(
        _ event: CKSyncEngine.Event,
        syncEngine: CKSyncEngine
    ) async {
        guard beginActivity(for: syncEngine) else { return }
        defer { endActivity() }
        markTransportActivity()
        switch event {
        case let .stateUpdate(update):
            let canPersistState = statusLock.withLock {
                accountContinuityVerified && !accountTransitionPending &&
                    !statePersistenceBlocked && engine === syncEngine
            }
            guard canPersistState else {
                if !statusLock.withLock({ statePersistenceBlocked }) {
                    markStatePersistenceFailure()
                }
                return
            }
            do {
                try stateStore.save(update.stateSerialization)
            } catch {
                markStatePersistenceFailure()
            }
        case .accountChange:
            // CKSyncEngine resets its pending state on account changes. Keep
            // local records, clear the persisted engine token, and require a
            // new account-aware setup instead of deleting user data.
            statusLock.withLock {
                accountVerificationTask?.task.cancel()
                accountVerificationTask = nil
                accountVerificationGeneration &+= 1
                accountContinuityVerified = false
            }
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
            guard statusLock.withLock({
                accountContinuityVerified && !accountTransitionPending &&
                    !statePersistenceBlocked && engine === syncEngine
            }) else {
                markStatePersistenceFailure()
                return
            }
            await applyFetched(changes)
            requestEventDrivenSyncIfUnbounded()
        case let .sentRecordZoneChanges(changes):
            guard statusLock.withLock({
                accountContinuityVerified && !accountTransitionPending &&
                    !statePersistenceBlocked && engine === syncEngine
            }) else {
                markStatePersistenceFailure()
                return
            }
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
        guard beginActivity(for: syncEngine) else { return nil }
        defer { endActivity() }
        do {
            try await ensureAccountContinuity()
        } catch {
            return nil
        }
        let pending = syncEngine.state.pendingRecordZoneChanges.filter {
            context.options.scope.contains($0)
        }
        let quarantinedIDs = Set((await quarantineStore.allQuarantined()).keys)
        let unsupportedDeletes = pending.filter {
            if case .deleteRecord = $0 { return true }
            return false
        }
        var saves: [CKSyncEngine.PendingRecordZoneChange] = []
        var permanentlyRemoved: [CKSyncEngine.PendingRecordZoneChange] = []
        for change in pending {
            guard case let .saveRecord(cloudID) = change else { continue }
            guard let recordID = UUID(uuidString: cloudID.recordName) else {
                guard await persistQuarantine(
                    recordID: Self.quarantineIdentifier(for: cloudID.recordName),
                    reason: "invalid_pending_record_identifier"
                ) else { return nil }
                permanentlyRemoved.append(change)
                continue
            }
            if quarantinedIDs.contains(recordID) {
                permanentlyRemoved.append(change)
                continue
            }
            let local: SyncRecord?
            do {
                local = try await recordStore.record(for: recordID)
            } catch {
                markStatePersistenceFailure()
                return nil
            }
            guard let local else {
                guard await persistQuarantine(
                    recordID: recordID,
                    reason: "missing_local_transport_record"
                ) else { return nil }
                permanentlyRemoved.append(change)
                continue
            }
            do {
                try authorizeOutboundRecord(local)
                _ = try codec.encode(local, zoneID: zoneID)
            } catch {
                if developerAssetAuthorizationIsPending(for: local, error: error) {
                    continue
                }
                guard await persistQuarantine(
                    recordID: recordID,
                    reason: "outbound_boundary_validation_failed"
                ) else { return nil }
                permanentlyRemoved.append(change)
                continue
            }
            saves.append(change)
        }
        if !permanentlyRemoved.isEmpty {
            syncEngine.state.remove(pendingRecordZoneChanges: permanentlyRemoved)
        }
        let gate = statusLock.withLock { () -> (passID: UInt64?, requestHostPass: Bool) in
            let safe = accountContinuityVerified &&
                !accountTransitionPending && !zoneRecoveryPending &&
                !zonePreparationInProgress
            let passID = safe && boundedSyncPassActive &&
                !boundedSyncPassOutboundBlocked && outboundBatchWindowDepth > 0
                ? boundedSyncPassID
                : nil
            return (
                passID,
                safe && !boundedSyncPassActive &&
                    (!unsupportedDeletes.isEmpty || !saves.isEmpty)
            )
        }
        let passID = gate.passID
        guard let passID else {
            if gate.requestHostPass {
                requestEventDrivenSyncIfUnbounded()
            }
            return nil
        }
        guard !unsupportedDeletes.isEmpty || !saves.isEmpty else { return nil }

        // A previous engine state must never smuggle a raw delete back into a
        // provider whose public contract is tombstone-only.
        if !unsupportedDeletes.isEmpty {
            var durableDeletes: [CKSyncEngine.PendingRecordZoneChange] = []
            for change in unsupportedDeletes {
                guard case let .deleteRecord(cloudID) = change,
                      await persistQuarantine(
                        recordID: Self.quarantineIdentifier(for: cloudID.recordName),
                        reason: "raw_pending_delete_discarded"
                      ) else { return nil }
                durableDeletes.append(change)
            }
            syncEngine.state.remove(pendingRecordZoneChanges: durableDeletes)
            setStatus(.init(
                phase: .failed,
                detail: SyncText(
                    "sync.status.raw_delete_discarded",
                    "Raw CloudKit record deletion was discarded"
                )
            ))
            return nil
        }

        guard !saves.isEmpty else { return nil }
        return await .init(pendingChanges: saves) { [weak self, recordStore, codec, zoneID] cloudID in
            guard let self, self.beginActivity(for: syncEngine) else { return nil }
            defer { self.endActivity() }
            guard self.canProvideRecordBatch(for: passID, syncEngine: syncEngine) else {
                return nil
            }
            guard let rawID = UUID(uuidString: cloudID.recordName),
                  !(await self.quarantineStore.allQuarantined()).keys.contains(rawID),
                  let local = try? await recordStore.record(for: rawID),
                  self.canProvideRecordBatch(for: passID, syncEngine: syncEngine),
                  (try? self.authorizeOutboundRecord(local)) != nil else {
                return nil
            }
            do {
                let baseRecord = try await self.baseRecord(
                    for: rawID,
                    expectedCloudID: cloudID
                )
                return try codec.encode(
                    local,
                    zoneID: zoneID,
                    baseRecord: baseRecord
                )
            } catch {
                self.markStatePersistenceFailure()
                return nil
            }
        }
    }

    public func nextFetchChangesOptions(
        _ context: CKSyncEngine.FetchChangesContext,
        syncEngine: CKSyncEngine
    ) async -> CKSyncEngine.FetchChangesOptions {
        _ = context
        guard beginActivity(for: syncEngine) else {
            return .init(scope: .zoneIDs([]))
        }
        defer { endActivity() }
        do {
            try await ensureAccountContinuity()
        } catch {
            return .init(scope: .zoneIDs([]))
        }
        guard statusLock.withLock({
            accountContinuityVerified && !accountTransitionPending &&
                !zoneRecoveryPending && engine === syncEngine
        }) else { return .init(scope: .zoneIDs([])) }
        return .init(scope: .zoneIDs([zoneID]))
    }

    private func applyFetched(
        _ changes: CKSyncEngine.Event.FetchedRecordZoneChanges
    ) async {
        var resolvedConflict = false
        var validatedModifications: [(record: SyncRecord, serverRecord: CKRecord)] = []
        for modification in changes.modifications {
            let recordID = Self.quarantineIdentifier(
                for: modification.record.recordID.recordName
            )
            let incoming: SyncRecord
            do {
                incoming = try codec.decode(modification.record)
                try authorizeInboundTransportEnvelope(incoming)
            } catch {
                await persistQuarantine(
                    recordID: recordID,
                    reason: "invalid_or_denied_cloudkit_record"
                )
                continue
            }
            validatedModifications.append((incoming, modification.record))
        }
        if !validatedModifications.isEmpty {
            do {
                let retentions = try await Self.retainFetchedEnvelopes(
                    validatedModifications.map(\.record),
                    in: recordStore
                )
                try await persistSystemFields(
                    for: validatedModifications.map(\.serverRecord)
                )
                for retained in retentions {
                    await markResolutionCandidateIfQuarantined(retained.record)
                    resolvedConflict = resolvedConflict || retained.existing != nil &&
                        retained.snapshot != retained.existing
                }
            } catch {
                // Boundary-valid data hit a local persistence failure. Never
                // quarantine it as corrupt; keep the inbox and block the token.
                markStatePersistenceFailure()
            }
        }
        var physicallyDeletedRecordIDs = Set<UUID>()
        for deletion in changes.deletions {
            let recordID = Self.quarantineIdentifier(for: deletion.recordID.recordName)
            if await persistQuarantine(
                recordID: recordID,
                reason: "physical_delete_without_validated_tombstone"
            ), (await quarantineStore.entry(for: recordID))?.reason ==
                "physical_delete_without_validated_tombstone" {
                physicallyDeletedRecordIDs.insert(recordID)
            }
        }
        if !physicallyDeletedRecordIDs.isEmpty {
            do {
                // A physically deleted server row has no valid change tag. A
                // user-approved restore must therefore create it anew.
                try await systemFieldsStore.remove(
                    recordIDs: physicallyDeletedRecordIDs
                )
            } catch {
                markStatePersistenceFailure()
            }
        }
        if resolvedConflict && statusLock.withLock({
            !Self.blocksSuccessfulCompletion(currentStatus.phase)
        }) {
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
        if !changes.savedRecords.isEmpty {
            do {
                try await persistSystemFields(for: changes.savedRecords)
            } catch {
                markStatePersistenceFailure()
            }
        }
        guard !changes.failedRecordSaves.isEmpty || !changes.failedRecordDeletes.isEmpty else {
            return
        }

        for failure in changes.failedRecordSaves {
            if failure.error.code == .serverRecordChanged,
               let serverRecord = failure.error.serverRecord {
                if await reconcileServerRecord(serverRecord, syncEngine: syncEngine) {
                    setStatus(.init(
                        phase: .conflictResolved,
                        detail: SyncText(
                            "sync.status.conflict_merged",
                            "Conflict was merged deterministically"
                        )
                    ))
                }
                // A successful reconciliation has already retained the server
                // envelope, persisted its change tag, and queued the merged
                // retry. Do not overwrite that state with a generic failure.
                continue
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
    ) async -> Bool {
        let recordID = Self.quarantineIdentifier(for: serverRecord.recordID.recordName)
        let incoming: SyncRecord
        do {
            incoming = try codec.decode(serverRecord)
            try authorizeInboundTransportEnvelope(incoming)
        } catch {
            await persistQuarantine(
                recordID: recordID,
                reason: "cloudkit_conflict_record_validation_failed"
            )
            return false
        }
        do {
            _ = try await Self.retainFetchedEnvelope(incoming, in: recordStore)
            try await persistSystemFields(for: [serverRecord])
            await markResolutionCandidateIfQuarantined(incoming)
            syncEngine.state.add(pendingRecordZoneChanges: [
                .saveRecord(serverRecord.recordID)
            ])
            markTransportActivity()
            return true
        } catch {
            markStatePersistenceFailure()
            return false
        }
    }

    private func baseRecord(
        for recordID: UUID,
        expectedCloudID: CKRecord.ID
    ) async throws -> CKRecord? {
        guard let data = try await systemFieldsStore.data(for: recordID) else {
            return nil
        }
        do {
            let record = try CloudKitSystemFieldsCodec.decode(data)
            guard record.recordType == AppleCloudKitRecordCodec.recordType,
                  record.recordID == expectedCloudID else {
                throw CloudKitSystemFieldsStoreError.invalidArchive
            }
            return record
        } catch {
            // A corrupt or stale sidecar is recoverable: remove it and let the
            // server-record-changed path fetch a fresh change tag. Failure to
            // remove it remains fail-closed to avoid an endless retry loop.
            try await systemFieldsStore.remove(recordIDs: [recordID])
            return nil
        }
    }

    private func persistSystemFields(for records: [CKRecord]) async throws {
        var values: [UUID: Data] = [:]
        values.reserveCapacity(records.count)
        for record in records {
            guard record.recordType == AppleCloudKitRecordCodec.recordType,
                  record.recordID.zoneID == zoneID,
                  let recordID = UUID(uuidString: record.recordID.recordName) else {
                throw CloudKitSystemFieldsStoreError.invalidArchive
            }
            values[recordID] = CloudKitSystemFieldsCodec.encode(record)
        }
        try await systemFieldsStore.upsert(values)
    }

    @discardableResult
    private func persistQuarantine(recordID: UUID, reason: String) async -> Bool {
        do {
            _ = try await quarantineStore.quarantine(recordID: recordID, reason: reason)
            _ = statusLock.withLock {
                quarantineResolutionCandidates.removeValue(forKey: recordID)
            }
            setStatus(.init(
                phase: .quarantined,
                detail: SyncText(
                    "sync.status.record_quarantined",
                    "At least one CloudKit record was quarantined"
                )
            ))
            return true
        } catch {
            // Without durable quarantine metadata, persisting the advanced
            // CKSyncEngine token would permanently hide the rejected record.
            markStatePersistenceFailure()
            return false
        }
    }

    private func markResolutionCandidateIfQuarantined(_ record: SyncRecord) async {
        guard let entry = await quarantineStore.entry(for: record.recordID),
              entry.reason != "physical_delete_without_validated_tombstone" else {
            return
        }
        statusLock.withLock {
            quarantineResolutionCandidates[record.recordID] = .init(
                record: record,
                generation: entry.generation
            )
        }
    }

    private static func quarantineIdentifier(for recordName: String) -> UUID {
        if let identifier = UUID(uuidString: recordName) { return identifier }
#if canImport(CryptoKit)
        var bytes = Array(SHA256.hash(data: Data(recordName.utf8)).prefix(16))
        bytes[6] = (bytes[6] & 0x0f) | 0x50
        bytes[8] = (bytes[8] & 0x3f) | 0x80
        return UUID(uuid: (
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5], bytes[6], bytes[7],
            bytes[8], bytes[9], bytes[10], bytes[11],
            bytes[12], bytes[13], bytes[14], bytes[15]
        ))
#else
        return UUID()
#endif
    }

    struct FetchedEnvelopeRetention: Sendable {
        let record: SyncRecord
        let existing: SyncRecord?
        let snapshot: SyncRecord
    }

    /// Durably stages every distinct inbound encrypted envelope in one inbox
    /// write before choosing and persisting the final transport snapshots in one
    /// primary-store write. If the second write fails, the lossless inbox stays
    /// durable and the CloudKit token is blocked for a safe retry.
    static func retainFetchedEnvelopes(
        _ incomingRecords: [SyncRecord],
        in recordStore: any LocalSyncRecordStore
    ) async throws -> [FetchedEnvelopeRetention] {
        guard !incomingRecords.isEmpty else { return [] }
        // Preserve every exact inbound envelope first. The subsequent store
        // merge resolves against its then-current primary state atomically, so
        // a concurrent newer local enqueue cannot be overwritten by a stale
        // read-compute-write batch.
        try await recordStore.stageFetchedRecords(incomingRecords)
        return try await recordStore.mergeRecords(
            incomingRecords,
            policy: .transportLastWriterWins
        ).map {
            FetchedEnvelopeRetention(
                record: $0.incoming,
                existing: $0.existing,
                snapshot: $0.snapshot
            )
        }
    }

    /// Compatibility seam for focused callers and tests. Production fetch pages
    /// use the batch form above.
    @discardableResult
    static func retainFetchedEnvelope(
        _ incoming: SyncRecord,
        in recordStore: any LocalSyncRecordStore
    ) async throws -> (existing: SyncRecord?, snapshot: SyncRecord) {
        guard let retained = try await retainFetchedEnvelopes(
            [incoming],
            in: recordStore
        ).first else {
            return (nil, incoming)
        }
        return (retained.existing, retained.snapshot)
    }

    /// Chooses only the opaque record used for future CloudKit upload. LWW is
    /// safe here because retainFetchedEnvelope has already preserved `rhs` for
    /// the authenticated domain/field merge, even when `lhs` wins this choice.
    static func resolveTransportSnapshot(
        _ lhs: SyncRecord?,
        _ rhs: SyncRecord
    ) -> SyncRecord {
        SyncRecordTransportResolver.resolve(
            lhs,
            rhs,
            policy: .transportLastWriterWins
        )
    }

    /// The transport must stage an encrypted developer-asset envelope before
    /// its opt-in bit and secret-safety constraints can be authenticated. This
    /// grants only that single opaque ID passage to CompanionSyncBridge; it is
    /// not an outbound authorization.
    private func authorizeInboundTransportEnvelope(_ record: SyncRecord) throws {
        let context = record.dataClass == .developerAsset
            ? SyncAuthorizationContext(optedInDeveloperAssetIDs: [record.entityID])
            : .init()
        try boundary.authorize(record, context: context)
    }

    private func authorizeOutboundRecord(_ record: SyncRecord) throws {
        let allowedDeveloperAssetIDs = statusLock.withLock {
            authorizedDeveloperAssetIDs
        }
        try boundary.authorize(
            record,
            context: .init(optedInDeveloperAssetIDs: allowedDeveloperAssetIDs)
        )
    }

    private func developerAssetAuthorizationIsPending(
        for record: SyncRecord,
        error: any Error
    ) -> Bool {
        guard record.dataClass == .developerAsset,
              record.tombstone == nil,
              let boundaryError = error as? SyncBoundaryError,
              case .developerAssetNotOptedIn = boundaryError else {
            return false
        }
        return statusLock.withLock { !developerAssetAuthorizationReady }
    }

    private func rehydrateTransportIfRequired(using syncEngine: CKSyncEngine) async throws {
        let shouldRehydrate = statusLock.withLock { () -> Bool in
            guard engine === syncEngine, transportRehydrationRequired else { return false }
            transportRehydrationRequired = false
            return true
        }
        guard shouldRehydrate else { return }
        do {
            try await requeueLocalRecords()
        } catch {
            statusLock.withLock {
                if engine === syncEngine { transportRehydrationRequired = true }
            }
            throw error
        }
    }

    private func requeueLocalRecords() async throws {
        let quarantinedIDs = Set((await quarantineStore.allQuarantined()).keys)
        var changes: [CKSyncEngine.PendingRecordZoneChange] = []
        var deferredDeveloperAssetIDs = Set<UUID>()
        for record in try await recordStore.allRecords()
            where !quarantinedIDs.contains(record.recordID) {
            do {
                try authorizeOutboundRecord(record)
                _ = try codec.encode(record, zoneID: zoneID)
            } catch {
                if developerAssetAuthorizationIsPending(for: record, error: error) {
                    deferredDeveloperAssetIDs.insert(record.recordID)
                    continue
                }
                guard await persistQuarantine(
                    recordID: record.recordID,
                    reason: "outbound_boundary_validation_failed"
                ) else {
                    throw error
                }
                continue
            }
            changes.append(.saveRecord(CKRecord.ID(
                recordName: record.recordID.uuidString.lowercased(),
                zoneID: zoneID
            )))
        }
        let syncEngine = try activeEngine()
        statusLock.withLock {
            deferredDeveloperAssetRehydrationIDs = deferredDeveloperAssetIDs
        }
        syncEngine.state.add(pendingRecordZoneChanges: changes)
        if !changes.isEmpty { markTransportActivity() }
    }

    private struct AccountVerificationTicket {
        let verificationGeneration: UInt64
        let engineGeneration: UInt64
        let engine: CKSyncEngine
        let needsInitialBindingAudit: Bool
    }

    /// Verifies the opaque CloudKit account identity before any fetch or
    /// upload. Every proof is bound to both a monotonic verification ticket and
    /// the exact CKSyncEngine generation. An older async completion can therefore
    /// never reopen a gate that a newer account check or engine replacement has
    /// already closed.
    private func ensureAccountContinuity() async throws {
        let verification = try statusLock.withLock { () -> (
            generation: UInt64,
            engineGeneration: UInt64,
            engine: CKSyncEngine,
            task: Task<Void, Error>
        ) in
            guard !isInvalidated, !engineReplacementInProgress, let engine else {
                throw CloudKitSyncProviderError.unavailable
            }
            if let active = accountVerificationTask,
               active.engineGeneration == engineGeneration,
               active.engine === engine {
                return (
                    active.verificationGeneration,
                    active.engineGeneration,
                    active.engine,
                    active.task
                )
            }
            accountVerificationGeneration &+= 1
            accountContinuityVerified = false
            let ticket = AccountVerificationTicket(
                verificationGeneration: accountVerificationGeneration,
                engineGeneration: engineGeneration,
                engine: engine,
                needsInitialBindingAudit: lastKnownAccountIdentifier == nil
            )
            let task = Task { [weak self] in
                guard let self else {
                    throw CloudKitSyncProviderError.unavailable
                }
                try Task.checkCancellation()
                try await self.performAccountContinuityVerification(ticket)
            }
            accountVerificationTask = (
                verificationGeneration: ticket.verificationGeneration,
                engineGeneration: ticket.engineGeneration,
                engine: ticket.engine,
                task: task
            )
            return (
                ticket.verificationGeneration,
                ticket.engineGeneration,
                ticket.engine,
                task
            )
        }
        do {
            try await verification.task.value
        } catch {
            statusLock.withLock {
                if accountVerificationTask?.verificationGeneration ==
                    verification.generation {
                    accountVerificationTask = nil
                }
            }
            throw error
        }
        try statusLock.withLock {
            guard !isInvalidated,
                  !engineReplacementInProgress,
                  accountVerificationGeneration == verification.generation,
                  engineGeneration == verification.engineGeneration,
                  engine === verification.engine,
                  accountContinuityVerified else {
                throw CloudKitSyncProviderError.unavailable
            }
            if accountVerificationTask?.verificationGeneration == verification.generation {
                accountVerificationTask = nil
            } else if accountVerificationTask != nil {
                // A newer verification has already closed the gate. An older
                // waiter may not return success into prepare/fetch/send.
                throw CloudKitSyncProviderError.unavailable
            }
        }
    }

    private func performAccountContinuityVerification(
        _ ticket: AccountVerificationTicket
    ) async throws {
        let currentIdentifier: String
        do {
            currentIdentifier = try await container.userRecordID().recordName
        } catch {
            let isCurrent = statusLock.withLock {
                accountVerificationGeneration == ticket.verificationGeneration &&
                    engineGeneration == ticket.engineGeneration &&
                    engine === ticket.engine
            }
            if isCurrent { setStatus(classify(error).status) }
            throw error
        }

        let hasUnboundPersistentState: Bool
        if ticket.needsInitialBindingAudit {
            do {
                let hasEngineState = try stateStore.load() != nil
                let hasRecords = !(try await recordStore.allRecords()).isEmpty
                let hasFetchedInbox = !(try await recordStore.fetchedRecords()).isEmpty
                hasUnboundPersistentState = hasEngineState || hasRecords || hasFetchedInbox
            } catch {
                markStatePersistenceFailure()
                throw error
            }
        } else {
            hasUnboundPersistentState = false
        }

        let accountMismatch = try statusLock.withLock { () -> Bool in
            guard !isInvalidated, !engineReplacementInProgress,
                  accountVerificationGeneration == ticket.verificationGeneration,
                  engineGeneration == ticket.engineGeneration,
                  engine === ticket.engine else {
                throw CloudKitSyncProviderError.unavailable
            }
            if let known = lastKnownAccountIdentifier, known != currentIdentifier {
                accountTransitionPending = true
                accountContinuityVerified = false
                do {
                    try stateStore.saveSafetyState(.init(
                        accountTransitionPending: true,
                        zoneRecoveryPending: zoneRecoveryPending,
                        lastKnownAccountIdentifier: known
                    ))
                } catch {
                    // A failed privacy-sidecar write is itself fail-closed.
                    zoneRecoveryPending = true
                    try? stateStore.saveSafetyState(.init(
                        accountTransitionPending: true,
                        zoneRecoveryPending: true,
                        lastKnownAccountIdentifier: known
                    ))
                    throw error
                }
                return true
            }
            if lastKnownAccountIdentifier == nil {
                if hasUnboundPersistentState {
                    accountTransitionPending = true
                    accountContinuityVerified = false
                    do {
                        try stateStore.saveSafetyState(.init(
                            accountTransitionPending: true,
                            zoneRecoveryPending: zoneRecoveryPending,
                            lastKnownAccountIdentifier: nil
                        ))
                    } catch {
                        zoneRecoveryPending = true
                        try? stateStore.saveSafetyState(.init(
                            accountTransitionPending: true,
                            zoneRecoveryPending: true,
                            lastKnownAccountIdentifier: nil
                        ))
                        throw error
                    }
                    return true
                }
                do {
                    try stateStore.saveSafetyState(.init(
                        accountTransitionPending: accountTransitionPending,
                        zoneRecoveryPending: zoneRecoveryPending,
                        lastKnownAccountIdentifier: currentIdentifier
                    ))
                    lastKnownAccountIdentifier = currentIdentifier
                } catch {
                    accountTransitionPending = true
                    zoneRecoveryPending = true
                    try? stateStore.saveSafetyState(.init(
                        accountTransitionPending: true,
                        zoneRecoveryPending: true,
                        lastKnownAccountIdentifier: nil
                    ))
                    throw error
                }
            }
            accountContinuityVerified = !accountTransitionPending
            return accountTransitionPending
        }
        if accountMismatch {
            setStatus(.init(
                phase: .accountRequired,
                detail: SyncText(
                    "sync.status.account_changed",
                    "iCloud account changed; local data was retained"
                )
            ))
            throw CloudKitSyncProviderError.accountTransitionRequiresConfirmation
        }
    }

    private func rebuildEngineForFullRefetch() async throws {
        try await ensureAccountContinuity()
        let previousEngine = try statusLock.withLock { () -> CKSyncEngine in
            guard !isInvalidated,
                  !engineReplacementInProgress,
                  !boundedSyncPassActive,
                  activeActivityCount == 1,
                  let engine else {
                throw CloudKitSyncProviderError.boundedSyncPassAlreadyActive
            }
            engineReplacementInProgress = true
            statePersistenceBlocked = true
            return engine
        }

        var replacementInstalled = false
        defer {
            if !replacementInstalled {
                statusLock.withLock { engineReplacementInProgress = false }
            }
        }

        await previousEngine.cancelOperations()
        do {
            try stateStore.clear()
        } catch {
            markStatePersistenceFailure()
            throw error
        }

        var replacementConfiguration = CKSyncEngine.Configuration(
            database: database,
            stateSerialization: nil,
            delegate: self
        )
        replacementConfiguration.automaticallySync = configuration.automaticallySync
        replacementConfiguration.subscriptionID = configuration.subscriptionID
        let replacementEngine = CKSyncEngine(replacementConfiguration)
        try statusLock.withLock {
            guard !isInvalidated,
                  engineReplacementInProgress,
                  engine === previousEngine,
                  !boundedSyncPassActive,
                  activeActivityCount == 1 else {
                throw CloudKitSyncProviderError.unavailable
            }
            engine = replacementEngine
            accountVerificationTask?.task.cancel()
            accountVerificationTask = nil
            engineGeneration &+= 1
            accountVerificationGeneration &+= 1
            engineReplacementInProgress = false
            statePersistenceBlocked = false
            transportRehydrationRequired = true
            accountContinuityVerified = false
            activityEpoch &+= 1
        }
        replacementInstalled = true
        // Bind a fresh proof to the replacement engine itself. Reusing the old
        // engine's successful account await would reopen the exact race this
        // generation boundary is designed to prevent.
        try await ensureAccountContinuity()
    }

    private func markStatePersistenceFailure() {
        statusLock.withLock { statePersistenceBlocked = true }
        setStatus(.init(
            phase: .failed,
            detail: SyncText(
                "sync.status.state_persistence_failed",
                "CloudKit progress was not committed; a full refetch is required"
            )
        ))
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

    private func beginActivity() -> Bool {
        statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress else { return false }
            activeActivityCount += 1
            activityEpoch &+= 1
            return true
        }
    }

    private func beginActivity(for syncEngine: CKSyncEngine) -> Bool {
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

    private func endActivity() {
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

    private func waitForActivityDrain() async {
        await withCheckedContinuation { continuation in
            let resumeImmediately = statusLock.withLock { () -> Bool in
                if activeActivityCount == 0 { return true }
                activityDrainWaiters.append(continuation)
                return false
            }
            if resumeImmediately { continuation.resume() }
        }
    }

    private func markTransportActivity() {
        statusLock.withLock { activityEpoch &+= 1 }
    }

    private func beginBoundedSyncPass(persistentlyBlocked: Bool) throws -> UInt64 {
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

    private func finishBoundedSyncPass(_ passID: UInt64) {
        statusLock.withLock {
            guard boundedSyncPassActive, boundedSyncPassID == passID else { return }
            boundedSyncPassActive = false
            outboundBatchWindowDepth = 0
            activityEpoch &+= 1
        }
    }

    private func beginOutboundBatchWindow(passID: UInt64) -> UInt64? {
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

    private func endOutboundBatchWindow(_ passID: UInt64) {
        statusLock.withLock {
            guard boundedSyncPassID == passID, outboundBatchWindowDepth > 0 else { return }
            outboundBatchWindowDepth -= 1
            activityEpoch &+= 1
        }
    }

    private func canProvideRecordBatch(
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

    private func requestEventDrivenSyncIfUnbounded() {
        let handler = statusLock.withLock { () -> (@Sendable () -> Void)? in
            guard !isInvalidated, !boundedSyncPassActive else { return nil }
            return eventDrivenSyncHandler
        }
        handler?()
    }

    private func setRetryScheduledUnlessBlocked() {
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
    private func setSyncedIfActivityUnchanged(
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

    private static func blocksSuccessfulCompletion(
        _ phase: CloudKitSyncPhase
    ) -> Bool {
        switch phase {
        case .offline, .accountRequired, .retryScheduled, .quarantined, .failed:
            return true
        case .idle, .preparing, .syncing, .conflictResolved:
            return false
        }
    }

    private static func blocksOutboundTransport(_ phase: CloudKitSyncPhase) -> Bool {
        switch phase {
        case .offline, .accountRequired, .retryScheduled, .failed:
            return true
        case .idle, .preparing, .syncing, .conflictResolved, .quarantined:
            return false
        }
    }

    private func setStatus(_ status: CloudKitSyncStatus) {
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

    private func activeEngine() throws -> CKSyncEngine {
        try statusLock.withLock {
            guard !isInvalidated, !engineReplacementInProgress, let engine else {
                throw CloudKitSyncProviderError.unavailable
            }
            return engine
        }
    }

    private func updateSafetyState(
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
