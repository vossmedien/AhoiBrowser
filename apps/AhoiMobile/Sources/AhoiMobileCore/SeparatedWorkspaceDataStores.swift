import Foundation
import WebKit

/// What the separated-Workspace coordinator needs from the website-data-store
/// owner. It never sees WebKit, so its lifecycle is testable with a fake.
@MainActor
public protocol SeparatedWorkspaceDataStoreLifecycle: AnyObject {
    /// Workspaces whose pages must open in their own data store. Persisted, so
    /// pages restored at launch never touch the default store before
    /// discovery has run.
    var separatedWorkspaceIDs: Set<UUID> { get }
    func registerSeparatedWorkspace(_ workspaceID: UUID)
    /// Closes the Workspace's pages, then removes its data store. Idempotent;
    /// the Workspace stays registered (fail closed) until removal succeeded.
    func removeDataStore(for workspaceID: UUID) async throws
}

/// Persistence for the registered separated Workspace ids.
public protocol SeparatedWorkspaceIDStoring: AnyObject, Sendable {
    func load() -> Set<UUID>
    func save(_ ids: Set<UUID>)
}

public final class InMemorySeparatedWorkspaceIDStore: SeparatedWorkspaceIDStoring,
    @unchecked Sendable {
    private let lock = NSLock()
    private var ids: Set<UUID>

    public init(_ ids: Set<UUID> = []) { self.ids = ids }
    public func load() -> Set<UUID> { lock.withLock { ids } }
    public func save(_ ids: Set<UUID>) { lock.withLock { self.ids = ids } }
}

public final class UserDefaultsSeparatedWorkspaceIDStore: SeparatedWorkspaceIDStoring,
    @unchecked Sendable {
    public static let key = "AhoiMobile.SeparatedWorkspaces.DataStoreIDs.v1"
    private let defaults: UserDefaults

    public init(defaults: UserDefaults) { self.defaults = defaults }

    public func load() -> Set<UUID> {
        Set((defaults.stringArray(forKey: Self.key) ?? []).compactMap {
            SyncNamespace.separatedWorkspace(lowercaseUUID: $0)?.workspaceID
        })
    }

    public func save(_ ids: Set<UUID>) {
        defaults.set(ids.map { $0.uuidString.lowercased() }.sorted(), forKey: Self.key)
    }
}

/// Owns one website data store per fully separated Workspace, keyed by the
/// Workspace UUID (ADR 0011 step 4). Generic over the store type so tests run
/// without WebKit; production uses `WKWebsiteDataStore(forIdentifier:)`.
@MainActor
public final class SeparatedWorkspaceDataStoreRegistry<Store: AnyObject>:
    SeparatedWorkspaceDataStoreLifecycle {
    private let makeStore: @MainActor (UUID) -> Store
    private let removeStore: @MainActor (UUID) async throws -> Void
    private let idStore: any SeparatedWorkspaceIDStoring
    private var stores: [UUID: Store] = [:]
    public private(set) var separatedWorkspaceIDs: Set<UUID>
    /// Called before a store is removed, so every page using it is closed
    /// first (WebKit refuses to remove a store that is still in use).
    public var willRemoveDataStore: (@MainActor (UUID) async -> Void)?

    public init(
        idStore: any SeparatedWorkspaceIDStoring,
        makeStore: @escaping @MainActor (UUID) -> Store,
        removeStore: @escaping @MainActor (UUID) async throws -> Void
    ) {
        self.idStore = idStore
        self.makeStore = makeStore
        self.removeStore = removeStore
        self.separatedWorkspaceIDs = idStore.load()
    }

    public func isSeparated(_ workspaceID: UUID?) -> Bool {
        workspaceID.map { separatedWorkspaceIDs.contains($0) } ?? false
    }

    public func registerSeparatedWorkspace(_ workspaceID: UUID) {
        guard SyncNamespace.separatedWorkspace(workspaceID) != nil,
              separatedWorkspaceIDs.insert(workspaceID).inserted else { return }
        idStore.save(separatedWorkspaceIDs)
    }

    /// The dedicated store of a registered separated Workspace; nil for any
    /// other Workspace, which then keeps the normal store.
    public func dataStore(for workspaceID: UUID) -> Store? {
        guard separatedWorkspaceIDs.contains(workspaceID) else { return nil }
        if let store = stores[workspaceID] { return store }
        let store = makeStore(workspaceID)
        stores[workspaceID] = store
        return store
    }

    public func removeDataStore(for workspaceID: UUID) async throws {
        await willRemoveDataStore?(workspaceID)
        stores.removeValue(forKey: workspaceID)
        try await removeStore(workspaceID)
        if separatedWorkspaceIDs.remove(workspaceID) != nil {
            idStore.save(separatedWorkspaceIDs)
        }
    }
}

public typealias MobileSeparatedWorkspaceDataStores =
    SeparatedWorkspaceDataStoreRegistry<WKWebsiteDataStore>

extension SeparatedWorkspaceDataStoreRegistry where Store == WKWebsiteDataStore {
    /// Production registry: `WKWebsiteDataStore(forIdentifier:)` keyed by the
    /// Workspace UUID, never `.default()`. A missing store counts as removed.
    public static func webKit(
        idStore: any SeparatedWorkspaceIDStoring
    ) -> SeparatedWorkspaceDataStoreRegistry<WKWebsiteDataStore> {
        .init(
            idStore: idStore,
            makeStore: { WKWebsiteDataStore(forIdentifier: $0) },
            removeStore: { workspaceID in
                let existing = await WKWebsiteDataStore.allDataStoreIdentifiers
                guard existing.contains(workspaceID) else { return }
                try await WKWebsiteDataStore.remove(forIdentifier: workspaceID)
            }
        )
    }
}
