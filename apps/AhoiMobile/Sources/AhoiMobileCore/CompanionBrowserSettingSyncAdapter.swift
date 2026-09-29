import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    public func setBrowserSettingsSyncEnabled(_ enabled: Bool, epoch: UInt64) {
        guard epoch >= browserSettingsApprovalEpoch else { return }
        browserSettingsApprovalEpoch = epoch
        let ids = enabled ? CompanionBrowserSettingCatalog.recordIDs : []
        provider.setBrowserSettingApprovedIDs(ids, epoch: epoch)
        if ids != browserSettingApprovedIDs {
            browserSettingApprovedIDs = ids
            browserSettingsHydrationRequired = enabled
        }
    }

    nonisolated static func browserSettingValidator(
        codec: CompanionPayloadCodec
    ) -> BrowserSettingTransportAuthorization.Validator {
        { record in
            let value = try DesktopWirePayloadCodec().decodePermittedSetting(
                record, plaintext: codec.openData(record)
            )
            guard record.recordID == record.entityID, value.id == record.recordID,
                  value.version.schemaVersion == record.schemaVersion,
                  value.version.modifiedAt == record.modifiedAt,
                  value.version.modifiedBy == record.originatingDevice,
                  value.tombstone == record.tombstone,
                  CompanionBrowserSettingCatalog.isPortable(value) else {
                throw BrowserSettingTransportAuthorizationError.invalidPayload
            }
        }
    }
}
