import XCTest
import Synchronization
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionKeyLifecycleTests: XCTestCase {
    func testConcurrentFirstOptInCreatesOneClaimAndNoSplitKey() async throws {
        let server = FakeBootstrapServer(waitForInitialInspectors: 2)
        let firstStore = FakePayloadKeyStore(generatedByte: 0x11)
        let secondStore = FakePayloadKeyStore(generatedByte: 0x22)
        let first = CompanionKeyLifecycleCoordinator(
            transport: FakeBootstrapTransport(server: server),
            keyStore: firstStore,
            generator: { Data(repeating: 0x11, count: 32) }
        )
        let second = CompanionKeyLifecycleCoordinator(
            transport: FakeBootstrapTransport(server: server),
            keyStore: secondStore,
            generator: { Data(repeating: 0x22, count: 32) }
        )

        async let firstResult = first.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )
        async let secondResult = second.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )
        let results = try await [firstResult, secondResult]

        XCTAssertEqual(results.filter(\.permitsEncryptedDomainRecords).count, 1)
        XCTAssertEqual(results.filter { status in
            if case .waiting(_, .anotherDeviceWonBootstrap) = status { return true }
            return false
        }.count, 1)
        let serverCreates = await server.claimCreateCount()
        let firstGenerations = await firstStore.generatorCalls()
        let secondGenerations = await secondStore.generatorCalls()
        let firstCanonicalCount = await firstStore.canonicalKeyCount()
        let secondCanonicalCount = await secondStore.canonicalKeyCount()
        let canonicalCount = firstCanonicalCount + secondCanonicalCount
        let firstDiscardedCount = await firstStore.generatedDiscardCount()
        let secondDiscardedCount = await secondStore.generatedDiscardCount()
        let discardedCount = firstDiscardedCount + secondDiscardedCount
        XCTAssertEqual(serverCreates, 1)
        XCTAssertEqual(firstGenerations, 1)
        XCTAssertEqual(secondGenerations, 1)
        XCTAssertEqual(
            canonicalCount,
            1
        )
        XCTAssertEqual(
            discardedCount,
            1
        )
    }

    func testAcceptedReceiptRecoversCrashWithoutGeneratingAnotherKey() async throws {
        let claim = CompanionBootstrapClaim(keyVersion: 1, serverChangeTag: "server-1")
        let receipt = CompanionBootstrapClaimReceipt(
            keyVersion: 1,
            serverChangeTag: "server-1"
        )
        let server = FakeBootstrapServer(claim: claim)
        let store = FakePayloadKeyStore(
            generatedByte: 0x42,
            pending: .init(
                keyVersion: 1,
                origin: .generated,
                acceptedReceipt: receipt
            )
        )
        let coordinator = makeCoordinator(server: server, store: store)

        let status = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )

        XCTAssertEqual(status, .ready(keyVersion: 1))
        let generations = await store.generatorCalls()
        let canonicalCount = await store.canonicalKeyCount()
        let pending = await store.pendingState(version: 1)
        XCTAssertEqual(generations, 0)
        XCTAssertEqual(canonicalCount, 1)
        XCTAssertNil(pending)
    }

    func testUnverifiedCrashCandidateStaysRecoveryAndIsNotPublished() async throws {
        let server = FakeBootstrapServer(
            claim: .init(keyVersion: 1, serverChangeTag: "other-device")
        )
        let store = FakePayloadKeyStore(
            generatedByte: 0x42,
            pending: .init(keyVersion: 1, origin: .generated)
        )
        let coordinator = makeCoordinator(server: server, store: store)

        let status = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )

        XCTAssertEqual(
            status,
            .recovery(reason: .bootstrapOwnershipUnverified, keyVersion: 1)
        )
        let canonicalCount = await store.canonicalKeyCount()
        let pending = await store.pendingState(version: 1)
        XCTAssertEqual(canonicalCount, 0)
        XCTAssertNotNil(pending)
    }

    func testRemoteEncryptedDataWithoutClaimNeverGeneratesOrOverwrites() async throws {
        let server = FakeBootstrapServer(hasEncryptedDomainRecords: true)
        let store = FakePayloadKeyStore(generatedByte: 0x42)
        let coordinator = makeCoordinator(server: server, store: store)

        let status = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )

        XCTAssertEqual(
            status,
            .recovery(reason: .remoteDataWithoutKey, keyVersion: 1)
        )
        let generations = await store.generatorCalls()
        let serverCreates = await server.claimCreateCount()
        XCTAssertEqual(generations, 0)
        XCTAssertEqual(serverCreates, 0)
    }

    func testExistingProvisionedKeyMigratesIdempotentlyWithoutRotation() async throws {
        let server = FakeBootstrapServer()
        let store = FakePayloadKeyStore(
            generatedByte: 0x99,
            canonical: [1: Data(repeating: 0x33, count: 32)]
        )
        let coordinator = makeCoordinator(server: server, store: store)

        let first = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )
        let second = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )

        XCTAssertEqual(first, .ready(keyVersion: 1))
        XCTAssertEqual(second, .ready(keyVersion: 1))
        let generations = await store.generatorCalls()
        let serverCreates = await server.claimCreateCount()
        let canonical = await store.canonicalKey(version: 1)
        XCTAssertEqual(generations, 0)
        XCTAssertEqual(serverCreates, 1)
        XCTAssertEqual(canonical, Data(repeating: 0x33, count: 32))
    }

    func testRemoteClaimWaitsForSynchronizablePairingKey() async throws {
        let server = FakeBootstrapServer(
            claim: .init(keyVersion: 3, serverChangeTag: "server-3")
        )
        let store = FakePayloadKeyStore(generatedByte: 0x42)
        let coordinator = makeCoordinator(server: server, store: store)

        let status = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 3
        )

        XCTAssertEqual(
            status,
            .waiting(keyVersion: 3, reason: .synchronizableKeyPending)
        )
        let generations = await store.generatorCalls()
        XCTAssertEqual(generations, 0)
    }

    func testMismatchedLocalVersionRequiresRecovery() async throws {
        let server = FakeBootstrapServer(
            claim: .init(keyVersion: 1, serverChangeTag: "server-1")
        )
        let store = FakePayloadKeyStore(
            generatedByte: 0x42,
            canonical: [2: Data(repeating: 0x22, count: 32)]
        )
        let coordinator = makeCoordinator(server: server, store: store)

        let status = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )

        XCTAssertEqual(
            status,
            .recovery(reason: .keyVersionMismatch, keyVersion: 1)
        )
    }

    func testSplitKeyReadbackFailsClosedToRecovery() async throws {
        let claim = CompanionBootstrapClaim(keyVersion: 1, serverChangeTag: "server-1")
        let store = FakePayloadKeyStore(
            generatedByte: 0x11,
            canonical: [1: Data(repeating: 0x22, count: 32)],
            pending: .init(
                keyVersion: 1,
                origin: .generated,
                acceptedReceipt: .init(keyVersion: 1, serverChangeTag: "server-1")
            )
        )
        let coordinator = makeCoordinator(
            server: FakeBootstrapServer(claim: claim),
            store: store
        )

        let status = try await coordinator.activate(
            explicitOptIn: true,
            desiredKeyVersion: 1
        )

        XCTAssertEqual(
            status,
            .recovery(reason: .bootstrapOwnershipUnverified, keyVersion: 1)
        )
        let canonical = await store.canonicalKey(version: 1)
        XCTAssertEqual(canonical, Data(repeating: 0x22, count: 32))
    }

    func testRotationRevocationAndRecoveryStatusesNeverPermitWrites() async {
        let coordinator = makeCoordinator(
            server: FakeBootstrapServer(),
            store: FakePayloadKeyStore(generatedByte: 0x42)
        )
        let transition = Date(timeIntervalSince1970: 2_000)

        let rotating = await coordinator.requireRotation(
            current: 1,
            next: 2,
            transitionEndsAt: transition
        )
        let revoked = await coordinator.markRevoked(keyVersion: 1)
        let recovery = await coordinator.requireRecovery(
            reason: .revokedKey,
            keyVersion: 1
        )

        XCTAssertEqual(
            rotating,
            .rotation(current: 1, next: 2, transitionEndsAt: transition)
        )
        XCTAssertEqual(revoked, .revoked(keyVersion: 1))
        XCTAssertEqual(recovery, .recovery(reason: .revokedKey, keyVersion: 1))
        XCTAssertFalse(rotating.permitsEncryptedDomainRecords)
        XCTAssertFalse(revoked.permitsEncryptedDomainRecords)
        XCTAssertFalse(recovery.permitsEncryptedDomainRecords)
    }

    func testSealerReadsOldVersionOnlyDuringExplicitRotationWindow() throws {
        let primary = CompanionSyncKeyConfiguration(
            service: "fixture", account: "v2", keyVersion: 2
        )
        let previous = CompanionSyncKeyConfiguration(
            service: "fixture", account: "v1", keyVersion: 1
        )
        let oldSealer = KeychainCompanionPayloadSealer(
            configuration: previous,
            keyLoader: { Data(repeating: 0x11, count: 32) }
        )
        let rotating = KeychainCompanionPayloadSealer(
            configuration: primary,
            acceptedPreviousConfigurations: [previous]
        ) { configuration in
            Data(
                repeating: configuration.keyVersion == 1 ? 0x11 : 0x22,
                count: 32
            )
        }
        let afterRevocation = KeychainCompanionPayloadSealer(
            configuration: primary,
            keyLoader: { Data(repeating: 0x22, count: 32) }
        )
        let envelope = try oldSealer.seal(Data("old payload".utf8))

        XCTAssertEqual(try rotating.open(envelope), Data("old payload".utf8))
        XCTAssertThrowsError(try afterRevocation.open(envelope)) { error in
            XCTAssertEqual(error as? CompanionSyncKeyError, .unsupportedKeyVersion(1))
        }
    }

    func testDeviceSignerCreatesOnePrivateKeyAndPublishesOnlyPublicIdentity() throws {
#if canImport(CryptoKit) && canImport(Security)
        importCryptoKitMarker()
        let state = Mutex<(key: Data?, creates: Int)>((nil, 0))
        let fixtureKey = Curve25519.Signing.PrivateKey()
        let signer = KeychainRemoteCommandSigner(
            configuration: .init(
                service: "fixture",
                account: "device",
                sourceDeviceID: DeviceID()
            ),
            keyLoader: {
                try state.withLock { value in
                    guard let key = value.key else {
                        throw RemoteCommandSignerError.keychainStatus(errSecItemNotFound)
                    }
                    return key
                }
            },
            keyCreator: {
                state.withLock { value in
                    value.creates += 1
                    value.key = fixtureKey.rawRepresentation
                    return fixtureKey.rawRepresentation
                }
            },
            nonceLoader: { Data(repeating: 0x77, count: 32) }
        )

        let first = try signer.ensureIdentity()
        let second = try signer.ensureIdentity()

        XCTAssertEqual(first, second)
        XCTAssertEqual(first.publicKeyBase64, fixtureKey.publicKey.rawRepresentation.base64EncodedString())
        XCTAssertEqual(state.withLock { $0.creates }, 1)
#else
        throw XCTSkip("CryptoKit/Security unavailable")
#endif
    }

    private func makeCoordinator(
        server: FakeBootstrapServer,
        store: FakePayloadKeyStore
    ) -> CompanionKeyLifecycleCoordinator {
        CompanionKeyLifecycleCoordinator(
            transport: FakeBootstrapTransport(server: server),
            keyStore: store,
            generator: { Data(repeating: 0x42, count: 32) }
        )
    }
}

#if canImport(CryptoKit) && canImport(Security)
import CryptoKit
import Security
private func importCryptoKitMarker() {}
#endif

private actor FakeBootstrapServer {
    private var zoneExists: Bool
    private var claim: CompanionBootstrapClaim?
    private var hasEncryptedDomainRecords: Bool
    private var createCount = 0
    private let waitForInitialInspectors: Int
    private var initialInspectCount = 0
    private var initialInspectWaiters: [CheckedContinuation<Void, Never>] = []

    init(
        zoneExists: Bool = true,
        claim: CompanionBootstrapClaim? = nil,
        hasEncryptedDomainRecords: Bool = false,
        waitForInitialInspectors: Int = 0
    ) {
        self.zoneExists = zoneExists
        self.claim = claim
        self.hasEncryptedDomainRecords = hasEncryptedDomainRecords
        self.waitForInitialInspectors = waitForInitialInspectors
    }

    func inspect() async -> CompanionBootstrapRemoteSnapshot {
        let snapshot = CompanionBootstrapRemoteSnapshot(
            zoneExists: zoneExists,
            claim: claim,
            hasEncryptedDomainRecords: hasEncryptedDomainRecords
        )
        if initialInspectCount < waitForInitialInspectors {
            initialInspectCount += 1
            if initialInspectCount == waitForInitialInspectors {
                let waiters = initialInspectWaiters
                initialInspectWaiters.removeAll()
                waiters.forEach { $0.resume() }
            } else {
                await withCheckedContinuation { continuation in
                    initialInspectWaiters.append(continuation)
                }
            }
        }
        return snapshot
    }

    func ensureZone() { zoneExists = true }

    func create(
        version: UInt32,
        accepted: @escaping @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    ) async throws -> CompanionBootstrapClaimResult {
        if let claim { return .existing(claim) }
        createCount += 1
        let created = CompanionBootstrapClaim(
            keyVersion: version,
            serverChangeTag: "server-\(createCount)"
        )
        claim = created
        let receipt = CompanionBootstrapClaimReceipt(
            keyVersion: version,
            serverChangeTag: created.serverChangeTag
        )
        try await accepted(receipt)
        return .created(receipt)
    }

    func claimCreateCount() -> Int { createCount }
}

private actor FakeBootstrapTransport: CompanionKeyBootstrapTransport {
    private let server: FakeBootstrapServer

    init(server: FakeBootstrapServer) { self.server = server }

    func inspectRemote() async throws -> CompanionBootstrapRemoteSnapshot {
        await server.inspect()
    }

    func ensureZone() async throws { await server.ensureZone() }

    func createClaim(
        keyVersion: UInt32,
        accepted: @escaping @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    ) async throws -> CompanionBootstrapClaimResult {
        try await server.create(version: keyVersion, accepted: accepted)
    }

    func shutdown() async {}
}

private actor FakePayloadKeyStore: CompanionPayloadKeyLifecycleStoring {
    private let generatedByte: UInt8
    private var canonical: [UInt32: Data]
    private var pending: [UInt32: (state: CompanionPendingKeyState, key: Data)] = [:]
    private var generationCount = 0
    private var discardCount = 0

    init(
        generatedByte: UInt8,
        canonical: [UInt32: Data] = [:],
        pending initialPending: CompanionPendingKeyState? = nil
    ) {
        self.generatedByte = generatedByte
        self.canonical = canonical
        if let initialPending {
            self.pending[initialPending.keyVersion] = (
                initialPending,
                Data(repeating: generatedByte, count: 32)
            )
        }
    }

    func hasCanonicalKey(version: UInt32) -> Bool { canonical[version] != nil }
    func knownCanonicalVersions() -> Set<UInt32> { Set(canonical.keys) }
    func pendingState(version: UInt32) -> CompanionPendingKeyState? {
        pending[version]?.state
    }

    func prepareCandidate(
        version: UInt32,
        generator: @escaping @Sendable () throws -> Data
    ) throws -> CompanionPendingKeyState {
        if let pending = pending[version]?.state { return pending }
        if canonical[version] != nil {
            let state = CompanionPendingKeyState(
                keyVersion: version,
                origin: .externallyProvisioned
            )
            pending[version] = (state, Data())
            return state
        }
        let generated = try generator()
        generationCount += 1
        let key = generated.isEmpty ? Data(repeating: generatedByte, count: 32) : generated
        guard key.count == 32 else {
            throw CompanionKeyLifecycleError.invalidGeneratedKeyLength
        }
        let state = CompanionPendingKeyState(keyVersion: version, origin: .generated)
        pending[version] = (state, key)
        return state
    }

    func markClaimAccepted(_ receipt: CompanionBootstrapClaimReceipt) throws {
        guard let existing = pending[receipt.keyVersion] else {
            throw CompanionKeyLifecycleError.candidateMissing
        }
        let state = CompanionPendingKeyState(
            keyVersion: receipt.keyVersion,
            origin: existing.state.origin,
            acceptedReceipt: receipt
        )
        pending[receipt.keyVersion] = (state, existing.key)
    }

    func promoteAcceptedCandidate(
        version: UInt32,
        matching claim: CompanionBootstrapClaim
    ) throws {
        guard let candidate = pending[version],
              candidate.state.acceptedReceipt?.matches(claim) == true else {
            throw CompanionKeyLifecycleError.acceptedReceiptMissing
        }
        if candidate.state.origin == .generated {
            if let existing = canonical[version], existing != candidate.key {
                throw CompanionKeyLifecycleError.splitKeyPrevented
            }
            canonical[version] = candidate.key
        }
        pending[version] = nil
    }

    func discardGeneratedCandidate(version: UInt32) {
        if pending[version]?.state.origin == .generated { discardCount += 1 }
        pending[version] = nil
    }

    func generatorCalls() -> Int { generationCount }
    func generatedDiscardCount() -> Int { discardCount }
    func canonicalKeyCount() -> Int { canonical.count }
    func canonicalKey(version: UInt32) -> Data? { canonical[version] }
}
