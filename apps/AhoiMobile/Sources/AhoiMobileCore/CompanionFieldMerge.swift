import Foundation
import AhoiCloudKitSpike

public enum CompanionFieldMergeError: Error, Equatable, Sendable {
    case identityMismatch
    case immutableFieldConflict(String)
    case equalClockConflict(String)
}

/// Wire-v2 field merge for the mutable domains authored by the Companion.
/// Tree location (workspace, parent and order) is one atomic register, matching
/// Chromium's move semantics. Creation identity and node kind are immutable.
public enum CompanionFieldMerge {
    public static let workspaceFields: Set<String> = [
        "name", "icon", "sort_key", "accent_argb", "created_at", "modified_at",
        "tombstone",
    ]
    public static let treeNodeFields: Set<String> = [
        "location", "kind", "title", "icon", "accent_argb", "url", "created_at",
        "modified_at", "tombstone",
    ]

    public static func merge(_ existing: Workspace, _ incoming: Workspace) throws -> Workspace {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.createdAt == incoming.createdAt else {
            throw CompanionFieldMergeError.immutableFieldConflict("created_at")
        }
        let oldVersion = existing.version.normalized(for: workspaceFields)
        let newVersion = incoming.version.normalized(for: workspaceFields)
        var result = existing
        if try incomingWins("name", existing.name, incoming.name, oldVersion, newVersion) {
            result.name = incoming.name
        }
        if try incomingWins("icon", existing.icon, incoming.icon, oldVersion, newVersion) {
            result.icon = incoming.icon
        }
        if try incomingWins(
            "sort_key", existing.sortKey, incoming.sortKey, oldVersion, newVersion
        ) {
            result.sortKey = incoming.sortKey
        }
        if try incomingWins(
            "accent_argb", existing.accent, incoming.accent, oldVersion, newVersion
        ) {
            result.accent = incoming.accent
        }
        if try incomingWins(
            "modified_at", existing.modifiedAt, incoming.modifiedAt, oldVersion, newVersion
        ) {
            result.modifiedAt = incoming.modifiedAt
        }
        if try incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            result.tombstone = incoming.tombstone
        }
        var version = mergedVersion(oldVersion, newVersion, fields: workspaceFields)
        if !workspaceProjectionEqual(result, version, existing, oldVersion),
           !workspaceProjectionEqual(result, version, incoming, newVersion) {
            version = try dominatingMergeVersion(version)
        }
        result.version = version
        return result
    }

    public static func merge(_ existing: TreeNode, _ incoming: TreeNode) throws -> TreeNode {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard SharedTabCreationProvenance.sameTime(existing.createdAt, incoming.createdAt) else {
            throw CompanionFieldMergeError.immutableFieldConflict("created_at")
        }
        guard existing.kind == incoming.kind else {
            throw CompanionFieldMergeError.immutableFieldConflict("kind")
        }
        let oldVersion = existing.version.normalized(for: treeNodeFields)
        let newVersion = incoming.version.normalized(for: treeNodeFields)
        var result = existing
        let oldLocation = TreeLocation(existing)
        let newLocation = TreeLocation(incoming)
        if try incomingWins("location", oldLocation, newLocation, oldVersion, newVersion) {
            result.workspaceID = incoming.workspaceID
            result.parentID = incoming.parentID
            result.orderKey = incoming.orderKey
            result.wireSortKey = incoming.wireSortKey
        }
        if try incomingWins("title", existing.title, incoming.title, oldVersion, newVersion) {
            result.title = incoming.title
        }
        if try incomingWins("icon", existing.icon, incoming.icon, oldVersion, newVersion) {
            result.icon = incoming.icon
        }
        if try incomingWins(
            "accent_argb", existing.accent, incoming.accent, oldVersion, newVersion
        ) {
            result.accent = incoming.accent
        }
        let oldTarget = try SharedTabURLGroup.of(existing)
        let newTarget = try SharedTabURLGroup.of(incoming)
        if try incomingWins("url", oldTarget, newTarget, oldVersion, newVersion) {
            result.url = incoming.url
            result.targetKind = newTarget?.kind
            result.localScheme = newTarget?.localScheme
        } else {
            result.targetKind = oldTarget?.kind
            result.localScheme = oldTarget?.localScheme
        }
        if try incomingWins(
            "modified_at", existing.modifiedAt, incoming.modifiedAt, oldVersion, newVersion
        ) {
            result.modifiedAt = incoming.modifiedAt
        }
        if try incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            result.tombstone = incoming.tombstone
        }
        let persistence = try SharedTabFieldReadMerge.field(
            "is_temporary", existing: existing.isTemporary, incoming: incoming.isTemporary,
            legacyDefault: false, oldVersion: existing.version, newVersion: incoming.version
        )
        result.isTemporary = persistence.value
        var version = SharedTabFieldReadMerge.version(
            base: mergedVersion(oldVersion, newVersion, fields: treeNodeFields),
            field: "is_temporary", clock: persistence.clock
        )
        if version.schemaVersion < 3 { result.targetKind = nil; result.localScheme = nil }
        result.version = version
        result = try SharedTabCreationProvenance.finishMerge(result, old: existing, new: incoming)
        version = result.version
        if !treeProjectionEqual(result, version, existing, oldVersion),
           !treeProjectionEqual(result, version, incoming, newVersion) {
            version = try dominatingMergeVersion(version)
        }
        result.version = version
        return result
    }

    public static func stampLocal(
        previous: Workspace?,
        candidate: Workspace
    ) -> Workspace {
        guard let previous else {
            var result = candidate
            result.version = candidate.version.normalized(for: workspaceFields)
            return result
        }
        var result = candidate
        result.modifiedAt = candidate.version.modifiedAt
        var version = candidate.version.normalized(for: workspaceFields)
        let old = previous.version.normalized(for: workspaceFields)
        let equality: [String: Bool] = [
            "name": previous.name == candidate.name,
            "icon": previous.icon == candidate.icon,
            "sort_key": previous.sortKey == candidate.sortKey,
            "accent_argb": previous.accent == candidate.accent,
            "created_at": previous.createdAt == candidate.createdAt,
            "modified_at": previous.modifiedAt == result.modifiedAt,
            "tombstone": previous.tombstone == candidate.tombstone,
        ]
        for field in workspaceFields where equality[field] == true {
            version.fieldVersions[field] = old.fieldVersions[field]
        }
        result.version = version
        return result
    }

    public static func stampLocal(
        previous: TreeNode?,
        candidate: TreeNode
    ) -> TreeNode {
        guard let previous else {
            var result = candidate
            result.version = candidate.version.normalized(for: treeNodeFields)
            return result
        }
        var result = candidate
        result.modifiedAt = candidate.version.modifiedAt
        var version = candidate.version.normalized(for: treeNodeFields)
        let old = previous.version.normalized(for: treeNodeFields)
        let equality: [String: Bool] = [
            "location": TreeLocation(previous) == TreeLocation(candidate),
            "kind": previous.kind == candidate.kind,
            "title": previous.title == candidate.title,
            "icon": previous.icon == candidate.icon,
            "accent_argb": previous.accent == candidate.accent,
            "url": previous.url == candidate.url && previous.localScheme == candidate.localScheme &&
                (previous.targetKind ?? .web) == (candidate.targetKind ?? .web),
            "created_at": previous.createdAt == candidate.createdAt,
            "modified_at": previous.modifiedAt == result.modifiedAt,
            "tombstone": previous.tombstone == candidate.tombstone,
        ]
        for field in treeNodeFields where equality[field] == true {
            version.fieldVersions[field] = old.fieldVersions[field]
        }
        result.creationProvenanceClock = previous.creationProvenanceClock
        if previous.version.schemaVersion == 3 && candidate.version.schemaVersion < 3 {
            result.isTemporary = previous.isTemporary
            version = SharedTabFieldReadMerge.retainingExistingField(
                "is_temporary", previous: previous.version, candidate: version
            )
            version.fieldVersions["created_at"] = previous.version.fieldVersions["created_at"]
            if previous.url == candidate.url {
                result.targetKind = previous.targetKind
                result.localScheme = previous.localScheme
            } else {
                result.targetKind = candidate.kind == .folder ? nil : .web
                result.localScheme = nil
            }
        }
        result.version = version
        return result
    }

    static func incomingWins<Value: Equatable>(
        _ field: String,
        _ oldValue: Value,
        _ newValue: Value,
        _ oldVersion: SyncVersion,
        _ newVersion: SyncVersion
    ) throws -> Bool {
        let oldClock = oldVersion.fieldVersions[field] ?? oldVersion.modifiedAt
        let newClock = newVersion.fieldVersions[field] ?? newVersion.modifiedAt
        if oldClock == newClock && oldValue != newValue {
            throw CompanionFieldMergeError.equalClockConflict(field)
        }
        return newClock > oldClock
    }

    static func mergedVersion(
        _ existing: SyncVersion,
        _ incoming: SyncVersion,
        fields: Set<String>
    ) -> SyncVersion {
        var versions: [String: HybridLogicalClock] = [:]
        for field in fields {
            versions[field] = max(
                existing.fieldVersions[field] ?? existing.modifiedAt,
                incoming.fieldVersions[field] ?? incoming.modifiedAt
            )
        }
        let top = max(existing.modifiedAt, incoming.modifiedAt)
        return SyncVersion(
            schemaVersion: 2,
            modifiedAt: top,
            modifiedBy: top.nodeID,
            fieldVersions: versions
        )
    }

    static func dominatingMergeVersion(_ version: SyncVersion) throws -> SyncVersion {
        let clock = version.modifiedAt
        let next: HybridLogicalClock
        if clock.logicalCounter < UInt32.max {
            next = HybridLogicalClock(
                physicalMilliseconds: clock.physicalMilliseconds,
                submillisecondMicroseconds: clock.submillisecondMicroseconds,
                logicalCounter: clock.logicalCounter + 1,
                nodeID: clock.nodeID
            )
        } else if clock.submillisecondMicroseconds < 999 {
            next = HybridLogicalClock(
                physicalMilliseconds: clock.physicalMilliseconds,
                submillisecondMicroseconds: clock.submillisecondMicroseconds + 1,
                nodeID: clock.nodeID
            )
        } else {
            guard clock.physicalMilliseconds < UInt64.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            next = HybridLogicalClock(
                physicalMilliseconds: clock.physicalMilliseconds + 1,
                nodeID: clock.nodeID
            )
        }
        return SyncVersion(
            schemaVersion: version.schemaVersion,
            modifiedAt: next,
            modifiedBy: next.nodeID,
            fieldVersions: version.fieldVersions
        )
    }

    private static func workspaceProjectionEqual(
        _ lhs: Workspace,
        _ lhsVersion: SyncVersion,
        _ rhs: Workspace,
        _ rhsVersion: SyncVersion
    ) -> Bool {
        lhs.name == rhs.name && lhs.icon == rhs.icon &&
            lhs.sortKey == rhs.sortKey && lhs.accent == rhs.accent &&
            lhs.createdAt == rhs.createdAt && lhs.modifiedAt == rhs.modifiedAt &&
            lhs.isDeleted == rhs.isDeleted &&
            lhsVersion.fieldVersions == rhsVersion.fieldVersions
    }

    private static func treeProjectionEqual(
        _ lhs: TreeNode,
        _ lhsVersion: SyncVersion,
        _ rhs: TreeNode,
        _ rhsVersion: SyncVersion
    ) -> Bool {
        TreeLocation(lhs) == TreeLocation(rhs) && lhs.kind == rhs.kind &&
            lhs.isTemporary == rhs.isTemporary &&
            (lhs.targetKind ?? (lhs.kind == .folder ? nil : .web)) ==
                (rhs.targetKind ?? (rhs.kind == .folder ? nil : .web)) && lhs.localScheme == rhs.localScheme &&
            lhs.title == rhs.title && lhs.icon == rhs.icon &&
            lhs.accent == rhs.accent && lhs.url == rhs.url &&
            SharedTabCreationProvenance.sameTime(lhs.createdAt, rhs.createdAt) && lhs.modifiedAt == rhs.modifiedAt &&
            lhs.isDeleted == rhs.isDeleted &&
            lhsVersion.fieldVersions == rhsVersion.fieldVersions
    }

    private struct TreeLocation: Equatable {
        let workspace: WorkspaceID
        let parent: TreeNodeID?
        let order: String

        init(_ node: TreeNode) {
            workspace = node.workspaceID
            parent = node.parentID
            order = node.syncSortKey
        }
    }
}
