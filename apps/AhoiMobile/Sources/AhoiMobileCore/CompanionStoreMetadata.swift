import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    func workspaceOrderKey(_ version: SyncVersion) -> String {
        String(format: "%016llx", version.modifiedAt.physicalMicroseconds)
            + "!" + localDeviceID.rawValue.uuidString.lowercased()
    }

    func makeTombstone(
        entityID: UUID,
        version: SyncVersion,
        parentID: UUID?,
        orderKey: OrderKey?
    ) -> Tombstone {
        Tombstone(
            entityID: entityID,
            deletedAt: version.modifiedAt,
            deletedBy: localDeviceID,
            originalParentID: parentID,
            originalOrderKey: orderKey,
            purgeAfterMilliseconds: version.modifiedAt.physicalMilliseconds
                + UInt64(30 * 24 * 60 * 60 * 1_000)
        )
    }
}
