import Foundation

#if canImport(CloudKit)

/// Owns the temporary transport used while normal encrypted-domain writes are
/// disabled. The provider is cancelled by the caller after the coordinator
/// completes or reaches a fail-closed product state.
@available(iOS 17.0, macOS 14.0, *)
public struct CompanionCloudKitKeyRotationRuntime: Sendable {
    public let provider: CloudKitSyncProvider
    public let coordinator: CompanionKeyRotationCoordinator

    public init(
        provider: CloudKitSyncProvider,
        coordinator: CompanionKeyRotationCoordinator
    ) {
        self.provider = provider
        self.coordinator = coordinator
    }
}

public enum CompanionCloudKitKeyRotationBootstrap {
    /// Builds one real CKSyncEngine-backed rotation lane over the same durable
    /// record store as normal sync. A missing writer-membership authority is
    /// intentional: the remote adapter then rejects before upload, preserving
    /// both key versions and the restart journal.
    @available(iOS 17.0, macOS 14.0, *)
    public static func makeRuntimeChecked(
        containerIdentifier: String,
        familyAnchorConfiguration: CompanionSyncKeyConfiguration,
        currentVersion: UInt32,
        nextVersion: UInt32,
        recordsURL: URL,
        stateURL: URL,
        journalURL: URL,
        keyStore: any CompanionPayloadKeyRotationStoring,
        writerAcknowledgements:
            (any CloudKitKeyRotationWriterAcknowledgementProviding)? = nil
    ) async throws -> CompanionCloudKitKeyRotationRuntime {
        guard nextVersion > currentVersion else {
            throw CompanionKeyRotationError.invalidVersions
        }
        guard let provider = try CompanionCloudKitBootstrap.makeProviderChecked(
            syncEnabled: true,
            containerIdentifier: containerIdentifier,
            recordsURL: recordsURL,
            stateURL: stateURL,
            automaticallySync: false
        ) else {
            throw CompanionCloudKitBootstrapError.providerUnavailable
        }
        do {
            let sealer = try KeychainCompanionKeyRotationSealer(
                familyAnchorConfiguration: familyAnchorConfiguration,
                currentVersion: currentVersion,
                nextVersion: nextVersion
            )
            let coordinator = CompanionKeyRotationCoordinator(
                journalStore: FileCompanionKeyRotationJournalStore(
                    fileURL: journalURL
                ),
                keyStore: keyStore,
                recordStore: provider.recordStore,
                sealer: sealer,
                remote: provider.makeKeyRotationRemoteAdapter(
                    writerAcknowledgements: writerAcknowledgements
                ),
                generator: CompanionSecureKeyGenerator.aes256
            )
            return .init(provider: provider, coordinator: coordinator)
        } catch {
            await provider.cancel()
            throw error
        }
    }
}

#endif
