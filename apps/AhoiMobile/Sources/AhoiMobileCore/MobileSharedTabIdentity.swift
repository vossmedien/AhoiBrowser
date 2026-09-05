import Foundation
import AhoiCloudKitSpike

enum MobileSharedTabIdentity {
    static let inboxID = WorkspaceID(rawValue: UUID(uuidString: "83699047-edf8-580d-948d-9c37acc35cb6")!)
    static let systemActor = DeviceID(rawValue: UUID(uuidString: "9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86")!)

    /// Canonical read/bootstrap value only. Constructing it does not persist,
    /// reorder user workspaces or authorize sync publication.
    static var inbox: Workspace {
        let epoch = HybridLogicalClock(physicalMilliseconds: 0, nodeID: systemActor)
        return Workspace(workspaceID: inboxID, name: "Inbox", icon: "", sortKey: "0",
                         createdAt: epoch, modifiedAt: epoch,
                         version: SyncVersion(modifiedAt: epoch, modifiedBy: systemActor))
    }

}
