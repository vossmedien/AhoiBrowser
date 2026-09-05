import Foundation
import AhoiCloudKitSpike

enum CompanionBookmarkFieldMerge {
    static func merge(_ existing: BookmarkRecord, _ incoming: BookmarkRecord) throws -> BookmarkRecord {
        try existing.validate()
        try incoming.validate()
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.kind == incoming.kind else {
            throw CompanionFieldMergeError.immutableFieldConflict("kind")
        }
        let old = existing.version.normalized(for: BookmarkRecord.syncFields)
        let new = incoming.version.normalized(for: BookmarkRecord.syncFields)
        var merged = existing
        if try CompanionFieldMerge.incomingWins(
            "location", Location(existing), Location(incoming), old, new
        ) {
            merged.rootKind = incoming.rootKind
            merged.parentID = incoming.parentID
            merged.sortKey = incoming.sortKey
        }
        if try CompanionFieldMerge.incomingWins("title", existing.title, incoming.title, old, new) {
            merged.title = incoming.title
        }
        if try CompanionFieldMerge.incomingWins("url", existing.url, incoming.url, old, new) {
            merged.url = incoming.url
        }
        if try CompanionFieldMerge.incomingWins(
            "created_at", existing.createdAt, incoming.createdAt, old, new
        ) {
            merged.createdAt = incoming.createdAt
        }
        if try CompanionFieldMerge.incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, old, new
        ) {
            merged.tombstone = incoming.tombstone
        }
        var version = CompanionFieldMerge.mergedVersion(old, new, fields: BookmarkRecord.syncFields)
        if !sameProjection(merged, version, existing, old),
           !sameProjection(merged, version, incoming, new) {
            version = try CompanionFieldMerge.dominatingMergeVersion(version)
        }
        merged.version = version
        normalizeTombstone(&merged, retentionFloor: max(
            existing.tombstone?.purgeAfterMilliseconds ?? 0,
            incoming.tombstone?.purgeAfterMilliseconds ?? 0
        ))
        try merged.validate()
        return merged
    }

    static func stampLocal(previous: BookmarkRecord?, candidate: BookmarkRecord) -> BookmarkRecord {
        var result = candidate
        var version = candidate.version.normalized(for: BookmarkRecord.syncFields)
        if let previous {
            let old = previous.version.normalized(for: BookmarkRecord.syncFields)
            let unchanged = [
                "location": Location(previous) == Location(candidate),
                "kind": previous.kind == candidate.kind,
                "title": previous.title == candidate.title,
                "url": previous.url == candidate.url,
                "created_at": previous.createdAt == candidate.createdAt,
                "tombstone": previous.isDeleted == candidate.isDeleted,
            ]
            for field in BookmarkRecord.syncFields where unchanged[field] == true {
                version.fieldVersions[field] = old.fieldVersions[field]
            }
        }
        result.version = version
        normalizeTombstone(&result, retentionFloor: candidate.tombstone?.purgeAfterMilliseconds ?? 0)
        return result
    }

    private static func normalizeTombstone(_ value: inout BookmarkRecord, retentionFloor: UInt64) {
        guard value.isDeleted else { return }
        // The causal delete remains in fieldVersions["tombstone"]. The outer
        // envelope metadata follows the merged record authority, as on Desktop.
        let deletedAt = value.version.modifiedAt
        let (purgeAfter, overflow) = deletedAt.physicalMilliseconds.addingReportingOverflow(
            30 * 24 * 60 * 60 * 1_000
        )
        value.tombstone = Tombstone(
            entityID: value.id.rawValue, deletedAt: deletedAt,
            deletedBy: value.version.modifiedBy, originalParentID: value.parentID?.rawValue,
            originalOrderKey: nil,
            purgeAfterMilliseconds: max(retentionFloor, overflow ? UInt64.max : purgeAfter)
        )
    }

    private static func sameProjection(
        _ lhs: BookmarkRecord, _ lhsVersion: SyncVersion,
        _ rhs: BookmarkRecord, _ rhsVersion: SyncVersion
    ) -> Bool {
        Location(lhs) == Location(rhs) && lhs.kind == rhs.kind && lhs.title == rhs.title &&
            lhs.url == rhs.url && lhs.createdAt == rhs.createdAt && lhs.isDeleted == rhs.isDeleted &&
            lhsVersion.fieldVersions == rhsVersion.fieldVersions
    }

    private struct Location: Equatable {
        let root: BookmarkRoot?
        let parent: BookmarkID?
        let order: String

        init(_ value: BookmarkRecord) {
            root = value.rootKind
            parent = value.parentID
            order = value.sortKey
        }
    }
}
