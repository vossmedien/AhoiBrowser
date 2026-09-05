import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    func observeStoredClocks() {
        let clocks = snapshot.bookmarks.map(\.version.modifiedAt)
            + snapshot.deviceCapabilities.map(\.version.modifiedAt)
            + snapshot.devices.map(\.version.modifiedAt)
            + snapshot.workspaces.map(\.version.modifiedAt)
            + snapshot.treeNodes.map(\.version.modifiedAt)
            + snapshot.sessions.map(\.version.modifiedAt)
            + snapshot.remoteTabs.map(\.version.modifiedAt)
            + snapshot.history.map(\.version.modifiedAt)
            + snapshot.productRecords.appearance.map(\.version.modifiedAt)
            + snapshot.productRecords.permittedSettings.map(\.version.modifiedAt)
            + snapshot.productRecords.extensionInventory.map(\.version.modifiedAt)
            + snapshot.productRecords.developerAssets.map(\.version.modifiedAt)
        guard let newest = clocks.max(), newest >= clock else { return }
        clock = HybridLogicalClock(
            physicalMilliseconds: newest.physicalMilliseconds,
            submillisecondMicroseconds: newest.submillisecondMicroseconds,
            logicalCounter: newest.logicalCounter,
            nodeID: localDeviceID
        )
    }
}
