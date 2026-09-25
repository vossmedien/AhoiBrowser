import Foundation
import AhoiCloudKitSpike

// ADR 0011 step 4 on the Companion: every fully separated Workspace of the Mac
// syncs into its own private-database zone `<main zone>-ws-<uuid>`. The
// Companion discovers those zones, lists each as its own Workspace (level
// "Vollständig getrennt"), syncs one only after the user enabled it here, keeps
// its records in its own repository and never merges them into the main tree.

/// Lists the zone names of the private database. Implemented by the CloudKit
/// provider; tests substitute a fake.
public protocol CloudKitRecordZoneListing: AnyObject, Sendable {
    func allRecordZoneNames() async throws -> [String]
}

public enum SeparatedWorkspaceSyncState: String, Codable, Sendable {
    case off
    case activating
    case ready
    case waitingForKey
    /// A ready key was seen before and is missing now (e.g. iCloud Keychain
    /// turned off). The session is paused; pages, website data store and local
    /// data stay, and it resumes when the key returns.
    case keyMissing
    case failed

    public var localizedLabel: String? {
        switch self {
        case .keyMissing:
            CompanionL10n.string("workspace.separated.key_missing", fallback: "Key missing")
        default:
            nil
        }
    }
}

public enum SeparatedWorkspaceRetirementReason: String, Codable, Sendable {
    /// The Workspace record is tombstoned in its own zone.
    case tombstoned
    /// A successful zone listing no longer contains its zone.
    case zoneRemoved
    // A missing key never retires a Workspace (sync-owner decision): it only
    // pauses the session, see `SeparatedWorkspaceSyncState.keyMissing`.
}

/// One separated Workspace as the Workspace list presents it.
public struct SeparatedWorkspaceEntry: Identifiable, Hashable, Sendable {
    public let workspaceID: UUID
    public let identifiers: SyncNamespaceIdentifiers
    public var name: String?
    public var icon: String?
    public var syncEnabled: Bool
    public var state: SeparatedWorkspaceSyncState

    public var id: UUID { workspaceID }

    public var displayName: String {
        if let name, !name.isEmpty { return name }
        return CompanionL10n.string(
            "workspace.separated.unnamed",
            fallback: "Separated workspace"
        )
    }

    /// ADR 0011 level `isolated`.
    public static var isolationLevelLabel: String {
        CompanionL10n.string("workspace.isolation.isolated", fallback: "Fully separated")
    }
}

/// Durable per-Workspace state. Sync is opt-in per Workspace and off for a
/// newly discovered zone.
public struct SeparatedWorkspaceRecord: Codable, Hashable, Sendable {
    public var workspaceID: UUID
    public var syncEnabled: Bool
    /// True once a ready key was seen; a later missing key then shows
    /// `keyMissing` (paused) instead of `waitingForKey`. Never retires.
    public var keyObserved: Bool
    public var name: String?
    public var icon: String?
    /// Set when a retirement could not finish (e.g. the data store was still
    /// in use); retried on the next refresh.
    public var pendingRetirement: SeparatedWorkspaceRetirementReason?

    public init(
        workspaceID: UUID,
        syncEnabled: Bool = false,
        keyObserved: Bool = false,
        name: String? = nil,
        icon: String? = nil,
        pendingRetirement: SeparatedWorkspaceRetirementReason? = nil
    ) {
        self.workspaceID = workspaceID
        self.syncEnabled = syncEnabled
        self.keyObserved = keyObserved
        self.name = name
        self.icon = icon
        self.pendingRetirement = pendingRetirement
    }
}

public protocol SeparatedWorkspaceStateStoring: AnyObject, Sendable {
    func load() -> [SeparatedWorkspaceRecord]
    func save(_ records: [SeparatedWorkspaceRecord])
}

public final class InMemorySeparatedWorkspaceStateStore: SeparatedWorkspaceStateStoring,
    @unchecked Sendable {
    private let lock = NSLock()
    private var records: [SeparatedWorkspaceRecord]

    public init(_ records: [SeparatedWorkspaceRecord] = []) { self.records = records }
    public func load() -> [SeparatedWorkspaceRecord] { lock.withLock { records } }
    public func save(_ records: [SeparatedWorkspaceRecord]) {
        lock.withLock { self.records = records }
    }
}

public final class UserDefaultsSeparatedWorkspaceStateStore: SeparatedWorkspaceStateStoring,
    @unchecked Sendable {
    public static let key = "AhoiMobile.SeparatedWorkspaces.State.v1"
    private let defaults: UserDefaults

    public init(defaults: UserDefaults) { self.defaults = defaults }

    public func load() -> [SeparatedWorkspaceRecord] {
        guard let data = defaults.data(forKey: Self.key),
              let records = try? JSONDecoder().decode(
                  [SeparatedWorkspaceRecord].self, from: data
              ) else { return [] }
        return records.filter { SyncNamespace.separatedWorkspace($0.workspaceID) != nil }
    }

    public func save(_ records: [SeparatedWorkspaceRecord]) {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        guard let data = try? encoder.encode(records) else { return }
        defaults.set(data, forKey: Self.key)
    }
}

/// One Workspace's own sync session: its own zone, subscription, key and
/// local repository. It never touches the main namespace.
@MainActor
public protocol SeparatedWorkspaceSyncSession: AnyObject {
    /// One bounded pass over this Workspace's zone; returns its own snapshot.
    func sync() async throws -> CompanionSnapshot
    /// Whether this namespace's canonical end-to-end key is still present.
    func isKeyPresent() async throws -> Bool
    func cancel() async
}

public struct SeparatedWorkspaceSessionContext: Hashable, Sendable {
    public let namespace: SyncNamespace
    public let identifiers: SyncNamespaceIdentifiers
    public var workspaceID: UUID { namespace.workspaceID! }
}

public enum SeparatedWorkspaceSessionActivation {
    case ready(any SeparatedWorkspaceSyncSession)
    /// The key has not arrived (yet); nothing is synced.
    case waitingForKey
    /// No canonical key exists for this namespace.
    case keyMissing
}

public typealias SeparatedWorkspaceSessionFactory = @MainActor (
    SeparatedWorkspaceSessionContext
) async throws -> SeparatedWorkspaceSessionActivation

@MainActor
public final class SeparatedWorkspaceSyncCoordinator: ObservableObject {
    @Published public private(set) var entries: [SeparatedWorkspaceEntry] = []
    /// Most recent retirements, for diagnostics and tests.
    public private(set) var retirements: [UUID: SeparatedWorkspaceRetirementReason] = [:]

    private let base: SyncNamespaceIdentifiers?
    private let stateStore: any SeparatedWorkspaceStateStoring
    private let dataStores: (any SeparatedWorkspaceDataStoreLifecycle)?
    private let sessionFactory: SeparatedWorkspaceSessionFactory?
    private let removeLocalData: (@MainActor (UUID) async -> Void)?
    private var records: [UUID: SeparatedWorkspaceRecord]
    private var sessions: [UUID: any SeparatedWorkspaceSyncSession] = [:]
    private var states: [UUID: SeparatedWorkspaceSyncState] = [:]
    private var passInProgress = false

    /// An unconfigured coordinator (no base identifiers) lists nothing and
    /// never creates a session.
    public init(
        base: SyncNamespaceIdentifiers? = nil,
        stateStore: any SeparatedWorkspaceStateStoring = InMemorySeparatedWorkspaceStateStore(),
        dataStores: (any SeparatedWorkspaceDataStoreLifecycle)? = nil,
        sessionFactory: SeparatedWorkspaceSessionFactory? = nil,
        removeLocalData: (@MainActor (UUID) async -> Void)? = nil
    ) {
        // A base that cannot derive a separated namespace disables the feature.
        self.base = base.flatMap { base in
            SyncNamespace.separatedWorkspace(UUID())?.resolve(base) == nil ? nil : base
        }
        self.stateStore = stateStore
        self.dataStores = dataStores
        self.sessionFactory = sessionFactory
        self.removeLocalData = removeLocalData
        let loaded = self.base == nil ? [] : stateStore.load()
        self.records = Dictionary(
            loaded.map { ($0.workspaceID, $0) }, uniquingKeysWith: { lhs, _ in lhs }
        )
        for id in records.keys { dataStores?.registerSeparatedWorkspace(id) }
        publish()
    }

    public var isConfigured: Bool { base != nil }

    public var hasActiveSessions: Bool { !sessions.isEmpty }

    public func isSeparatedWorkspace(_ workspaceID: UUID) -> Bool {
        records[workspaceID].map { $0.pendingRetirement == nil } ?? false
    }

    public func identifiers(for workspaceID: UUID) -> SyncNamespaceIdentifiers? {
        guard let base, let namespace = SyncNamespace.separatedWorkspace(workspaceID) else {
            return nil
        }
        return namespace.resolve(base)
    }

    /// Lists the private database's zones. A listing failure changes nothing:
    /// only a successful listing without a zone retires that Workspace.
    @discardableResult
    public func refreshDiscovery(using lister: any CloudKitRecordZoneListing) async -> Bool {
        guard isConfigured else { return false }
        let zoneNames: [String]
        do {
            zoneNames = try await lister.allRecordZoneNames()
        } catch {
            return false
        }
        await applyDiscoveredZoneNames(zoneNames)
        return true
    }

    /// Applies one complete zone listing: new separated zones appear with sync
    /// off; a known Workspace whose zone is gone is retired. The main zone,
    /// foreign zones and malformed names are ignored.
    public func applyDiscoveredZoneNames(_ zoneNames: [String]) async {
        guard let base else { return }
        let discovered = Set(zoneNames.compactMap {
            SyncNamespace.separatedWorkspace(fromZoneName: $0, base: base)?.workspaceID
        })
        for id in records.keys.sorted(by: { $0.uuidString < $1.uuidString })
            where !discovered.contains(id) {
            await retire(id, reason: .zoneRemoved)
        }
        for id in discovered where records[id] == nil {
            records[id] = SeparatedWorkspaceRecord(workspaceID: id)
            dataStores?.registerSeparatedWorkspace(id)
        }
        await retryPendingRetirements()
        persist()
        publish()
    }

    /// The user's per-Workspace opt-in. Disabling keeps its local data.
    public func setSyncEnabled(_ enabled: Bool, for workspaceID: UUID) async {
        guard var record = records[workspaceID], record.pendingRetirement == nil else {
            return
        }
        record.syncEnabled = enabled
        records[workspaceID] = record
        persist()
        if enabled {
            await activateIfNeeded(workspaceID)
        } else {
            await sessions.removeValue(forKey: workspaceID)?.cancel()
            states[workspaceID] = .off
        }
        publish()
    }

    /// One pass over every enabled separated Workspace, each in its own
    /// session. Applies tombstones and key loss.
    public func syncEnabledWorkspaces() async {
        guard isConfigured, !passInProgress else { return }
        passInProgress = true
        defer { passInProgress = false }
        await retryPendingRetirements()
        for id in records.keys.sorted(by: { $0.uuidString < $1.uuidString }) {
            guard let record = records[id], record.syncEnabled,
                  record.pendingRetirement == nil else { continue }
            await activateIfNeeded(id)
            guard let session = sessions[id] else { continue }
            if let present = try? await session.isKeyPresent(), !present {
                await pauseForMissingKey(id)
                continue
            }
            do {
                let snapshot = try await session.sync()
                guard sessions[id] === session else { continue }
                if let workspace = snapshot.workspaces.first(where: {
                    $0.id.rawValue == id
                }) {
                    if workspace.isDeleted {
                        await retire(id, reason: .tombstoned)
                        continue
                    }
                    records[id]?.name = workspace.name
                    records[id]?.icon = workspace.icon
                }
                states[id] = .ready
            } catch {
                states[id] = .failed
            }
        }
        persist()
        publish()
    }

    /// Main sync was switched off: stop every session, keep the opt-ins.
    public func suspendAll() async {
        let active = sessions
        sessions.removeAll()
        for session in active.values { await session.cancel() }
        for id in active.keys { states[id] = .off }
        publish()
    }

    private func activateIfNeeded(_ id: UUID) async {
        guard sessions[id] == nil, let sessionFactory,
              let namespace = SyncNamespace.separatedWorkspace(id),
              let identifiers = identifiers(for: id),
              let base, identifiers.zoneName != base.zoneName,
              identifiers.keychainAccount != base.keychainAccount else {
            if sessions[id] == nil { states[id] = sessionFactory == nil ? .off : .failed }
            return
        }
        states[id] = .activating
        publish()
        let activation: SeparatedWorkspaceSessionActivation
        do {
            activation = try await sessionFactory(.init(
                namespace: namespace, identifiers: identifiers
            ))
        } catch {
            states[id] = .failed
            return
        }
        // The user may have switched it off or it may have been retired meanwhile.
        guard let record = records[id], record.syncEnabled,
              record.pendingRetirement == nil else {
            if case let .ready(session) = activation { await session.cancel() }
            return
        }
        switch activation {
        case let .ready(session):
            sessions[id] = session
            records[id]?.keyObserved = true
            states[id] = .ready
            persist()
        case .waitingForKey:
            states[id] = .waitingForKey
        case .keyMissing:
            states[id] = record.keyObserved ? .keyMissing : .waitingForKey
        }
    }

    /// Stops only the session. The Workspace, its opt-in, its pages, its
    /// website data store and its local data all stay; the next pass tries to
    /// activate it again and resumes once the key is back.
    private func pauseForMissingKey(_ id: UUID) async {
        await sessions.removeValue(forKey: id)?.cancel()
        states[id] = .keyMissing
        publish()
    }

    private func retire(_ id: UUID, reason: SeparatedWorkspaceRetirementReason) async {
        guard var record = records[id] else { return }
        record.pendingRetirement = record.pendingRetirement ?? reason
        record.syncEnabled = false
        records[id] = record
        persist()
        await sessions.removeValue(forKey: id)?.cancel()
        states[id] = .off
        do {
            try await dataStores?.removeDataStore(for: id)
        } catch {
            // Stays pending: the store is retried on the next refresh.
            publish()
            return
        }
        await removeLocalData?(id)
        records.removeValue(forKey: id)
        states.removeValue(forKey: id)
        retirements[id] = reason
        persist()
        publish()
    }

    private func retryPendingRetirements() async {
        for record in records.values {
            if let reason = record.pendingRetirement {
                await retire(record.workspaceID, reason: reason)
            }
        }
    }

    private func persist() {
        guard isConfigured else { return }
        stateStore.save(records.values.sorted { $0.workspaceID.uuidString < $1.workspaceID.uuidString })
    }

    private func publish() {
        entries = records.values
            .filter { $0.pendingRetirement == nil }
            .compactMap { record -> SeparatedWorkspaceEntry? in
                guard let identifiers = identifiers(for: record.workspaceID) else { return nil }
                return SeparatedWorkspaceEntry(
                    workspaceID: record.workspaceID,
                    identifiers: identifiers,
                    name: record.name,
                    icon: record.icon,
                    syncEnabled: record.syncEnabled,
                    state: states[record.workspaceID] ?? .off
                )
            }
            .sorted {
                let lhs = $0.displayName.localizedLowercase
                let rhs = $1.displayName.localizedLowercase
                return lhs == rhs ? $0.workspaceID.uuidString < $1.workspaceID.uuidString : lhs < rhs
            }
    }
}
