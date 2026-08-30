import XCTest
@testable import AhoiMobileCore

final class CompanionSyncPreferenceTests: XCTestCase {
    @MainActor
    func testDisableAfterActivationFailureClearsSyncPresentationState() async throws {
        let suiteName = "CompanionSyncPreferenceTests-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }
        let model = CompanionAppModel(
            repository: LocalFirstRepository(store: InMemoryCompanionStore()),
            syncRuntimeFactory: { throw SyncActivationTestError.failed },
            defaults: defaults
        )

        await model.setSyncEnabled(true)

        XCTAssertFalse(model.isSyncConfigured)
        XCTAssertEqual(
            model.keyLifecycleStatus,
            .recovery(reason: .keychainFailure, keyVersion: nil)
        )
        XCTAssertNotNil(model.loadError)

        await model.setSyncEnabled(false)

        XCTAssertFalse(model.isSyncConfigured)
        XCTAssertNil(model.syncStatus)
        XCTAssertEqual(model.syncSafetyState, .init())
        XCTAssertFalse(model.physicalDeletionRecoveryRequired)
        XCTAssertNil(model.remoteControlIdentity)
        XCTAssertNil(model.remoteCommandStatus)
        XCTAssertTrue(model.recentRemoteCommands.isEmpty)
        XCTAssertEqual(model.keyLifecycleStatus, .disabled)
        XCTAssertNil(model.loadError)
    }

    @MainActor
    func testOptOutWhileFactoryAwaitsDiscardsStaleActivation() async throws {
        let script = DelayedSyncActivationScript(recordFirstDiscard: true)
        let (model, suiteName) = try makeModel(factory: { await script.makeActivation() })
        defer { UserDefaults.standard.removePersistentDomain(forName: suiteName) }
        let activation = Task { @MainActor in
            await model.setSyncEnabled(true)
        }
        await script.waitUntilFirstCallStarts()

        await model.setSyncEnabled(false)
        await script.releaseFirstCall()
        await activation.value

        let discardCount = await script.discardCount()
        let callCount = await script.callCount()
        XCTAssertEqual(discardCount, 1)
        XCTAssertEqual(callCount, 1)
        XCTAssertFalse(model.isSyncConfigured)
        XCTAssertEqual(model.keyLifecycleStatus, .disabled)
        XCTAssertNil(model.loadError)
        XCTAssertNil(model.syncProvider)
        XCTAssertNil(model.syncBridge)
    }

    @MainActor
    func testDuplicateEnableIntentSharesOneActivation() async throws {
        let script = DelayedSyncActivationScript(recordFirstDiscard: false)
        let (model, suiteName) = try makeModel(factory: { await script.makeActivation() })
        defer { UserDefaults.standard.removePersistentDomain(forName: suiteName) }
        let first = Task { @MainActor in
            await model.setSyncEnabled(true)
        }
        await script.waitUntilFirstCallStarts()
        let duplicate = Task { @MainActor in
            await model.setSyncEnabled(true)
        }
        await Task.yield()

        let callsWhileBlocked = await script.callCount()
        XCTAssertEqual(callsWhileBlocked, 1)

        await script.releaseFirstCall()
        await first.value
        await duplicate.value

        let finalCallCount = await script.callCount()
        XCTAssertEqual(finalCallCount, 1)
        XCTAssertFalse(model.isSyncConfigured)
        XCTAssertEqual(
            model.keyLifecycleStatus,
            .waiting(keyVersion: 1, reason: .synchronizableKeyPending)
        )
    }

    @MainActor
    func testRapidEnableDisableEnableHonorsLatestIntent() async throws {
        let script = DelayedSyncActivationScript(recordFirstDiscard: true)
        let (model, suiteName) = try makeModel(factory: { await script.makeActivation() })
        defer { UserDefaults.standard.removePersistentDomain(forName: suiteName) }
        let first = Task { @MainActor in
            await model.setSyncEnabled(true)
        }
        await script.waitUntilFirstCallStarts()

        await model.setSyncEnabled(false)
        let latest = Task { @MainActor in
            await model.setSyncEnabled(true)
        }
        await Task.yield()
        await script.releaseFirstCall()
        await first.value
        await latest.value

        let discardCount = await script.discardCount()
        let callCount = await script.callCount()
        XCTAssertEqual(discardCount, 1)
        XCTAssertEqual(callCount, 2)
        XCTAssertFalse(model.isSyncConfigured)
        XCTAssertEqual(
            model.keyLifecycleStatus,
            .waiting(keyVersion: 1, reason: .synchronizableKeyPending)
        )
        XCTAssertNotNil(model.loadError)
        XCTAssertNil(model.syncProvider)
        XCTAssertNil(model.syncBridge)
    }

    @MainActor
    private func makeModel(
        factory: @escaping CompanionSyncRuntimeFactory
    ) throws -> (model: CompanionAppModel, suiteName: String) {
        let suiteName = "CompanionSyncPreferenceTests-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suiteName))
        let model = CompanionAppModel(
            repository: LocalFirstRepository(store: InMemoryCompanionStore()),
            syncRuntimeFactory: factory,
            defaults: defaults
        )
        return (model, suiteName)
    }
}

private enum SyncActivationTestError: LocalizedError {
    case failed

    var errorDescription: String? {
        "Synthetic sync activation failure"
    }
}

private actor DelayedSyncActivationScript {
    private let recordFirstDiscard: Bool
    private var calls = 0
    private var discards = 0
    private var firstCallStarted = false
    private var firstCallStartWaiters: [CheckedContinuation<Void, Never>] = []
    private var firstCallRelease: CheckedContinuation<Void, Never>?

    init(recordFirstDiscard: Bool) {
        self.recordFirstDiscard = recordFirstDiscard
    }

    func makeActivation() async -> CompanionSyncRuntimeActivation {
        calls += 1
        let call = calls
        if call == 1 {
            firstCallStarted = true
            let waiters = firstCallStartWaiters
            firstCallStartWaiters.removeAll()
            waiters.forEach { $0.resume() }
            await withCheckedContinuation { continuation in
                firstCallRelease = continuation
            }
        }

        if call == 1, recordFirstDiscard {
            return .init(
                status: .ready(keyVersion: 1),
                runtime: nil,
                discardHandler: { await self.recordDiscard() }
            )
        }
        return .init(
            status: .waiting(keyVersion: 1, reason: .synchronizableKeyPending),
            runtime: nil
        )
    }

    func waitUntilFirstCallStarts() async {
        guard !firstCallStarted else { return }
        await withCheckedContinuation { continuation in
            firstCallStartWaiters.append(continuation)
        }
    }

    func releaseFirstCall() {
        firstCallRelease?.resume()
        firstCallRelease = nil
    }

    func callCount() -> Int {
        calls
    }

    func discardCount() -> Int {
        discards
    }

    private func recordDiscard() {
        discards += 1
    }
}
