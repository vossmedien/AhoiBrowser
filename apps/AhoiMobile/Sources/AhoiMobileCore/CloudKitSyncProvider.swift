import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif

func SyncText(_ key: String, _ fallback: String) -> String {
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
    struct QuarantineResolutionCandidate {
        let record: SyncRecord
        let generation: UUID
    }

    let configuration: CloudKitSyncConfiguration
    let boundary: SyncBoundary
    let recordStore: any LocalSyncRecordStore
    let stateStore: any SyncEngineStateStore
    let quarantineStore: any SyncQuarantineStore
    let systemFieldsStore: any CloudKitSystemFieldsStore
    let codec = AppleCloudKitRecordCodec()
    let zoneID: CKRecordZone.ID
    let container: CKContainer
    let database: CKDatabase
    let statusLock = NSLock()
    let bookmarkTransportAuthorization = BookmarkTransportAuthorization()
    var currentStatus = CloudKitSyncStatus(
        phase: .idle,
        detail: CompanionL10n.string(
            "sync.status.not_yet_synced",
            fallback: "Not synced yet"
        )
    )
    var engine: CKSyncEngine?
    var engineReplacementInProgress = false
    var accountTransitionPending = false
    var zoneRecoveryPending = false
    var lastKnownAccountIdentifier: String?
    var accountContinuityVerified = false
    var zonePreparationInProgress = false
    var isInvalidated = false
    var activeActivityCount = 0
    var activityDrainWaiters: [CheckedContinuation<Void, Never>] = []
    var activityEpoch: UInt64 = 0
    var boundedSyncPassID: UInt64 = 0
    var boundedSyncPassActive = false
    var boundedSyncPassCompletionBlocked = false
    var boundedSyncPassOutboundBlocked = false
    var outboundBatchWindowDepth = 0
    var eventDrivenSyncHandler: (@Sendable () -> Void)?
    var statePersistenceBlocked = false
    var quarantineResolutionCandidates: [UUID: QuarantineResolutionCandidate] = [:]
    var authorizedDeveloperAssetIDs: Set<UUID> = []
    var developerAssetAuthorizationMutationEpoch: UInt64 = 0
    var developerAssetAuthorizationLastMutation: [UUID: UInt64] = [:]
    /// Developer assets skipped while a fresh CKSyncEngine is rebuilt. They
    /// may be queued only after the bridge has re-proved the corresponding
    /// domain opt-in from the local snapshot.
    var deferredDeveloperAssetRehydrationIDs: Set<UUID> = []
    var developerAssetAuthorizationReady = false
    var transportRehydrationRequired = false
    var engineGeneration: UInt64 = 1
    var accountVerificationGeneration: UInt64 = 0
    var accountVerificationTask: (
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
