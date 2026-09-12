import AhoiCloudKitSpike
import Foundation

enum CompanionWorkspaceStructureMerge {
  static func merge(_ old: SplitGroupRecord, _ new: SplitGroupRecord) throws -> SplitGroupRecord {
    try old.validate()
    try new.validate()
    guard old.id == new.id else { throw CompanionFieldMergeError.identityMismatch }
    var r = old
    if try wins("workspace_id", old.workspaceID, new.workspaceID, old.version, new.version) {
      r.workspaceID = new.workspaceID
    }
    if try wins("topology", old.topology, new.topology, old.version, new.version) {
      r.topology = new.topology
    }
    if try wins("ratios", old.ratios, new.ratios, old.version, new.version) {
      r.ratios = new.ratios
    }
    if try wins("tombstone", old.isDeleted, new.isDeleted, old.version, new.version) {
      r.tombstone = new.tombstone
    }
    var v = CompanionFieldMerge.mergedVersion(
      old.version, new.version, fields: SplitGroupRecord.syncFields)
    func same(_ x: SplitGroupRecord) -> Bool {
      r.workspaceID == x.workspaceID && r.topology == x.topology && r.ratios == x.ratios
        && r.isDeleted == x.isDeleted && v.fieldVersions == x.version.fieldVersions
    }
    if !same(old), !same(new) { v = try CompanionFieldMerge.dominatingMergeVersion(v) }
    r.version = v
    r.tombstone = reframe(r.tombstone, id: r.id, version: v)
    try r.validate()
    return r
  }
  static func merge(_ old: TabArchiveEntryRecord, _ new: TabArchiveEntryRecord) throws
    -> TabArchiveEntryRecord
  {
    try old.validate()
    try new.validate()
    guard old.id == new.id else { throw CompanionFieldMergeError.identityMismatch }
    var r = old
    if try wins("snapshot", old.snapshot, new.snapshot, old.version, new.version) {
      r.snapshot = new.snapshot
    }
    if try wins("state", State(old), State(new), old.version, new.version) {
      r.reason = new.reason
      r.archivedAtWindowsMicroseconds = new.archivedAtWindowsMicroseconds
      r.restored = new.restored
    }
    let newDeleteWins = try wins(
      "tombstone", old.isDeleted, new.isDeleted, old.version, new.version)
    if old.isDeleted != new.isDeleted {
      r.tombstone = old.tombstone ?? new.tombstone
    } else if newDeleteWins {
      r.tombstone = new.tombstone
    }
    var v = CompanionFieldMerge.mergedVersion(
      old.version, new.version, fields: TabArchiveEntryRecord.syncFields)
    if old.isDeleted != new.isDeleted {
      var fields = v.fieldVersions
      fields["tombstone"] = (old.isDeleted ? old.version : new.version).fieldVersions["tombstone"]
      v = SyncVersion(
        schemaVersion: v.schemaVersion, modifiedAt: v.modifiedAt, modifiedBy: v.modifiedBy,
        fieldVersions: fields)
    }
    func same(_ x: TabArchiveEntryRecord) -> Bool {
      r.snapshot == x.snapshot && State(r) == State(x) && r.isDeleted == x.isDeleted
        && v.fieldVersions == x.version.fieldVersions
    }
    if !same(old), !same(new) { v = try CompanionFieldMerge.dominatingMergeVersion(v) }
    r.version = v
    r.tombstone = reframe(r.tombstone, id: r.id, version: v)
    try r.validate()
    return r
  }
  private struct State: Equatable {
    let reason: SharedArchiveReason
    let time: Int64
    let restored: Bool
    init(_ x: TabArchiveEntryRecord) {
      reason = x.reason
      time = x.archivedAtWindowsMicroseconds
      restored = x.restored
    }
  }
  private static func wins<T: Equatable>(
    _ f: String, _ old: T, _ new: T, _ a: SyncVersion, _ b: SyncVersion
  ) throws -> Bool {
    try CompanionFieldMerge.incomingWins(f, old, new, a, b)
  }
  private static func reframe(_ t: Tombstone?, id: UUID, version: SyncVersion) -> Tombstone? {
    guard let t else { return nil }
    let (time, overflow) = version.modifiedAt.physicalMilliseconds.addingReportingOverflow(
      2_592_000_000)
    return Tombstone(
      entityID: id, deletedAt: version.modifiedAt, deletedBy: version.modifiedBy,
      originalParentID: nil, originalOrderKey: nil,
      purgeAfterMilliseconds: max(t.purgeAfterMilliseconds, overflow ? UInt64.max : time))
  }
}
