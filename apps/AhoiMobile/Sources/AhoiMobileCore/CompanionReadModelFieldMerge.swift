import Foundation
import AhoiCloudKitSpike

/// Field-register convergence for records the Companion normally consumes
/// from a desktop. Presentation-only device/workspace labels are deliberately
/// excluded from the wire projection.
public enum CompanionReadModelFieldMerge {
    private static let deviceFields: Set<String> = [
        "type", "display_name", "created_at", "last_seen", "retired", "tombstone",
    ]
    private static let sessionFields: Set<String> = [
        "device_id", "started_at", "liveness", "tombstone",
    ]
    private static let tabFields: Set<String> = [
        "device_id", "session_id", "workspace_id", "url", "title", "opened_at",
        "last_active", "pinned", "is_incognito", "tombstone",
    ]
    private static let historyFields: Set<String> = [
        "device_id", "url", "title", "last_visit", "visit_count", "transition",
        "tombstone",
    ]

    public static func merge(_ existing: Device, _ incoming: Device) throws -> Device {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.kind == incoming.kind else {
            throw CompanionFieldMergeError.immutableFieldConflict("type")
        }
        guard existing.createdAt == incoming.createdAt else {
            throw CompanionFieldMergeError.immutableFieldConflict("created_at")
        }
        let oldVersion = existing.version.normalized(for: deviceFields)
        let newVersion = incoming.version.normalized(for: deviceFields)
        var result = existing
        if try incomingWins(
            "display_name", existing.name, incoming.name, oldVersion, newVersion
        ) {
            result.name = incoming.name
        }
        if try incomingWins(
            "last_seen", existing.lastSeenAt, incoming.lastSeenAt, oldVersion, newVersion
        ) {
            result.lastSeenAt = incoming.lastSeenAt
        }
        var retired = existing.isRevoked && !existing.isDeleted
        let incomingRetired = incoming.isRevoked && !incoming.isDeleted
        if try incomingWins("retired", retired, incomingRetired, oldVersion, newVersion) {
            retired = incomingRetired
        }
        if try incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            result.tombstone = incoming.tombstone
        }
        result.isRevoked = retired || result.isDeleted
        result.isOnline = !result.isRevoked
        let merged = CompanionFieldMerge.mergedVersion(
            oldVersion, newVersion, fields: deviceFields
        )
        result.version = try mergeVersion(
            merged,
            sameAsOld: deviceProjection(result, merged) == deviceProjection(existing, oldVersion),
            sameAsNew: deviceProjection(result, merged) == deviceProjection(incoming, newVersion)
        )
        return result
    }

    public static func merge(
        _ existing: DeviceSession,
        _ incoming: DeviceSession
    ) throws -> DeviceSession {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.deviceID == incoming.deviceID else {
            throw CompanionFieldMergeError.immutableFieldConflict("device_id")
        }
        guard existing.startedAt == incoming.startedAt else {
            throw CompanionFieldMergeError.immutableFieldConflict("started_at")
        }
        let oldVersion = existing.version.normalized(for: sessionFields)
        let newVersion = incoming.version.normalized(for: sessionFields)
        var result = existing
        let oldLiveness = Liveness(existing)
        let newLiveness = Liveness(incoming)
        if try incomingWins("liveness", oldLiveness, newLiveness, oldVersion, newVersion) {
            result.lastActiveAt = incoming.lastActiveAt
            result.isOnline = incoming.isOnline
        }
        if try incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            result.tombstone = incoming.tombstone
        }
        result.deviceName = incoming.deviceName
        result.workspaceID = incoming.workspaceID
        let merged = CompanionFieldMerge.mergedVersion(
            oldVersion, newVersion, fields: sessionFields
        )
        result.version = try mergeVersion(
            merged,
            sameAsOld: sessionProjection(result, merged) ==
                sessionProjection(existing, oldVersion),
            sameAsNew: sessionProjection(result, merged) ==
                sessionProjection(incoming, newVersion)
        )
        return result
    }

    public static func merge(_ existing: RemoteTab, _ incoming: RemoteTab) throws -> RemoteTab {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.deviceID == incoming.deviceID else {
            throw CompanionFieldMergeError.immutableFieldConflict("device_id")
        }
        guard existing.sessionID == incoming.sessionID else {
            throw CompanionFieldMergeError.immutableFieldConflict("session_id")
        }
        guard existing.openedAt == incoming.openedAt else {
            throw CompanionFieldMergeError.immutableFieldConflict("opened_at")
        }
        guard existing.context == .normal, incoming.context == .normal else {
            throw CompanionModelError.incognitoNotSyncable
        }
        let oldVersion = existing.version.normalized(for: tabFields)
        let newVersion = incoming.version.normalized(for: tabFields)
        var result = existing
        if try incomingWins(
            "workspace_id", existing.workspaceID, incoming.workspaceID, oldVersion, newVersion
        ) {
            result.workspaceID = incoming.workspaceID
        }
        if try incomingWins("url", existing.url, incoming.url, oldVersion, newVersion) {
            result.url = incoming.url
        }
        if try incomingWins("title", existing.title, incoming.title, oldVersion, newVersion) {
            result.title = incoming.title
        }
        if try incomingWins(
            "last_active", existing.lastActiveAt, incoming.lastActiveAt, oldVersion, newVersion
        ) {
            result.lastActiveAt = incoming.lastActiveAt
        }
        if try incomingWins("pinned", existing.pinned, incoming.pinned, oldVersion, newVersion) {
            result.pinned = incoming.pinned
        }
        if try incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            result.tombstone = incoming.tombstone
            result.isOpen = incoming.isOpen
        }
        result.deviceName = incoming.deviceName
        result.workspaceName = incoming.workspaceName
        let binding = try SharedTabFieldReadMerge.field(
            "tree_node_id", existing: existing.treeNodeID, incoming: incoming.treeNodeID,
            legacyDefault: nil, oldVersion: existing.version, newVersion: incoming.version
        )
        result.treeNodeID = binding.value
        let merged = SharedTabFieldReadMerge.version(
            base: CompanionFieldMerge.mergedVersion(oldVersion, newVersion, fields: tabFields),
            field: "tree_node_id", clock: binding.clock
        )
        result.version = try mergeVersion(
            merged,
            sameAsOld: tabProjection(result, merged) == tabProjection(existing, oldVersion),
            sameAsNew: tabProjection(result, merged) == tabProjection(incoming, newVersion)
        )
        return result
    }

    /// Local publications mint one new record clock, but fields whose values did
    /// not change must retain their original field clocks. Otherwise an encode /
    /// decode round trip combines an immutable timestamp value with a newer field
    /// clock and makes the value appear to have changed remotely.
    public static func stampLocal(previous: Device, candidate: Device) -> Device {
        var result = candidate
        var version = candidate.version.normalized(for: deviceFields)
        let oldVersion = previous.version.normalized(for: deviceFields)
        let equality: [String: Bool] = [
            "type": previous.kind == candidate.kind,
            "display_name": previous.name == candidate.name,
            "created_at": previous.createdAt == candidate.createdAt,
            "last_seen": previous.lastSeenAt == candidate.lastSeenAt,
            "retired": previous.isRevoked == candidate.isRevoked,
            "tombstone": previous.tombstone == candidate.tombstone,
        ]
        for field in deviceFields where equality[field] == true {
            version.fieldVersions[field] = oldVersion.fieldVersions[field]
        }
        result.version = version
        return result
    }

    public static func stampLocal(
        previous: DeviceSession,
        candidate: DeviceSession
    ) -> DeviceSession {
        var result = candidate
        var version = candidate.version.normalized(for: sessionFields)
        let oldVersion = previous.version.normalized(for: sessionFields)
        let equality: [String: Bool] = [
            "device_id": previous.deviceID == candidate.deviceID,
            "started_at": previous.startedAt == candidate.startedAt,
            "liveness": Liveness(previous) == Liveness(candidate),
            "tombstone": previous.tombstone == candidate.tombstone,
        ]
        for field in sessionFields where equality[field] == true {
            version.fieldVersions[field] = oldVersion.fieldVersions[field]
        }
        result.version = version
        return result
    }

    public static func stampLocal(previous: RemoteTab, candidate: RemoteTab) -> RemoteTab {
        var result = candidate
        var version = candidate.version.normalized(for: tabFields)
        let oldVersion = previous.version.normalized(for: tabFields)
        let equality: [String: Bool] = [
            "device_id": previous.deviceID == candidate.deviceID,
            "session_id": previous.sessionID == candidate.sessionID,
            "workspace_id": previous.workspaceID == candidate.workspaceID,
            "url": previous.url == candidate.url,
            "title": previous.title == candidate.title,
            "opened_at": previous.openedAt == candidate.openedAt,
            "last_active": previous.lastActiveAt == candidate.lastActiveAt,
            "pinned": previous.pinned == candidate.pinned,
            "is_incognito": previous.context == candidate.context,
            "tombstone": previous.tombstone == candidate.tombstone,
        ]
        for field in tabFields where equality[field] == true {
            version.fieldVersions[field] = oldVersion.fieldVersions[field]
        }
        if previous.version.schemaVersion == 3 && candidate.version.schemaVersion < 3 {
            result.treeNodeID = previous.treeNodeID
            version = SharedTabFieldReadMerge.retainingExistingField(
                "tree_node_id", previous: previous.version, candidate: version
            )
        }
        result.version = version
        return result
    }

    public static func merge(
        _ existing: HistoryVisit,
        _ incoming: HistoryVisit
    ) throws -> HistoryVisit {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.deviceID == incoming.deviceID else {
            throw CompanionFieldMergeError.immutableFieldConflict("device_id")
        }
        guard existing.url == incoming.url else {
            throw CompanionFieldMergeError.immutableFieldConflict("url")
        }
        guard existing.visitedAt == incoming.visitedAt else {
            throw CompanionFieldMergeError.immutableFieldConflict("last_visit")
        }
        guard existing.visitCount == incoming.visitCount else {
            throw CompanionFieldMergeError.immutableFieldConflict("visit_count")
        }
        guard existing.transition == incoming.transition else {
            throw CompanionFieldMergeError.immutableFieldConflict("transition")
        }
        let oldVersion = existing.version.normalized(for: historyFields)
        let newVersion = incoming.version.normalized(for: historyFields)
        var result = existing
        if try incomingWins("title", existing.title, incoming.title, oldVersion, newVersion) {
            result.title = incoming.title
        }
        if try incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            result.tombstone = incoming.tombstone
        }
        let merged = CompanionFieldMerge.mergedVersion(
            oldVersion, newVersion, fields: historyFields
        )
        result.version = try mergeVersion(
            merged,
            sameAsOld: historyProjection(result, merged) ==
                historyProjection(existing, oldVersion),
            sameAsNew: historyProjection(result, merged) ==
                historyProjection(incoming, newVersion)
        )
        return result
    }

    public static func stampLocal(
        previous: HistoryVisit,
        candidate: HistoryVisit
    ) -> HistoryVisit {
        var result = candidate
        var version = candidate.version.normalized(for: historyFields)
        let oldVersion = previous.version.normalized(for: historyFields)
        let equality: [String: Bool] = [
            "device_id": previous.deviceID == candidate.deviceID,
            "url": previous.url == candidate.url,
            "title": previous.title == candidate.title,
            "last_visit": previous.visitedAt == candidate.visitedAt,
            "visit_count": previous.visitCount == candidate.visitCount,
            "transition": previous.transition == candidate.transition,
            "tombstone": previous.tombstone == candidate.tombstone,
        ]
        for field in historyFields where equality[field] == true {
            version.fieldVersions[field] = oldVersion.fieldVersions[field]
        }
        result.version = version
        return result
    }

    private static func incomingWins<Value: Equatable>(
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

    private static func mergeVersion(
        _ merged: SyncVersion,
        sameAsOld: Bool,
        sameAsNew: Bool
    ) throws -> SyncVersion {
        return !sameAsOld && !sameAsNew
            ? try CompanionFieldMerge.dominatingMergeVersion(merged)
            : merged
    }

    private struct Liveness: Equatable {
        let lastSeen: HybridLogicalClock
        let active: Bool
        init(_ value: DeviceSession) {
            lastSeen = value.lastActiveAt
            active = value.isOnline
        }
    }

    private struct DeviceProjection: Equatable {
        let name: String
        let lastSeen: HybridLogicalClock
        let retired: Bool
        let deleted: Bool
        let fields: [String: HybridLogicalClock]
    }

    private static func deviceProjection(
        _ value: Device,
        _ version: SyncVersion
    ) -> DeviceProjection {
        .init(
            name: value.name,
            lastSeen: value.lastSeenAt,
            retired: value.isRevoked && !value.isDeleted,
            deleted: value.isDeleted,
            fields: version.fieldVersions
        )
    }

    private struct SessionProjection: Equatable {
        let liveness: Liveness
        let deleted: Bool
        let fields: [String: HybridLogicalClock]
    }

    private static func sessionProjection(
        _ value: DeviceSession,
        _ version: SyncVersion
    ) -> SessionProjection {
        .init(liveness: .init(value), deleted: value.isDeleted, fields: version.fieldVersions)
    }

    private struct TabProjection: Equatable {
        let workspace: WorkspaceID?
        let treeNode: TreeNodeID?
        let url: String
        let title: String
        let lastActive: HybridLogicalClock
        let pinned: Bool
        let deleted: Bool
        let fields: [String: HybridLogicalClock]
    }

    private static func tabProjection(
        _ value: RemoteTab,
        _ version: SyncVersion
    ) -> TabProjection {
        .init(
            workspace: value.workspaceID,
            treeNode: value.treeNodeID,
            url: value.url,
            title: value.title,
            lastActive: value.lastActiveAt,
            pinned: value.pinned,
            deleted: value.isDeleted,
            fields: version.fieldVersions
        )
    }

    private struct HistoryProjection: Equatable {
        let title: String
        let deleted: Bool
        let fields: [String: HybridLogicalClock]
    }

    private static func historyProjection(
        _ value: HistoryVisit,
        _ version: SyncVersion
    ) -> HistoryProjection {
        .init(title: value.title, deleted: value.isDeleted, fields: version.fieldVersions)
    }
}
