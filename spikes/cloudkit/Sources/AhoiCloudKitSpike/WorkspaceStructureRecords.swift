import Foundation

public struct SplitGroupRecord: Codable, Hashable, Sendable, Identifiable {
  public static let syncFields: Set<String> = ["workspace_id", "topology", "ratios", "tombstone"]
  public let id: UUID
  public var workspaceID: WorkspaceID
  public var topology: SharedSplitTopology
  public var ratios: SharedSplitRatios
  public var version: SyncVersion
  public var tombstone: Tombstone?
  public var isDeleted: Bool { tombstone != nil }
  public init(
    id: UUID, workspaceID: WorkspaceID, topology: SharedSplitTopology,
    ratios: SharedSplitRatios, version: SyncVersion, tombstone: Tombstone? = nil
  ) throws {
    self.id = id
    self.workspaceID = workspaceID
    self.topology = topology
    self.ratios = ratios
    self.version = version
    self.tombstone = tombstone
    try validate()
  }
  public func validate() throws {
    try SharedSplitMetadata(id: id, workspaceID: workspaceID, topology: topology, ratios: ratios)
      .validate()
    try SharedWorkspaceRecordValidation.validate(
      id: id, version: version, fields: Self.syncFields, tombstone: tombstone)
  }
  private enum CodingKeys: String, CodingKey {
    case id, workspaceID, topology, ratios, version, tombstone
  }
  public init(from decoder: Decoder) throws {
    let c = try decoder.container(keyedBy: CodingKeys.self)
    try self.init(
      id: c.decode(UUID.self, forKey: .id),
      workspaceID: c.decode(WorkspaceID.self, forKey: .workspaceID),
      topology: c.decode(SharedSplitTopology.self, forKey: .topology),
      ratios: c.decode(SharedSplitRatios.self, forKey: .ratios),
      version: c.decode(SyncVersion.self, forKey: .version),
      tombstone: c.decodeIfPresent(Tombstone.self, forKey: .tombstone))
  }
}

public struct TabArchiveEntryRecord: Codable, Hashable, Sendable, Identifiable {
  public static let syncFields: Set<String> = ["snapshot", "state", "tombstone"]
  public let id: UUID
  public var snapshot: SharedArchiveSnapshot
  public var reason: SharedArchiveReason
  public var archivedAtWindowsMicroseconds: Int64
  public var restored: Bool
  public var version: SyncVersion
  public var tombstone: Tombstone?
  public var isDeleted: Bool { tombstone != nil }
  public init(
    id: UUID, snapshot: SharedArchiveSnapshot, reason: SharedArchiveReason,
    archivedAtWindowsMicroseconds: Int64, restored: Bool, version: SyncVersion,
    tombstone: Tombstone? = nil
  ) throws {
    self.id = id
    self.snapshot = snapshot
    self.reason = reason
    self.archivedAtWindowsMicroseconds = archivedAtWindowsMicroseconds
    self.restored = restored
    self.version = version
    self.tombstone = tombstone
    try validate()
  }
  public func validate() throws {
    try snapshot.validate()
    guard id == (try snapshot.archiveID()), archivedAtWindowsMicroseconds >= 11_644_473_600_000_000 else {
      throw SharedWorkspaceValidation.Error.invalidStructure
    }
    try SharedWorkspaceRecordValidation.validate(
      id: id, version: version, fields: Self.syncFields, tombstone: tombstone)
  }
  private enum CodingKeys: String, CodingKey {
    case id, snapshot, reason, archivedAtWindowsMicroseconds, restored, version, tombstone
  }
  public init(from decoder: Decoder) throws {
    let c = try decoder.container(keyedBy: CodingKeys.self)
    try self.init(
      id: c.decode(UUID.self, forKey: .id),
      snapshot: c.decode(SharedArchiveSnapshot.self, forKey: .snapshot),
      reason: c.decode(SharedArchiveReason.self, forKey: .reason),
      archivedAtWindowsMicroseconds: c.decode(Int64.self, forKey: .archivedAtWindowsMicroseconds),
      restored: c.decode(Bool.self, forKey: .restored),
      version: c.decode(SyncVersion.self, forKey: .version),
      tombstone: c.decodeIfPresent(Tombstone.self, forKey: .tombstone))
  }
}

private enum SharedWorkspaceRecordValidation {
  static func validate(id: UUID, version: SyncVersion, fields: Set<String>, tombstone: Tombstone?)
    throws
  {
    try SharedSyncFormat.validate(version, fields: fields)
    if let tombstone {
      guard tombstone.entityID == id, tombstone.deletedAt == version.modifiedAt,
        tombstone.deletedBy == version.modifiedBy,
        tombstone.purgeAfterMilliseconds > tombstone.deletedAt.physicalMilliseconds
      else {
        throw SharedSyncFormatError.invalidClock
      }
    }
  }
}
