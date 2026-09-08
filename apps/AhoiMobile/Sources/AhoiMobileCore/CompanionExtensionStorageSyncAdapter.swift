import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    public func setExtensionStorageMetadataApproved(_ approved: Bool, epoch: UInt64) {
        guard epoch >= extensionStorageMetadataEpoch else { return }
        extensionStorageMetadataEpoch = epoch
        provider.setExtensionStorageMetadataApproved(approved, epoch: epoch)
        if extensionStorageMetadataApproved != approved {
            extensionStorageMetadataApproved = approved
            extensionStorageHydrationRequired = approved
        }
    }

    func canReadExtensionStorage(_ record: SyncRecord) -> Bool {
        record.dataClass == .permittedSetting && record.recordID == record.entityID &&
            extensionStorageMetadataApproved &&
            CompanionExtensionStorage.recordIDs.contains(record.entityID) &&
            provider.isExtensionStorageMetadataApproved(epoch: extensionStorageMetadataEpoch)
    }
}

extension CompanionAppModel {
    static let extensionStorageMetadataKey = "AhoiExtensionStorageMetadataApproved"

    public var extensionStorageMetadata: [CompanionExtensionStorage] {
        guard isExtensionStorageMetadataApproved else { return [] }
        return snapshot.productRecords.permittedSettings.compactMap {
            CompanionExtensionStorage.decode($0)
        }.sorted { $0.key < $1.key }
    }

    public func setExtensionStorageMetadataApproved(_ approved: Bool) {
        guard !approved || (isSyncConfigured && desiredSyncEnabled) else { return }
        extensionStorageMetadataEpoch &+= 1
        let epoch = extensionStorageMetadataEpoch
        isExtensionStorageMetadataApproved = approved
        defaults.set(approved, forKey: Self.extensionStorageMetadataKey)
        syncProvider?.setExtensionStorageMetadataApproved(approved, epoch: epoch)
        let originalBridge = syncBridge
        let generation = syncGeneration
        Task { [weak self] in
            await originalBridge?.setExtensionStorageMetadataApproved(approved, epoch: epoch)
            guard let self, self.syncGeneration == generation,
                  self.extensionStorageMetadataEpoch == epoch else { return }
            await self.sync()
        }
    }
}
