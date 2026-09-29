import Foundation

extension CompanionAppModel {
    static let extensionSetupMetadataKey = "AhoiExtensionSetupMetadataApproved"

    public var extensionSetupMetadata: [CompanionExtensionSetup] {
        guard isExtensionSetupMetadataApproved else { return [] }
        return snapshot.productRecords.permittedSettings.compactMap {
            CompanionExtensionSetup.decode($0)
        }.sorted { $0.extensionID < $1.extensionID }
    }

    public func setExtensionSetupMetadataApproved(_ approved: Bool) {
        guard !approved || (isSyncConfigured && desiredSyncEnabled) else { return }
        extensionSetupMetadataEpoch &+= 1
        let epoch = extensionSetupMetadataEpoch
        isExtensionSetupMetadataApproved = approved
        defaults.set(approved, forKey: Self.extensionSetupMetadataKey)
        syncProvider?.setExtensionSetupMetadataApproved(approved, epoch: epoch)
        let originalBridge = syncBridge
        let generation = syncGeneration
        Task { [weak self] in
            await originalBridge?.setExtensionSetupMetadataApproved(approved, epoch: epoch)
            guard let self, self.syncGeneration == generation,
                  self.extensionSetupMetadataEpoch == epoch else { return }
            await self.sync()
        }
    }
}
