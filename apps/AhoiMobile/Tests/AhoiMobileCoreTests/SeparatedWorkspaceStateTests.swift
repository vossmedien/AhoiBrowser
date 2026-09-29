import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

/// Review row C1 (Crest 2dbdc442): persisted separated-Workspace state this
/// build cannot read fails closed. It is never read as "no Workspaces", so
/// nothing syncs, retires or overwrites it until a later read succeeds.
@MainActor
final class SeparatedWorkspaceStateTests: XCTestCase {
    private let workspaceA = UUID(uuidString: "0f8b2c4e-6a1d-4e3b-9c5f-7d2a1b3c4e5f")!
    private let workspaceB = UUID(uuidString: "1a2b3c4d-5e6f-4a1b-8c2d-3e4f5a6b7c8d")!
    private let base = SyncNamespaceIdentifiers(
        zoneName: "AhoiBrowserSyncV3",
        subscriptionIdentifier: "AhoiBrowserSyncSubscription",
        keychainAccount: "payload-key"
    )
    private var factoryContexts: [SeparatedWorkspaceSessionContext] = []
    private var removedStores: [UUID] = []
    private var closedBeforeRemoval: [UUID] = []
    private var removedLocalData: [UUID] = []

    func testMissingStateReadsAsNoWorkspaces() throws {
        let store = UserDefaultsSeparatedWorkspaceStateStore(defaults: try isolatedDefaults())
        XCTAssertEqual(store.loadState(), .records([]))
        XCTAssertEqual(store.load(), [])
    }

    func testStateOfAnotherTypeIsUnreadable() throws {
        let defaults = try isolatedDefaults()
        defaults.set("not data", forKey: UserDefaultsSeparatedWorkspaceStateStore.key)
        let store = UserDefaultsSeparatedWorkspaceStateStore(defaults: defaults)
        XCTAssertEqual(store.loadState(), .unreadable)
        XCTAssertEqual(store.load(), [], "The legacy accessor still reports no records.")
    }

    /// A value this build does not know (here a newer retirement reason) must
    /// not become an empty list that the next persist writes back: that would
    /// lose every opt-in and orphan a retiring Workspace's data store.
    func testUnreadableStateFailsClosedAndKeepsItsBytes() async throws {
        let defaults = try isolatedDefaults()
        let bytes = Data("""
        [{"workspaceID":"0F8B2C4E-6A1D-4E3B-9C5F-7D2A1B3C4E5F","syncEnabled":true,\
        "keyObserved":true,"pendingRetirement":"retiredByAFutureBuild","accountIdentifier":"_me"}]
        """.utf8)
        defaults.set(bytes, forKey: UserDefaultsSeparatedWorkspaceStateStore.key)
        let store = UserDefaultsSeparatedWorkspaceStateStore(defaults: defaults)
        XCTAssertEqual(store.loadState(), .unreadable)

        let (coordinator, dataStores) = makeCoordinator(stateStore: store)
        XCTAssertTrue(coordinator.isStateUnreadable)
        XCTAssertTrue(coordinator.entries.isEmpty)
        XCTAssertFalse(SeparatedWorkspaceSyncCoordinator.stateUnreadableLabel.isEmpty)
        // The data store registry is its own record and stays as it is.
        dataStores.registerSeparatedWorkspace(workspaceA)

        let refreshed = await coordinator.refreshDiscovery(
            using: StateTestZoneLister([zone(workspaceB)], account: "_me")
        )
        XCTAssertFalse(refreshed)
        await coordinator.applyDiscoveredZoneNames([], accountIdentifier: "_me")
        await coordinator.setSyncEnabled(true, for: workspaceA)
        await coordinator.syncEnabledWorkspaces()

        XCTAssertTrue(factoryContexts.isEmpty, "Nothing syncs from a blank state.")
        XCTAssertTrue(removedStores.isEmpty)
        XCTAssertTrue(closedBeforeRemoval.isEmpty)
        XCTAssertTrue(removedLocalData.isEmpty)
        XCTAssertTrue(coordinator.retirements.isEmpty)
        XCTAssertEqual(dataStores.separatedWorkspaceIDs, [workspaceA])
        XCTAssertTrue(coordinator.entries.isEmpty)
        XCTAssertTrue(coordinator.isStateUnreadable)
        XCTAssertEqual(defaults.data(forKey: UserDefaultsSeparatedWorkspaceStateStore.key), bytes,
                       "The unreadable bytes are never overwritten.")
    }

    /// Once the state reads again (for example after an update that knows the
    /// value), the next pass adopts it and resumes.
    func testReadableStateResumesTheNextPass() async throws {
        let defaults = try isolatedDefaults()
        defaults.set(Data("not json".utf8), forKey: UserDefaultsSeparatedWorkspaceStateStore.key)
        let store = UserDefaultsSeparatedWorkspaceStateStore(defaults: defaults)
        let (coordinator, dataStores) = makeCoordinator(stateStore: store)
        XCTAssertTrue(coordinator.isStateUnreadable)

        store.save([SeparatedWorkspaceRecord(
            workspaceID: workspaceA, syncEnabled: true, accountIdentifier: "_me"
        )])
        await coordinator.syncEnabledWorkspaces()
        XCTAssertFalse(coordinator.isStateUnreadable)
        XCTAssertEqual(coordinator.entries.map(\.workspaceID), [workspaceA])
        XCTAssertEqual(factoryContexts.map(\.workspaceID), [workspaceA])
        XCTAssertTrue(dataStores.separatedWorkspaceIDs.contains(workspaceA))
    }

    // MARK: - Helpers

    private func isolatedDefaults() throws -> UserDefaults {
        let suite = "AhoiMobileCoreTests.SeparatedState.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
        return defaults
    }

    private func zone(_ id: UUID) -> String {
        SyncNamespace.separatedWorkspace(id)!.resolve(base)!.zoneName
    }

    private func makeCoordinator(
        stateStore: any SeparatedWorkspaceStateStoring
    ) -> (SeparatedWorkspaceSyncCoordinator, SeparatedWorkspaceDataStoreRegistry<StateTestDataStore>) {
        let dataStores = SeparatedWorkspaceDataStoreRegistry<StateTestDataStore>(
            idStore: InMemorySeparatedWorkspaceIDStore(),
            makeStore: { StateTestDataStore(id: $0) },
            removeStore: { [unowned self] id in removedStores.append(id) }
        )
        dataStores.willRemoveDataStore = { [unowned self] id in closedBeforeRemoval.append(id) }
        let coordinator = SeparatedWorkspaceSyncCoordinator(
            base: base,
            stateStore: stateStore,
            dataStores: dataStores,
            sessionFactory: { [unowned self] context in
                factoryContexts.append(context)
                return .ready(StateTestSession())
            },
            removeLocalData: { [unowned self] id in removedLocalData.append(id) }
        )
        return (coordinator, dataStores)
    }
}

private final class StateTestDataStore {
    let id: UUID
    init(id: UUID) { self.id = id }
}

private final class StateTestZoneLister: CloudKitRecordZoneListing, @unchecked Sendable {
    let names: [String]
    let account: String?
    init(_ names: [String], account: String?) {
        self.names = names
        self.account = account
    }
    func allRecordZoneNames() async throws -> [String] { names }
    func currentAccountIdentifier() async throws -> String? { account }
}

@MainActor
private final class StateTestSession: SeparatedWorkspaceSyncSession {
    func sync() async throws -> CompanionSnapshot { .empty }
    func isKeyPresent() async throws -> Bool { true }
    func cancel() async {}
}
