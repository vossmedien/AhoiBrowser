import XCTest
import Synchronization
@testable import AhoiMobileCore
import AhoiCloudKitSpike

#if canImport(CryptoKit) && canImport(Security)
import CryptoKit
import Security
#endif

final class CompanionDeviceRevocationTests: XCTestCase {
    func testDeviceRevocationPersistsTombstoneMetadataAndThirtyDayRetention() async throws {
        let author = deviceID("71000000-0000-4000-8000-000000000001")
        let target = deviceID("72000000-0000-4000-8000-000000000002")
        let store = InMemoryCompanionStore()
        let repository = LocalFirstRepository(store: store, localDeviceID: author)
        let original = makeDevice(
            id: target,
            name: "Fixture Mac",
            kind: .mac,
            author: target,
            milliseconds: 1_000
        )
        try await repository.upsert(original)

        let result = try await repository.revokeAndRemoveDevice(
            target,
            revokedBy: author,
            atMilliseconds: 2_000
        )
        let restarted = LocalFirstRepository(store: store, localDeviceID: author)
        let restartedSnapshot = try await restarted.currentSnapshot()
        let persisted = try XCTUnwrap(
            restartedSnapshot.devices.first { $0.id == target }
        )
        let tombstone = try XCTUnwrap(persisted.tombstone)

        XCTAssertTrue(result.didMutate)
        XCTAssertEqual(result.device, persisted)
        XCTAssertTrue(persisted.isDeleted)
        XCTAssertTrue(persisted.isRevoked)
        XCTAssertFalse(persisted.isOnline)
        XCTAssertEqual(persisted.name, original.name)
        XCTAssertEqual(persisted.kind, original.kind)
        XCTAssertEqual(persisted.createdAt, original.createdAt)
        XCTAssertEqual(persisted.lastSeenAt, original.lastSeenAt)
        XCTAssertEqual(tombstone.entityID, target.rawValue)
        XCTAssertEqual(tombstone.deletedBy, author)
        XCTAssertNil(tombstone.originalParentID)
        XCTAssertNil(tombstone.originalOrderKey)
        XCTAssertEqual(tombstone.deletedAt, persisted.version.modifiedAt)
        XCTAssertEqual(persisted.version.modifiedBy, author)
        XCTAssertEqual(persisted.version.fieldVersions["retired"], tombstone.deletedAt)
        XCTAssertEqual(persisted.version.fieldVersions["tombstone"], tombstone.deletedAt)
        XCTAssertEqual(
            tombstone.purgeAfterMilliseconds - tombstone.deletedAt.physicalMilliseconds,
            30 * 24 * 60 * 60 * 1_000
        )
    }

    func testRepeatedDeviceRevocationIsByteIdenticalNoOp() async throws {
        let author = deviceID("71000000-0000-4000-8000-000000000001")
        let target = deviceID("72000000-0000-4000-8000-000000000002")
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: author
        )
        try await repository.upsert(makeDevice(
            id: target,
            name: "Fixture Mac",
            kind: .mac,
            author: target,
            milliseconds: 1_000
        ))

        let first = try await repository.revokeAndRemoveDevice(
            target,
            revokedBy: author,
            atMilliseconds: 2_000
        )
        let second = try await repository.revokeAndRemoveDevice(
            target,
            revokedBy: author,
            atMilliseconds: 9_000
        )

        XCTAssertTrue(first.didMutate)
        XCTAssertFalse(second.didMutate)
        XCTAssertEqual(second.device, first.device)
        XCTAssertEqual(try canonicalJSON(second.device), try canonicalJSON(first.device))
    }

    @MainActor
    func testRemoteTargetGuardRejectsRevokedDeletedAndUnknownDevices() {
        let author = deviceID("71000000-0000-4000-8000-000000000001")
        let activeID = deviceID("72000000-0000-4000-8000-000000000002")
        let revokedID = deviceID("73000000-0000-4000-8000-000000000003")
        let deletedID = deviceID("74000000-0000-4000-8000-000000000004")
        let deletedClock = HybridLogicalClock(
            physicalMilliseconds: 2_000,
            nodeID: author
        )
        var revoked = makeDevice(
            id: revokedID,
            name: "Revoked Mac",
            kind: .mac,
            author: author,
            milliseconds: 1_000
        )
        revoked.isRevoked = true
        var deleted = makeDevice(
            id: deletedID,
            name: "Deleted Mac",
            kind: .mac,
            author: author,
            milliseconds: 1_000
        )
        deleted.tombstone = Tombstone(
            entityID: deletedID.rawValue,
            deletedAt: deletedClock,
            deletedBy: author,
            originalParentID: nil,
            originalOrderKey: nil,
            purgeAfterMilliseconds: 2_592_002_000
        )
        let model = CompanionAppModel(
            repository: LocalFirstRepository(store: InMemoryCompanionStore())
        )
        model.snapshot = CompanionSnapshot(devices: [
            makeDevice(
                id: activeID,
                name: "Active Mac",
                kind: .mac,
                author: author,
                milliseconds: 1_000
            ),
            revoked,
            deleted,
        ])

        XCTAssertTrue(model.canTargetRemoteCommand(activeID))
        XCTAssertFalse(model.canTargetRemoteCommand(revokedID))
        XCTAssertFalse(model.canTargetRemoteCommand(deletedID))
        XCTAssertFalse(model.canTargetRemoteCommand(DeviceID()))
    }

    func testSelfRevocationSurvivesRestartAndSignerStaysFailClosed() throws {
#if canImport(CryptoKit) && canImport(Security)
        let source = deviceID("71000000-0000-4000-8000-000000000001")
        let fixture = RestartableRemoteSignerFixture(sourceDeviceID: source)
        let signer = fixture.makeSigner()
        let activeIdentity = try signer.provisioningIdentity()

        let archivedIdentity = try signer.deleteIdentity()
        let restarted = fixture.makeSigner()

        XCTAssertEqual(archivedIdentity, activeIdentity)
        XCTAssertFalse(fixture.hasPrivateKey)
        XCTAssertTrue(try restarted.identityIsRevoked())
        XCTAssertThrowsError(try restarted.provisioningIdentity()) { error in
            XCTAssertEqual(error as? RemoteCommandSignerError, .identityRevoked)
        }
        XCTAssertThrowsError(try restarted.ensureIdentity()) { error in
            XCTAssertEqual(error as? RemoteCommandSignerError, .identityRevoked)
        }
        XCTAssertThrowsError(try restarted.sign(makePayload(source: source))) { error in
            XCTAssertEqual(error as? RemoteCommandSignerError, .identityRevoked)
        }
        XCTAssertEqual(fixture.keyCreationCount, 0)
#else
        throw XCTSkip("CryptoKit/Security unavailable")
#endif
    }

    func testExplicitRotationReEnrolsWithNewIdentityAfterRevocation() throws {
#if canImport(CryptoKit) && canImport(Security)
        let source = deviceID("71000000-0000-4000-8000-000000000001")
        let fixture = RestartableRemoteSignerFixture(sourceDeviceID: source)
        let original = fixture.makeSigner()
        let originalIdentity = try original.provisioningIdentity()
        _ = try original.deleteIdentity()

        let revokedRestart = fixture.makeSigner()
        XCTAssertTrue(try revokedRestart.identityIsRevoked())
        let rotatedIdentity = try revokedRestart.rotateIdentity()
        let reEnrolledRestart = fixture.makeSigner()

        XCTAssertNotEqual(rotatedIdentity.publicKeyBase64, originalIdentity.publicKeyBase64)
        XCTAssertNotEqual(rotatedIdentity.fingerprint, originalIdentity.fingerprint)
        XCTAssertTrue(fixture.hasPrivateKey)
        XCTAssertEqual(fixture.rotationCount, 1)
        XCTAssertFalse(try reEnrolledRestart.identityIsRevoked())
        XCTAssertEqual(try reEnrolledRestart.ensureIdentity(), rotatedIdentity)
        XCTAssertNoThrow(try reEnrolledRestart.sign(makePayload(source: source)))
#else
        throw XCTSkip("CryptoKit/Security unavailable")
#endif
    }

    private func makeDevice(
        id: DeviceID,
        name: String,
        kind: DeviceKind,
        author: DeviceID,
        milliseconds: UInt64
    ) -> Device {
        let clock = HybridLogicalClock(
            physicalMilliseconds: milliseconds,
            nodeID: author
        )
        return Device(
            deviceID: id,
            name: name,
            kind: kind,
            createdAt: clock,
            lastSeenAt: clock,
            isOnline: true,
            version: SyncVersion(
                modifiedAt: clock,
                modifiedBy: author,
                fieldVersions: [
                    "type": clock,
                    "display_name": clock,
                    "created_at": clock,
                    "last_seen": clock,
                    "retired": clock,
                    "tombstone": clock,
                ]
            )
        )
    }

    private func canonicalJSON(_ device: Device) throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        return try encoder.encode(device)
    }

    private func deviceID(_ rawValue: String) -> DeviceID {
        DeviceID(rawValue: UUID(uuidString: rawValue)!)
    }

#if canImport(CryptoKit) && canImport(Security)
    private func makePayload(source: DeviceID) -> RemoteCommandPayload {
        RemoteCommandPayload(
            commandID: UUID(uuidString: "75000000-0000-4000-8000-000000000005")!,
            sourceDeviceID: source,
            targetDeviceID: deviceID("72000000-0000-4000-8000-000000000002"),
            nonce: Data(repeating: 0x75, count: 32),
            issuedAtMilliseconds: 2_000,
            command: .open(.init(url: "https://fixture.ahoibrowser.test"))
        )
    }
#endif
}

#if canImport(CryptoKit) && canImport(Security)
private final class RestartableRemoteSignerFixture: @unchecked Sendable {
    private struct State: Sendable {
        var privateKey: Data?
        var revokedPublicKey: Data?
        var keyCreationCount = 0
        var rotationCount = 0
    }

    private let sourceDeviceID: DeviceID
    private let replacementKey: Data
    private let state: Mutex<State>

    init(sourceDeviceID: DeviceID) {
        self.sourceDeviceID = sourceDeviceID
        self.replacementKey = Data(repeating: 0x22, count: 32)
        self.state = Mutex(State(
            privateKey: Data(repeating: 0x11, count: 32),
            revokedPublicKey: nil
        ))
    }

    var hasPrivateKey: Bool {
        state.withLock { $0.privateKey != nil }
    }

    var keyCreationCount: Int {
        state.withLock { $0.keyCreationCount }
    }

    var rotationCount: Int {
        state.withLock { $0.rotationCount }
    }

    func makeSigner() -> KeychainRemoteCommandSigner {
        return KeychainRemoteCommandSigner(
            configuration: .init(
                service: "fixture.invalid",
                account: "remote-command-device",
                sourceDeviceID: sourceDeviceID
            ),
            keyLoader: { [self] in
                try state.withLock { value in
                    guard let privateKey = value.privateKey else {
                        throw RemoteCommandSignerError.keychainStatus(errSecItemNotFound)
                    }
                    return privateKey
                }
            },
            keyCreator: { [self] in
                state.withLock { value in
                    value.keyCreationCount += 1
                    value.privateKey = replacementKey
                    return replacementKey
                }
            },
            revocationLoader: { [self] in
                state.withLock { $0.revokedPublicKey }
            },
            keyRevoker: { [self] in
                try state.withLock { value in
                    if let archived = value.revokedPublicKey {
                        value.privateKey = nil
                        return archived
                    }
                    guard let privateKey = value.privateKey else {
                        throw RemoteCommandSignerError.invalidPrivateKey
                    }
                    let publicKey = try Curve25519.Signing.PrivateKey(
                        rawRepresentation: privateKey
                    ).publicKey.rawRepresentation
                    value.revokedPublicKey = publicKey
                    value.privateKey = nil
                    return publicKey
                }
            },
            keyRotator: { [self] in
                state.withLock { value in
                    value.rotationCount += 1
                    value.privateKey = replacementKey
                    value.revokedPublicKey = nil
                    return replacementKey
                }
            },
            nonceLoader: { Data(repeating: 0x75, count: 32) }
        )
    }
}
#endif
