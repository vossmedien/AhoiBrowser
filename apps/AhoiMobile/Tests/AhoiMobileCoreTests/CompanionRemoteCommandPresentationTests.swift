import Foundation
import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionRemoteCommandPresentationTests: XCTestCase {
    private final class TestTime: @unchecked Sendable {
        private let lock = NSLock()
        private var value: UInt64

        init(_ value: UInt64) {
            self.value = value
        }

        func now() -> UInt64 {
            lock.withLock { value }
        }

        func advance(by milliseconds: UInt64) {
            lock.withLock { value &+= milliseconds }
        }
    }

    @MainActor
    func testQueuedCommandExpiresExactlyAtSignedTTLBoundary() {
        let state = makeState(command: .open(.init(url: "https://example.test")))
        let expiry = state.envelope.payload.expiresAtMilliseconds
        let model = makeModel()

        model.reconcileRemoteCommandStates([state], nowMilliseconds: expiry - 1)
        XCTAssertEqual(model.recentRemoteCommands.first?.status, .queued)
        XCTAssertFalse(model.recentRemoteCommands.first?.isExpired == true)

        model.reconcileRemoteCommandStates([state], nowMilliseconds: expiry)
        XCTAssertEqual(model.recentRemoteCommands.first?.status, .failed)
        XCTAssertEqual(model.recentRemoteCommands.first?.resultCode, "expired")
        XCTAssertTrue(model.recentRemoteCommands.first?.isExpired == true)

        model.reconcileRemoteCommandStates([state], nowMilliseconds: expiry + 1)
        XCTAssertTrue(model.recentRemoteCommands.first?.isExpired == true)
    }

    @MainActor
    func testTerminalTransportStatusIsNeverRewrittenAsLocalExpiry() {
        var executed = makeState(command: .focus(.init(
            tabID: TabID(),
            context: .normal
        )))
        executed.status = .executed
        executed.resultCode = "ok"
        let model = makeModel()

        model.reconcileRemoteCommandStates(
            [executed],
            nowMilliseconds: executed.envelope.payload.expiresAtMilliseconds + 1
        )

        XCTAssertEqual(model.recentRemoteCommands.first?.status, .executed)
        XCTAssertEqual(model.recentRemoteCommands.first?.resultCode, "ok")
        XCTAssertFalse(model.recentRemoteCommands.first?.isExpired == true)
    }

    @MainActor
    func testRestoredStatesNeedNoEphemeralLabelsAndKeepNewestFirst() {
        let open = makeState(
            issuedAtMilliseconds: 1_000,
            command: .open(.init(url: "https://example.test"))
        )
        var focus = makeState(
            issuedAtMilliseconds: 2_000,
            command: .focus(.init(tabID: TabID(), context: .normal))
        )
        focus.status = .executed
        var close = makeState(
            issuedAtMilliseconds: 3_000,
            command: .close([.init(tabID: TabID(), context: .normal)])
        )
        close.status = .failed
        close.resultCode = "rejected"
        let model = makeModel()
        XCTAssertTrue(model.commandLabels.isEmpty)

        model.reconcileRemoteCommandStates(
            [focus, open, close],
            nowMilliseconds: 4_000
        )

        XCTAssertEqual(model.recentRemoteCommands.map(\.id), [close.id, focus.id, open.id])
        XCTAssertEqual(model.recentRemoteCommands.map(\.action), [
            CompanionL10n.string("remote.action.close", fallback: "Close"),
            CompanionL10n.string("remote.action.focus", fallback: "Focus"),
            CompanionL10n.string("remote.action.open", fallback: "Open"),
        ])
        XCTAssertEqual(model.recentRemoteCommands.map(\.status), [
            .failed,
            .executed,
            .queued,
        ])
    }

    @MainActor
    func testOnlyNewestTwentyTerminalCommandsArePresented() {
        let states = (0..<24).map { index -> RemoteCommandState in
            var state = makeState(
                issuedAtMilliseconds: UInt64(index + 1),
                command: .open(.init(url: "https://example.test/\(index)"))
            )
            state.status = .executed
            return state
        }
        let model = makeModel()

        model.reconcileRemoteCommandStates(states, nowMilliseconds: 25)

        XCTAssertEqual(model.recentRemoteCommands.count, 20)
        XCTAssertEqual(
            model.recentRemoteCommands.map(\.id),
            states.suffix(20).reversed().map(\.id)
        )
    }

    @MainActor
    func testNewerTerminalHistoryCannotCrowdOutOlderActiveCommand() {
        let active = makeState(
            issuedAtMilliseconds: 1,
            command: .open(.init(url: "https://still-active.example"))
        )
        let terminal = (0..<20).map { index -> RemoteCommandState in
            var state = makeState(
                issuedAtMilliseconds: UInt64(index + 2),
                command: .open(.init(url: "https://done\(index).example"))
            )
            state.status = .executed
            return state
        }
        let model = makeModel()

        model.reconcileRemoteCommandStates(
            [active] + terminal,
            nowMilliseconds: 21
        )

        XCTAssertEqual(model.recentRemoteCommands.count, 21)
        XCTAssertTrue(model.recentRemoteCommands.contains { $0.id == active.id })
        XCTAssertEqual(model.recentRemoteCommands.filter(\.isTerminal).count, 20)
        XCTAssertNotNil(model.remoteCommandExpiryTask)
    }

    @MainActor
    func testEqualIssueTimesUseVersionThenCommandIDAsStableTieBreakers() throws {
        let olderVersion = makeState(
            commandID: try XCTUnwrap(UUID(
                uuidString: "10000000-0000-4000-8000-000000000001"
            )),
            issuedAtMilliseconds: 1_000,
            logicalCounter: 1,
            command: .open(.init(url: "https://older.example"))
        )
        let lowerID = makeState(
            commandID: try XCTUnwrap(UUID(
                uuidString: "20000000-0000-4000-8000-000000000001"
            )),
            issuedAtMilliseconds: 1_000,
            logicalCounter: 2,
            command: .open(.init(url: "https://lower-id.example"))
        )
        let higherID = makeState(
            commandID: try XCTUnwrap(UUID(
                uuidString: "F0000000-0000-4000-8000-000000000001"
            )),
            issuedAtMilliseconds: 1_000,
            logicalCounter: 2,
            command: .open(.init(url: "https://higher-id.example"))
        )
        let model = makeModel()

        model.reconcileRemoteCommandStates(
            [olderVersion, lowerID, higherID],
            nowMilliseconds: 1_001
        )

        XCTAssertEqual(
            model.recentRemoteCommands.map(\.id),
            [higherID.id, lowerID.id, olderVersion.id]
        )
    }

    @MainActor
    func testEmptyAuthoritativeStateClearsStalePresentation() {
        let model = makeModel()
        model.reconcileRemoteCommandStates([
            makeState(command: .open(.init(url: "https://example.test"))),
        ])
        XCTAssertNotNil(model.remoteCommandStatus)

        model.reconcileRemoteCommandStates([])

        XCTAssertTrue(model.recentRemoteCommands.isEmpty)
        XCTAssertNil(model.remoteCommandStatus)
        XCTAssertTrue(model.commandLabels.isEmpty)
    }

    func testReadModelRetentionKeepsLiveCommandsAndBoundedTerminalHistory() {
        let now: UInt64 = 1_000_000
        let active = (0..<3).map { index in
            makeState(
                issuedAtMilliseconds: now - UInt64(index * 1_000),
                command: .open(.init(url: "https://active\(index).example"))
            )
        }
        let terminal = (0..<120).map { index -> RemoteCommandState in
            var state = makeState(
                issuedAtMilliseconds: UInt64(index + 1),
                command: .open(.init(url: "https://terminal\(index).example"))
            )
            state.status = .executed
            return state
        }

        let first = CompanionRemoteCommandRetention.boundedStates(
            terminal + active,
            nowMilliseconds: now
        )
        let second = CompanionRemoteCommandRetention.boundedStates(
            first,
            nowMilliseconds: now
        )

        XCTAssertEqual(first.count, 23)
        XCTAssertTrue(Set(active.map(\.id)).isSubset(of: Set(first.map(\.id))))
        XCTAssertEqual(first, second, "Retention must be idempotent.")
        XCTAssertEqual(first.filter { $0.status == .executed }.count, 20)
    }

    func testHydrationScanIsBoundedAndFiltersBeforePayloadDecryption() {
        let source = DeviceID()
        let remoteRecords = (0..<150).map { index in
            SyncRecord(
                entityID: UUID(),
                dataClass: .remoteCommand,
                modifiedAt: .init(
                    physicalMilliseconds: UInt64(index),
                    nodeID: source
                ),
                originatingDevice: source,
                encryptedValue: .init(
                    keyVersion: 1,
                    nonce: Data(repeating: 1, count: 12),
                    ciphertextAndTag: Data(repeating: 2, count: 16)
                )
            )
        }
        let unrelated = SyncRecord(
            entityID: UUID(),
            dataClass: .workspace,
            modifiedAt: .init(physicalMilliseconds: 999, nodeID: source),
            originatingDevice: source,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 3, count: 12),
                ciphertextAndTag: Data(repeating: 4, count: 16)
            )
        )

        let selected = CompanionRemoteCommandRetention.newestTransportRecords(
            remoteRecords + [unrelated]
        )

        XCTAssertEqual(selected.count, 150)
        XCTAssertTrue(selected.allSatisfy { $0.dataClass == .remoteCommand })
        XCTAssertEqual(selected.first?.modifiedAt.physicalMilliseconds, 149)
        XCTAssertEqual(selected.last?.modifiedAt.physicalMilliseconds, 0)
    }

    func testHydrationDoesNotLetForeignOrCorruptHistoryStarveOwnedState() {
        let ownedSource = DeviceID()
        let foreignSource = DeviceID()
        let ownedState = makeState(
            issuedAtMilliseconds: 1,
            sourceDeviceID: ownedSource,
            command: .open(.init(url: "https://owned.example"))
        )
        let foreignStates = (0..<150).map { index in
            makeState(
                issuedAtMilliseconds: UInt64(index + 2),
                sourceDeviceID: foreignSource,
                command: .open(.init(url: "https://foreign\(index).example"))
            )
        }
        let states = [ownedState] + foreignStates
        let sourceByID = Dictionary(uniqueKeysWithValues: states.map { ($0.id, $0) })
        let records = states.map { state in
            SyncRecord(
                recordID: state.id,
                entityID: state.id,
                dataClass: .remoteCommand,
                modifiedAt: state.version.modifiedAt,
                originatingDevice: state.version.modifiedBy,
                encryptedValue: .init(
                    keyVersion: 1,
                    nonce: Data(repeating: 1, count: 12),
                    ciphertextAndTag: Data(repeating: 2, count: 16)
                )
            )
        }
        let corrupt = SyncRecord(
            entityID: UUID(),
            dataClass: .remoteCommand,
            modifiedAt: .init(physicalMilliseconds: 10_000, nodeID: foreignSource),
            originatingDevice: foreignSource,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 3, count: 12),
                ciphertextAndTag: Data(repeating: 4, count: 16)
            )
        )

        let outcome = CompanionRemoteCommandRetention.decodeOwnedStates(
            from: records + [corrupt],
            sourceDeviceID: ownedSource,
            decode: { record in
                guard let state = sourceByID[record.recordID] else {
                    throw URLError(.cannotDecodeContentData)
                }
                return state
            },
            validateOwned: { _ in }
        )

        XCTAssertEqual(outcome.ownedStates.map(\.id), [ownedState.id])
        XCTAssertEqual(outcome.rejectedRecords, [corrupt])
    }

    @MainActor
    func testNearestExpiryTaskTransitionsWithoutTransportEvent() async {
        let state = makeState(command: .open(.init(url: "https://expiry.example")))
        let time = TestTime(state.envelope.payload.expiresAtMilliseconds - 25)
        let model = CompanionAppModel(
            repository: LocalFirstRepository(store: InMemoryCompanionStore()),
            remoteCommandClock: { time.now() },
            remoteCommandSleeper: { delay in time.advance(by: delay) }
        )

        model.reconcileRemoteCommandStates([state])
        XCTAssertEqual(model.recentRemoteCommands.first?.status, .queued)

        for _ in 0..<20 where model.recentRemoteCommands.first?.isExpired != true {
            await Task.yield()
        }

        XCTAssertTrue(model.recentRemoteCommands.first?.isExpired == true)
        XCTAssertNil(model.remoteCommandExpiryTask)
    }

    @MainActor
    func testDeletedSigningIdentityImmediatelyClearsPresentationAndExpiryTask() {
        let state = makeState(command: .open(.init(url: "https://revoked.example")))
        let model = makeModel()
        model.reconcileRemoteCommandStates(
            [state],
            nowMilliseconds: state.envelope.payload.issuedAtMilliseconds
        )
        XCTAssertFalse(model.recentRemoteCommands.isEmpty)
        XCTAssertNotNil(model.remoteCommandExpiryTask)

        model.applyDeletedRemoteControlIdentityState()

        XCTAssertTrue(model.recentRemoteCommands.isEmpty)
        XCTAssertTrue(model.commandLabels.isEmpty)
        XCTAssertNil(model.remoteCommandExpiryTask)
        XCTAssertNil(model.remoteControlIdentity)
    }

    @MainActor
    func testPartialSigningIdentityMutationUsesSameFailClosedPresentationReset() {
        let state = makeState(command: .open(.init(url: "https://partial.example")))
        let model = makeModel()
        model.remoteControlIdentity = .init(
            sourceDeviceID: state.envelope.payload.sourceDeviceID,
            publicKey: Data(repeating: 3, count: 32)
        )
        model.reconcileRemoteCommandStates(
            [state],
            nowMilliseconds: state.envelope.payload.issuedAtMilliseconds
        )

        // Both delete and rotation catch paths call this helper because their
        // Keychain operations are marker-first and can fail after revocation.
        model.applyDeletedRemoteControlIdentityState()

        XCTAssertNil(model.remoteControlIdentity)
        XCTAssertTrue(model.recentRemoteCommands.isEmpty)
        XCTAssertTrue(model.commandLabels.isEmpty)
        XCTAssertNil(model.remoteCommandExpiryTask)
    }

    @MainActor
    private func makeModel() -> CompanionAppModel {
        CompanionAppModel(
            repository: LocalFirstRepository(store: InMemoryCompanionStore())
        )
    }

    private func makeState(
        commandID: UUID = UUID(),
        issuedAtMilliseconds: UInt64 = 1_000,
        logicalCounter: UInt32 = 0,
        sourceDeviceID: DeviceID? = nil,
        command: RemoteCommand
    ) -> RemoteCommandState {
        let source = sourceDeviceID ?? DeviceID()
        let payload = RemoteCommandPayload(
            commandID: commandID,
            sourceDeviceID: source,
            targetDeviceID: DeviceID(),
            nonce: Data(repeating: 7, count: 16),
            issuedAtMilliseconds: issuedAtMilliseconds,
            command: command
        )
        return RemoteCommandState(
            envelope: SignedRemoteCommand(
                payload: payload,
                signature: Data(repeating: 9, count: 64)
            ),
            version: SyncVersion(
                modifiedAt: HybridLogicalClock(
                    physicalMilliseconds: issuedAtMilliseconds,
                    logicalCounter: logicalCounter,
                    nodeID: source
                ),
                modifiedBy: source
            )
        )
    }
}
