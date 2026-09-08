import Foundation
import AhoiCloudKitSpike

/// A timestamp field's value excludes its register's device/counter metadata.
/// C++ stores base::Time for these values; comparisons must match that boundary.
struct CompanionTimestampValue: Equatable {
    let milliseconds: UInt64
    let microseconds: UInt16

    init(_ clock: HybridLogicalClock) {
        milliseconds = clock.physicalMilliseconds
        microseconds = clock.submillisecondMicroseconds
    }
}

enum SharedTabCreationProvenance {
    static func sameTime(_ lhs: HybridLogicalClock, _ rhs: HybridLogicalClock) -> Bool {
        lhs.physicalMilliseconds == rhs.physicalMilliseconds &&
            lhs.submillisecondMicroseconds == rhs.submillisecondMicroseconds
    }

    /// The timestamp value stays immutable. Its field clock is independent
    /// register metadata, never the enclosing record's last-editor clock.
    static func reframe(_ node: TreeNode) throws -> TreeNode {
        guard let provenance = node.version.fieldVersions["created_at"] else {
            throw SharedSyncFormatError.invalidFieldMap
        }
        let time = HybridLogicalClock(physicalMilliseconds: node.createdAt.physicalMilliseconds,
                                      submillisecondMicroseconds: node.createdAt.submillisecondMicroseconds,
                                      logicalCounter: provenance.logicalCounter, nodeID: provenance.nodeID)
        return try TreeNode(treeNodeID: node.id, workspaceID: node.workspaceID, parentID: node.parentID,
                            kind: node.kind, title: node.title, url: node.url, icon: node.icon, accent: node.accent,
                            orderKey: node.orderKey, wireSortKey: node.wireSortKey, isTemporary: node.isTemporary,
                            targetKind: node.targetKind, localScheme: node.localScheme,
                            createdAt: time, modifiedAt: node.modifiedAt, version: node.version, tombstone: node.tombstone)
    }
}

enum SharedTabURLGroup {
    static func of(_ node: TreeNode) throws -> SharedTabTarget? {
        guard node.version.schemaVersion == SharedSyncFormat.currentVersion else {
            throw SharedSyncFormatError.unsupportedVersion
        }
        if node.kind == .folder {
            guard node.targetKind == nil, node.localScheme == nil, node.url == nil, !node.isTemporary else {
                throw SharedTabTargetError.invalidTarget
            }
            return nil
        }
        guard let kind = node.targetKind else { throw SharedTabTargetError.invalidTarget }
        let target = try SharedTabTarget(kind: kind,
                                         url: node.url ?? "", localScheme: node.localScheme)
        try target.validatePage(isTemporary: node.isTemporary)
        return target
    }

    static func of(_ tab: RemoteTab) throws -> SharedTabTarget {
        guard tab.version.schemaVersion == SharedSyncFormat.currentVersion else {
            throw SharedSyncFormatError.unsupportedVersion
        }
        guard let kind = tab.targetKind, let link = tab.treeNodeID, link.rawValue != tab.id.rawValue else {
            throw SharedTabTargetError.invalidTarget
        }
        let target = try SharedTabTarget(kind: kind,
                                         url: tab.url, localScheme: tab.localScheme)
        try target.validatePresence(treeNodeID: tab.treeNodeID)
        return target
    }
}
