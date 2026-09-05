import Foundation
import AhoiCloudKitSpike

enum MobileSharedTabIdentity {
    static let inboxID = SharedTabContract.inboxID
    static let systemActor = SharedTabContract.systemActor

    /// Canonical read/bootstrap value only. Constructing it does not persist,
    /// reorder user workspaces or authorize sync publication.
    static var inbox: Workspace {
        let epoch = HybridLogicalClock(physicalMilliseconds: 0, nodeID: systemActor)
        return Workspace(workspaceID: inboxID, name: "Inbox", icon: "", sortKey: "0",
                         createdAt: epoch, modifiedAt: epoch,
                         version: SyncVersion(modifiedAt: epoch, modifiedBy: systemActor))
    }

}
