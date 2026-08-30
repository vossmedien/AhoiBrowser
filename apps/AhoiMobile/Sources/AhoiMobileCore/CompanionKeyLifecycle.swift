import Foundation

public enum CompanionKeyWaitingReason: String, Codable, Equatable, Sendable {
    case pairingKeyPending
    case anotherDeviceWonBootstrap
    case synchronizableKeyPending
}

public enum CompanionKeyRecoveryReason: String, Codable, Equatable, Sendable {
    case remoteDataWithoutKey
    case bootstrapOwnershipUnverified
    case keyVersionMismatch
    case keychainFailure
    case accountChanged
    case zoneLost
    case revokedKey
}

public enum CompanionKeyLifecycleStatus: Equatable, Sendable {
    case disabled
    case waiting(keyVersion: UInt32, reason: CompanionKeyWaitingReason)
    case migration(keyVersion: UInt32)
    case claiming(keyVersion: UInt32)
    case ready(keyVersion: UInt32)
    case rotation(current: UInt32, next: UInt32, transitionEndsAt: Date)
    case revoked(keyVersion: UInt32)
    case recovery(reason: CompanionKeyRecoveryReason, keyVersion: UInt32?)

    public var permitsEncryptedDomainRecords: Bool {
        if case .ready = self { return true }
        return false
    }

    public var localizedSummary: String {
        switch self {
        case .disabled:
            CompanionL10n.string("sync.key.disabled", fallback: "Sync keys are off")
        case .waiting:
            CompanionL10n.string(
                "sync.key.waiting",
                fallback: "Waiting for the paired sync key"
            )
        case .migration:
            CompanionL10n.string(
                "sync.key.migration",
                fallback: "Verifying the existing sync key"
            )
        case .claiming:
            CompanionL10n.string(
                "sync.key.claiming",
                fallback: "Securing first-time sync setup"
            )
        case .ready:
            CompanionL10n.string("sync.key.ready", fallback: "Encryption ready")
        case .rotation:
            CompanionL10n.string(
                "sync.key.rotation",
                fallback: "Encryption key rotation required"
            )
        case .revoked:
            CompanionL10n.string(
                "sync.key.revoked",
                fallback: "Encryption key revoked"
            )
        case .recovery:
            CompanionL10n.string(
                "sync.key.recovery",
                fallback: "Encryption recovery required"
            )
        }
    }
}

public struct CompanionBootstrapClaim: Equatable, Sendable {
    public let keyVersion: UInt32
    public let serverChangeTag: String

    public init(keyVersion: UInt32, serverChangeTag: String) {
        self.keyVersion = keyVersion
        self.serverChangeTag = serverChangeTag
    }
}

public struct CompanionBootstrapRemoteSnapshot: Equatable, Sendable {
    public let zoneExists: Bool
    public let claim: CompanionBootstrapClaim?
    public let hasEncryptedDomainRecords: Bool

    public init(
        zoneExists: Bool,
        claim: CompanionBootstrapClaim?,
        hasEncryptedDomainRecords: Bool
    ) {
        self.zoneExists = zoneExists
        self.claim = claim
        self.hasEncryptedDomainRecords = hasEncryptedDomainRecords
    }
}

public struct CompanionBootstrapClaimReceipt: Codable, Equatable, Sendable {
    public let keyVersion: UInt32
    public let serverChangeTag: String

    public init(keyVersion: UInt32, serverChangeTag: String) {
        self.keyVersion = keyVersion
        self.serverChangeTag = serverChangeTag
    }

    public func matches(_ claim: CompanionBootstrapClaim) -> Bool {
        keyVersion == claim.keyVersion && serverChangeTag == claim.serverChangeTag
    }
}

public enum CompanionBootstrapClaimResult: Equatable, Sendable {
    case created(CompanionBootstrapClaimReceipt)
    case existing(CompanionBootstrapClaim)
    case indeterminate
}

public protocol CompanionKeyBootstrapTransport: Sendable {
    func inspectRemote() async throws -> CompanionBootstrapRemoteSnapshot
    func ensureZone() async throws
    func createClaim(
        keyVersion: UInt32,
        accepted: @escaping @Sendable (CompanionBootstrapClaimReceipt) async throws -> Void
    ) async throws -> CompanionBootstrapClaimResult
    func shutdown() async
}

public enum CompanionPendingKeyOrigin: String, Codable, Equatable, Sendable {
    case generated
    case externallyProvisioned
}

public struct CompanionPendingKeyState: Equatable, Sendable {
    public let keyVersion: UInt32
    public let origin: CompanionPendingKeyOrigin
    public let acceptedReceipt: CompanionBootstrapClaimReceipt?

    public init(
        keyVersion: UInt32,
        origin: CompanionPendingKeyOrigin,
        acceptedReceipt: CompanionBootstrapClaimReceipt? = nil
    ) {
        self.keyVersion = keyVersion
        self.origin = origin
        self.acceptedReceipt = acceptedReceipt
    }
}

public protocol CompanionPayloadKeyLifecycleStoring: Sendable {
    func hasCanonicalKey(version: UInt32) async throws -> Bool
    func knownCanonicalVersions() async throws -> Set<UInt32>
    func pendingState(version: UInt32) async throws -> CompanionPendingKeyState?
    func prepareCandidate(
        version: UInt32,
        generator: @escaping @Sendable () throws -> Data
    ) async throws -> CompanionPendingKeyState
    func markClaimAccepted(_ receipt: CompanionBootstrapClaimReceipt) async throws
    func promoteAcceptedCandidate(
        version: UInt32,
        matching claim: CompanionBootstrapClaim
    ) async throws
    func discardGeneratedCandidate(version: UInt32) async throws
}

public enum CompanionKeyLifecycleError: Error, Equatable, Sendable {
    case explicitOptInRequired
    case invalidKeyVersion
    case invalidGeneratedKeyLength
    case remoteClaimChanged
    case candidateMissing
    case acceptedReceiptMissing
    case splitKeyPrevented
    case bootstrapTransportUnavailable
}

public struct CompanionSyncRuntimeActivation {
    public let status: CompanionKeyLifecycleStatus
    public let runtime: CompanionCloudKitRuntime?

    public init(
        status: CompanionKeyLifecycleStatus,
        runtime: CompanionCloudKitRuntime?
    ) {
        self.status = status
        self.runtime = runtime
    }
}

public typealias CompanionSyncRuntimeFactory = @MainActor @Sendable () async throws
    -> CompanionSyncRuntimeActivation

public actor CompanionKeyLifecycleCoordinator {
    private let transport: any CompanionKeyBootstrapTransport
    private let keyStore: any CompanionPayloadKeyLifecycleStoring
    private let generator: @Sendable () throws -> Data
    private var status: CompanionKeyLifecycleStatus = .disabled

    public init(
        transport: any CompanionKeyBootstrapTransport,
        keyStore: any CompanionPayloadKeyLifecycleStoring,
        generator: @escaping @Sendable () throws -> Data
    ) {
        self.transport = transport
        self.keyStore = keyStore
        self.generator = generator
    }

    public func currentStatus() -> CompanionKeyLifecycleStatus {
        status
    }

    public func activate(
        explicitOptIn: Bool,
        desiredKeyVersion: UInt32
    ) async throws -> CompanionKeyLifecycleStatus {
        guard explicitOptIn else {
            status = .disabled
            return status
        }
        guard desiredKeyVersion > 0 else {
            throw CompanionKeyLifecycleError.invalidKeyVersion
        }

        var remote = try await transport.inspectRemote()
        if !remote.zoneExists {
            try await transport.ensureZone()
            remote = try await transport.inspectRemote()
        }
        guard remote.zoneExists else {
            throw CompanionKeyLifecycleError.bootstrapTransportUnavailable
        }

        if let claim = remote.claim {
            do {
                return try await joinExistingClaim(claim, remote: remote)
            } catch CompanionPayloadKeyStoreError.splitKeyPrevented {
                status = .recovery(
                    reason: .bootstrapOwnershipUnverified,
                    keyVersion: claim.keyVersion
                )
                return status
            } catch CompanionKeyLifecycleError.splitKeyPrevented {
                status = .recovery(
                    reason: .bootstrapOwnershipUnverified,
                    keyVersion: claim.keyVersion
                )
                return status
            }
        }
        if remote.hasEncryptedDomainRecords {
            status = .recovery(
                reason: .remoteDataWithoutKey,
                keyVersion: desiredKeyVersion
            )
            return status
        }

        let candidate = try await keyStore.prepareCandidate(
            version: desiredKeyVersion,
            generator: generator
        )
        status = candidate.origin == .externallyProvisioned
            ? .migration(keyVersion: desiredKeyVersion)
            : .claiming(keyVersion: desiredKeyVersion)

        let result = try await transport.createClaim(
            keyVersion: desiredKeyVersion
        ) { [keyStore] receipt in
            try await keyStore.markClaimAccepted(receipt)
        }
        switch result {
        case .created(let receipt):
            let verified = try await transport.inspectRemote()
            guard let claim = verified.claim,
                  receipt.matches(claim),
                  !verified.hasEncryptedDomainRecords else {
                status = .recovery(
                    reason: .bootstrapOwnershipUnverified,
                    keyVersion: desiredKeyVersion
                )
                return status
            }
            try await keyStore.promoteAcceptedCandidate(
                version: desiredKeyVersion,
                matching: claim
            )
            status = .ready(keyVersion: desiredKeyVersion)
        case .existing(let claim):
            if candidate.origin == .generated {
                try await keyStore.discardGeneratedCandidate(
                    version: desiredKeyVersion
                )
            }
            status = .waiting(
                keyVersion: claim.keyVersion,
                reason: .anotherDeviceWonBootstrap
            )
        case .indeterminate:
            status = .recovery(
                reason: .bootstrapOwnershipUnverified,
                keyVersion: desiredKeyVersion
            )
        }
        return status
    }

    public func requireRotation(
        current: UInt32,
        next: UInt32,
        transitionEndsAt: Date
    ) -> CompanionKeyLifecycleStatus {
        status = .rotation(
            current: current,
            next: next,
            transitionEndsAt: transitionEndsAt
        )
        return status
    }

    public func markRevoked(keyVersion: UInt32) -> CompanionKeyLifecycleStatus {
        status = .revoked(keyVersion: keyVersion)
        return status
    }

    public func requireRecovery(
        reason: CompanionKeyRecoveryReason,
        keyVersion: UInt32?
    ) -> CompanionKeyLifecycleStatus {
        status = .recovery(reason: reason, keyVersion: keyVersion)
        return status
    }

    public func shutdown() async {
        await transport.shutdown()
    }

    private func joinExistingClaim(
        _ claim: CompanionBootstrapClaim,
        remote: CompanionBootstrapRemoteSnapshot
    ) async throws -> CompanionKeyLifecycleStatus {
        if let pending = try await keyStore.pendingState(version: claim.keyVersion) {
            if let receipt = pending.acceptedReceipt, receipt.matches(claim) {
                try await keyStore.promoteAcceptedCandidate(
                    version: claim.keyVersion,
                    matching: claim
                )
                status = .ready(keyVersion: claim.keyVersion)
                return status
            }
            status = .recovery(
                reason: .bootstrapOwnershipUnverified,
                keyVersion: claim.keyVersion
            )
            return status
        }
        if try await keyStore.hasCanonicalKey(version: claim.keyVersion) {
            status = .ready(keyVersion: claim.keyVersion)
            return status
        }
        let versions = try await keyStore.knownCanonicalVersions()
        if !versions.isEmpty {
            status = .recovery(
                reason: .keyVersionMismatch,
                keyVersion: claim.keyVersion
            )
        } else if remote.hasEncryptedDomainRecords {
            status = .recovery(
                reason: .remoteDataWithoutKey,
                keyVersion: claim.keyVersion
            )
        } else {
            status = .waiting(
                keyVersion: claim.keyVersion,
                reason: .synchronizableKeyPending
            )
        }
        return status
    }
}
