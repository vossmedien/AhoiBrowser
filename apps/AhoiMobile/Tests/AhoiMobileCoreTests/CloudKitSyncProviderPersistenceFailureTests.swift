import Foundation
import XCTest
@testable import AhoiMobileCore

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
final class CloudKitSyncProviderPersistenceFailureTests: XCTestCase {
    func testSafetyStateWriteFailureReturnsBlockAfterDurableFallback() throws {
        let store = FailureInjectingSyncEngineStateStore()
        store.failNextSafetyStateSaves(1)
        let desired = CloudKitSyncSafetyState(
            accountTransitionPending: false,
            zoneRecoveryPending: false,
            lastKnownAccountIdentifier: "account-a"
        )
        let failClosed = CloudKitSyncSafetyState(
            accountTransitionPending: true,
            zoneRecoveryPending: true,
            lastKnownAccountIdentifier: "account-a"
        )

        let decision = CloudKitStatePersistence.persistSafetyState(
            desired,
            failClosedState: failClosed,
            in: store
        )

        XCTAssertFalse(decision.primaryCommitted)
        XCTAssertTrue(decision.fallbackAttempted)
        XCTAssertTrue(decision.fallbackCommitted)
        XCTAssertTrue(decision.requiresTransportBlock)
        XCTAssertEqual(decision.primaryError as? InjectedStateStoreError, .writeFailed)
        XCTAssertNil(decision.fallbackError)
        XCTAssertEqual(store.safetyStateSaveAttempts, 2)
        XCTAssertEqual(try store.loadSafetyState(), failClosed)
    }

    func testEngineStateClearFailureReturnsBlockWithoutHidingError() {
        let store = FailureInjectingSyncEngineStateStore()
        store.failNextClears(1)

        let decision = CloudKitStatePersistence.clearEngineState(in: store)

        XCTAssertFalse(decision.primaryCommitted)
        XCTAssertFalse(decision.fallbackAttempted)
        XCTAssertFalse(decision.fallbackCommitted)
        XCTAssertTrue(decision.requiresTransportBlock)
        XCTAssertEqual(decision.primaryError as? InjectedStateStoreError, .clearFailed)
        XCTAssertNil(decision.fallbackError)
        XCTAssertEqual(store.clearAttempts, 1)
    }

    func testSafetyStateFallbackFailureReportsBothAttemptsAndReturnsBlock() throws {
        let store = FailureInjectingSyncEngineStateStore()
        store.failAllSafetyStateSaves()
        let desired = CloudKitSyncSafetyState(zoneRecoveryPending: true)
        let failClosed = CloudKitSyncSafetyState(
            accountTransitionPending: true,
            zoneRecoveryPending: true
        )

        let decision = CloudKitStatePersistence.persistSafetyState(
            desired,
            failClosedState: failClosed,
            in: store
        )

        XCTAssertFalse(decision.primaryCommitted)
        XCTAssertTrue(decision.fallbackAttempted)
        XCTAssertFalse(decision.fallbackCommitted)
        XCTAssertTrue(decision.requiresTransportBlock)
        XCTAssertEqual(decision.primaryError as? InjectedStateStoreError, .writeFailed)
        XCTAssertEqual(decision.fallbackError as? InjectedStateStoreError, .writeFailed)
        XCTAssertEqual(store.safetyStateSaveAttempts, 2)
        XCTAssertEqual(try store.loadSafetyState(), .init())
    }
}

private enum InjectedStateStoreError: Error, Equatable {
    case writeFailed
    case clearFailed
}

private final class FailureInjectingSyncEngineStateStore:
    SyncEngineStateStore,
    @unchecked Sendable
{
    private let lock = NSLock()
    private var serialization: CKSyncEngine.State.Serialization?
    private var safetyState = CloudKitSyncSafetyState()
    private var remainingClearFailures = 0
    private var remainingSafetyStateSaveFailures = 0
    private var alwaysFailSafetyStateSaves = false
    private var clearAttemptCount = 0
    private var safetyStateSaveAttemptCount = 0

    var clearAttempts: Int {
        lock.withLock { clearAttemptCount }
    }

    var safetyStateSaveAttempts: Int {
        lock.withLock { safetyStateSaveAttemptCount }
    }

    func failNextClears(_ count: Int) {
        lock.withLock { remainingClearFailures = count }
    }

    func failNextSafetyStateSaves(_ count: Int) {
        lock.withLock { remainingSafetyStateSaveFailures = count }
    }

    func failAllSafetyStateSaves() {
        lock.withLock { alwaysFailSafetyStateSaves = true }
    }

    func load() throws -> CKSyncEngine.State.Serialization? {
        lock.withLock { serialization }
    }

    func save(_ serialization: CKSyncEngine.State.Serialization) throws {
        lock.withLock { self.serialization = serialization }
    }

    func clear() throws {
        try lock.withLock {
            clearAttemptCount += 1
            if remainingClearFailures > 0 {
                remainingClearFailures -= 1
                throw InjectedStateStoreError.clearFailed
            }
            serialization = nil
        }
    }

    func loadSafetyState() throws -> CloudKitSyncSafetyState {
        lock.withLock { safetyState }
    }

    func saveSafetyState(_ state: CloudKitSyncSafetyState) throws {
        try lock.withLock {
            safetyStateSaveAttemptCount += 1
            if alwaysFailSafetyStateSaves || remainingSafetyStateSaveFailures > 0 {
                remainingSafetyStateSaveFailures = max(
                    0,
                    remainingSafetyStateSaveFailures - 1
                )
                throw InjectedStateStoreError.writeFailed
            }
            safetyState = state
        }
    }
}
#endif
