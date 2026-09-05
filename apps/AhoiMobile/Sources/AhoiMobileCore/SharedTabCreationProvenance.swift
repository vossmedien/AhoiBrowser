import Foundation
import AhoiCloudKitSpike

enum SharedTabCreationProvenance {
    static func sameTime(_ lhs: HybridLogicalClock, _ rhs: HybridLogicalClock) -> Bool {
        lhs.physicalMilliseconds == rhs.physicalMilliseconds &&
            lhs.submillisecondMicroseconds == rhs.submillisecondMicroseconds
    }

    static func finishMerge(_ result: TreeNode, old: TreeNode, new: TreeNode) throws -> TreeNode {
        var result = result
        // Retained observations are local metadata, not candidates for the
        // replicated creation register. A peer's unknown/Bottom promotion
        // cannot withdraw genuine evidence already held by this repository.
        let evidence = [old.creationProvenanceClock, new.creationProvenanceClock].compactMap { $0 }
        guard evidence.allSatisfy(SharedTabContract.isActualMutation) else {
            throw SharedTabFieldReadMergeError.invalidLegacyField
        }
        if result.version.schemaVersion == 3 {
            // A later v2 write cannot turn unknown v3 creation provenance into
            // a spurious last-editor badge. Bottom has semantic absence order.
            let clocks = [old, new].filter { $0.version.schemaVersion == 3 }
                .compactMap { $0.version.fieldVersions["created_at"] }
            guard !clocks.isEmpty else { throw SharedTabFieldReadMergeError.missingFieldClock }
            let real = clocks.filter { !SharedTabContract.isBottom($0) }
            guard real.allSatisfy(SharedTabContract.isActualMutation) else {
                throw SharedTabFieldReadMergeError.invalidLegacyField
            }
            let selected = real.max() ?? SharedTabContract.bottom
            result.version.fieldVersions["created_at"] = selected
            return try reframe(result, provenance: selected,
                               evidence: SharedTabContract.isBottom(selected) ? evidence.max() : selected)
        }
        guard let selected = result.version.fieldVersions["created_at"] else {
            throw SharedTabFieldReadMergeError.missingFieldClock
        }
        // Keep the actual observation, not a Boolean attached to the winning
        // legacy clock. A synthetic last-editor clock may win the v2 register
        // without becoming creation evidence or erasing our original clock.
        return try reframe(result, provenance: selected, evidence: evidence.max())
    }

    static func reframe(
        _ node: TreeNode, provenance: HybridLogicalClock, evidence: HybridLogicalClock?
    ) throws -> TreeNode {
        let time = HybridLogicalClock(physicalMilliseconds: node.createdAt.physicalMilliseconds,
                                      submillisecondMicroseconds: node.createdAt.submillisecondMicroseconds,
                                      logicalCounter: provenance.logicalCounter, nodeID: provenance.nodeID)
        return try TreeNode(treeNodeID: node.id, workspaceID: node.workspaceID, parentID: node.parentID,
                            kind: node.kind, title: node.title, url: node.url, icon: node.icon, accent: node.accent,
                            orderKey: node.orderKey, wireSortKey: node.wireSortKey, isTemporary: node.isTemporary,
                            creationProvenanceClock: evidence, targetKind: node.targetKind, localScheme: node.localScheme,
                            createdAt: time, modifiedAt: node.modifiedAt, version: node.version, tombstone: node.tombstone)
    }

    /// Pure migration preparation. It is not called by the running sync loop;
    /// the writer gate still rejects this value until independently activated.
    static func preparePromotion(_ node: TreeNode) throws -> TreeNode {
        guard (1...2).contains(node.version.schemaVersion), !node.isTemporary else {
            throw SharedTabFieldReadMergeError.invalidLegacyField
        }
        var result = node
        let old = node.version.normalized(for: CompanionFieldMerge.treeNodeFields)
        var fields = old.fieldVersions
        fields["is_temporary"] = SharedTabContract.bottom
        let provenance = node.creationProvenanceClock ?? SharedTabContract.bottom
        guard SharedTabContract.isBottom(provenance) || SharedTabContract.isActualMutation(provenance) else {
            throw SharedTabFieldReadMergeError.invalidLegacyField
        }
        fields["created_at"] = provenance
        result.version = SyncVersion(schemaVersion: 3, modifiedAt: old.modifiedAt,
                                     modifiedBy: old.modifiedBy, fieldVersions: fields)
        if node.kind == .savedPage {
            try SharedTabTarget(kind: .web, url: node.url ?? "").validatePage(isTemporary: false)
            result.targetKind = .web
        }
        return try reframe(result, provenance: provenance,
                           evidence: SharedTabContract.isBottom(provenance) ? nil : provenance)
    }
}

enum SharedTabURLGroup {
    static func of(_ node: TreeNode) throws -> SharedTabTarget? {
        guard node.kind != .folder else { return nil }
        let target = try SharedTabTarget(kind: node.version.schemaVersion == 3 ? node.targetKind ?? .web : .web,
                                         url: node.url ?? "", localScheme: node.localScheme)
        try target.validatePage(isTemporary: node.isTemporary)
        return target
    }

    static func of(_ tab: RemoteTab) throws -> SharedTabTarget {
        let target = try SharedTabTarget(kind: tab.version.schemaVersion == 3 ? tab.targetKind ?? .web : .web,
                                         url: tab.url, localScheme: tab.localScheme)
        try target.validatePresence(treeNodeID: tab.treeNodeID)
        return target
    }
}
