import Foundation

extension TreeNode {
    /// The retained local provenance clock is persisted for promotion, but is not a
    /// replicated value. Including it in domain equality would make an inbound
    /// v2 row trigger endless re-enqueues solely because another device cannot
    /// know which local creation event this device actually observed.
    public static func == (lhs: TreeNode, rhs: TreeNode) -> Bool {
        lhs.id == rhs.id && lhs.workspaceID == rhs.workspaceID && lhs.parentID == rhs.parentID &&
            lhs.kind == rhs.kind && lhs.title == rhs.title && lhs.url == rhs.url &&
            lhs.icon == rhs.icon && lhs.accent == rhs.accent && lhs.orderKey == rhs.orderKey &&
            lhs.wireSortKey == rhs.wireSortKey && lhs.isTemporary == rhs.isTemporary &&
            lhs.targetKind == rhs.targetKind && lhs.localScheme == rhs.localScheme &&
            lhs.createdAt == rhs.createdAt && lhs.modifiedAt == rhs.modifiedAt &&
            lhs.version == rhs.version && lhs.tombstone == rhs.tombstone
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(id)
        hasher.combine(workspaceID)
        hasher.combine(parentID)
        hasher.combine(kind)
        hasher.combine(title)
        hasher.combine(url)
        hasher.combine(icon)
        hasher.combine(accent)
        hasher.combine(orderKey)
        hasher.combine(wireSortKey)
        hasher.combine(isTemporary)
        hasher.combine(targetKind)
        hasher.combine(localScheme)
        hasher.combine(createdAt)
        hasher.combine(modifiedAt)
        hasher.combine(version)
        hasher.combine(tombstone)
    }
}
