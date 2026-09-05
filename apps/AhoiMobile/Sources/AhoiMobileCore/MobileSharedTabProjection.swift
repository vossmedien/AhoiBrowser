import Foundation
import AhoiCloudKitSpike

/// Logical row preparation, independent of WebKit, selection and transport.
/// A local-only target is still a row, never an empty/new-tab URL substitution.
struct MobileSharedTabProjection: Equatable, Identifiable {
    let id: TreeNodeID
    let workspaceID: WorkspaceID
    let title: String
    let isTemporary: Bool
    let target: SharedTabTarget
    let originDevice: DeviceID?

    init?(_ node: TreeNode) {
        guard !node.isDeleted, node.kind == .savedPage,
              let target = try? SharedTabURLGroup.of(node) else { return nil }
        id = node.id
        workspaceID = node.workspaceID
        title = node.title
        isTemporary = node.isTemporary
        self.target = target
        if node.creationProvenanceKnown,
           let clock = node.version.fieldVersions["created_at"], SharedTabContract.isActualMutation(clock) {
            originDevice = clock.nodeID
        } else {
            originDevice = nil
        }
    }

    var canActivateHere: Bool {
        switch target.kind {
        case .web: MobileTabRecord.normalizedURLString(target.url) != nil
        case .newTab: true
        case .localOnly: false
        }
    }

    /// Only the owning local session supplies this original target. Remote
    /// metadata must never replace its file path/code with the wire's empty URL.
    func localRuntimeURL(preservingOwnedTarget original: String?) -> String? {
        switch target.kind {
        case .web: target.url
        case .newTab: nil
        case .localOnly: original
        }
    }
}
