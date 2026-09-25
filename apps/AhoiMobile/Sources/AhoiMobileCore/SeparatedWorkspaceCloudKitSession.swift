import Foundation
import AhoiCloudKitSpike

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
extension CloudKitSyncProvider: CloudKitRecordZoneListing {
    /// Zone discovery for ADR 0011 step 4. Read-only; never creates, modifies
    /// or deletes a zone.
    public func allRecordZoneNames() async throws -> [String] {
        guard !statusLock.withLock({ isInvalidated }) else {
            throw CloudKitSyncProviderError.unavailable
        }
        return try await database.allRecordZones().map(\.zoneID.zoneName)
    }
}

/// A separated Workspace's session over the same provider, bridge and key
/// machinery as the main zone, parameterized by its namespace. Its repository,
/// record store, engine state and key are its own.
@available(iOS 17.0, macOS 14.0, *)
@MainActor
final class CloudKitSeparatedWorkspaceSession: SeparatedWorkspaceSyncSession {
    private let runtime: CompanionCloudKitRuntime
    private let repository: LocalFirstRepository
    private let keyStore: KeychainCompanionPayloadKeyStore
    private let keyVersion: UInt32
    private let authorization: CompanionSyncRuntimeAuthorization
    private var prepared = false

    init(
        runtime: CompanionCloudKitRuntime,
        repository: LocalFirstRepository,
        keyStore: KeychainCompanionPayloadKeyStore,
        keyVersion: UInt32,
        authorization: CompanionSyncRuntimeAuthorization
    ) {
        self.runtime = runtime
        self.repository = repository
        self.keyStore = keyStore
        self.keyVersion = keyVersion
        self.authorization = authorization
    }

    func sync() async throws -> CompanionSnapshot {
        if !prepared {
            try await runtime.provider.prepare()
            try await runtime.bridge.enqueueLocalSnapshot()
            prepared = true
        }
        try await runtime.bridge.syncNow()
        return try await repository.currentSnapshot()
    }

    func isKeyPresent() async throws -> Bool {
        try await keyStore.hasCanonicalKey(version: keyVersion)
    }

    func cancel() async {
        authorization.revoke()
        runtime.provider.setEventDrivenSyncHandler(nil)
        await runtime.provider.cancel()
    }
}

/// Builds the production session factory. Every path and identifier comes
/// from the namespace; the main zone, main key account and main files are
/// unreachable from here.
public enum SeparatedWorkspaceCloudKitSessionFactory {
    /// `<support>/SeparatedWorkspaces/<uuid lowercase>/`.
    public static func storageDirectory(for workspaceID: UUID, under supportURL: URL) -> URL {
        supportURL
            .appendingPathComponent("SeparatedWorkspaces", isDirectory: true)
            .appendingPathComponent(workspaceID.uuidString.lowercased(), isDirectory: true)
    }

    @available(iOS 17.0, macOS 14.0, *)
    public static func make(
        containerIdentifier: String,
        baseKeyConfiguration: CompanionSyncKeyConfiguration,
        desiredKeyVersion: UInt32,
        supportURL: URL,
        localDeviceID: DeviceID
    ) -> SeparatedWorkspaceSessionFactory {
        { @MainActor context in
            let authorization = CompanionSyncRuntimeAuthorization()
            let identifiers = context.identifiers
            guard !context.namespace.isMain,
                  identifiers.keychainAccount != baseKeyConfiguration.account else {
                throw CompanionCloudKitBootstrapError.keyConfigurationMissing
            }
            let directory = storageDirectory(for: context.workspaceID, under: supportURL)
            try FileManager.default.createDirectory(
                at: directory, withIntermediateDirectories: true
            )
            let keyConfiguration = CompanionSyncKeyConfiguration(
                service: baseKeyConfiguration.service,
                account: identifiers.keychainAccount,
                accessGroup: baseKeyConfiguration.accessGroup,
                keyVersion: baseKeyConfiguration.keyVersion
            )
            let keyStore = try KeychainCompanionPayloadKeyStore(
                configuration: keyConfiguration,
                authorization: { authorization.isAuthorized() }
            )
            let bootstrapTransport = try CloudKitKeyBootstrapTransport(
                containerIdentifier: containerIdentifier,
                zoneName: identifiers.zoneName,
                subscriptionID: identifiers.subscriptionID,
                authorization: { authorization.isAuthorized() }
            )
            let keyLifecycle = CompanionKeyLifecycleCoordinator(
                transport: bootstrapTransport, keyStore: keyStore,
                generator: CompanionSecureKeyGenerator.aes256,
                authorization: { authorization.isAuthorized() }
            )
            let status: CompanionKeyLifecycleStatus
            do {
                status = try await keyLifecycle.activate(
                    explicitOptIn: true,
                    desiredKeyVersion: desiredKeyVersion
                )
            } catch {
                await keyLifecycle.shutdown()
                authorization.revoke()
                throw error
            }
            let bootstrapClaim = await bootstrapTransport.verifiedClaim()
            await keyLifecycle.shutdown()
            guard case let .ready(activeKeyVersion) = status else {
                authorization.revoke()
                switch status {
                case .waiting(_, .synchronizableKeyPending),
                     .recovery(.remoteDataWithoutKey, _),
                     .recovery(.revokedKey, _),
                     .revoked:
                    return .keyMissing
                default:
                    // Rotation of a separated zone's key is not driven from iOS.
                    return .waitingForKey
                }
            }
            guard let bootstrapClaim,
                  let writingDigest = try await keyStore.canonicalKeySHA256(
                      version: activeKeyVersion
                  ),
                  activeKeyVersion != bootstrapClaim.keyVersion ||
                    writingDigest == bootstrapClaim.keySHA256 else {
                authorization.revoke()
                throw CompanionSyncKeyError.keyCommitmentMismatch
            }
            let repository = LocalFirstRepository(
                store: FileCompanionStore(
                    fileURL: directory.appendingPathComponent("snapshot-format3.json")
                ),
                localDeviceID: localDeviceID
            )
            guard let runtime = try CompanionCloudKitBootstrap.makeRuntimeChecked(
                syncEnabled: true,
                containerIdentifier: containerIdentifier,
                zoneName: identifiers.zoneName,
                subscriptionID: identifiers.subscriptionID,
                keyConfiguration: keyConfiguration.canonicalConfiguration(
                    for: activeKeyVersion
                ),
                repository: repository,
                recordsURL: directory.appendingPathComponent("sync-records.json"),
                stateURL: directory.appendingPathComponent("sync-engine-state.json"),
                bootstrapClaim: bootstrapClaim,
                verifiedWritingKeySHA256: writingDigest
            ) else {
                authorization.revoke()
                throw CompanionCloudKitBootstrapError.providerUnavailable
            }
            return .ready(CloudKitSeparatedWorkspaceSession(
                runtime: runtime,
                repository: repository,
                keyStore: keyStore,
                keyVersion: activeKeyVersion,
                authorization: authorization
            ))
        }
    }

    /// Removes a retired Workspace's local sync files. Only ever inside
    /// `SeparatedWorkspaces/`.
    public static func removeLocalData(for workspaceID: UUID, under supportURL: URL) {
        let directory = storageDirectory(for: workspaceID, under: supportURL)
        guard directory.deletingLastPathComponent().lastPathComponent == "SeparatedWorkspaces"
        else { return }
        try? FileManager.default.removeItem(at: directory)
    }
}

#endif
