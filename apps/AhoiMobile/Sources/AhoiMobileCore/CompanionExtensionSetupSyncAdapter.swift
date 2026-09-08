import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    /// Read-only setup metadata on iOS. This approval grants neither a native
    /// extension runtime nor outbound replay of cached setup configuration.
    public func setExtensionSetupMetadataApproved(_ approved: Bool, epoch: UInt64) {
        guard epoch >= extensionSetupMetadataEpoch else { return }
        extensionSetupMetadataEpoch = epoch
        provider.setExtensionSetupMetadataApproved(approved, epoch: epoch)
        if extensionSetupMetadataApproved != approved {
            extensionSetupMetadataApproved = approved
            extensionSetupHydrationRequired = approved
        }
    }

    func canReadExtensionSetup(_ record: SyncRecord) -> Bool {
        record.dataClass == .permittedSetting && record.recordID == record.entityID &&
            extensionSetupMetadataApproved &&
            provider.isExtensionSetupMetadataApproved(epoch: extensionSetupMetadataEpoch) &&
            !CompanionExtensionStorage.recordIDs.contains(record.entityID) &&
            !CompanionBrowserSettingCatalog.recordIDs.contains(record.entityID)
    }
}
