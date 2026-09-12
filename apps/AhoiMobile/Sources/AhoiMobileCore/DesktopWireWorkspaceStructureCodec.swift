import AhoiCloudKitSpike
import Foundation

extension DesktopWirePayloadCodec {
  private static let structureCommonKeys: Set<String> = [
    "model_version", "id", "tombstone", "version_model",
    "version_physical", "version_logical", "version_device", "field_versions",
  ]

  public func encode(_ value: SplitGroupRecord) throws -> Data {
    try value.validate()
    var d = try common(
      id: value.id, tombstone: value.isDeleted, version: value.version,
      fields: SplitGroupRecord.syncFields)
    d["workspace_id"] = uuid(value.workspaceID.rawValue)
    d["topology"] = structureTopology(value.topology)
    d["ratios"] = structureRatios(value.ratios)
    return try serialize(d)
  }
  public func decodeSplitGroup(_ record: SyncRecord, plaintext: Data) throws -> SplitGroupRecord {
    let d = try object(from: plaintext)
    try structureKeys(d, Self.structureCommonKeys.union(["workspace_id", "topology", "ratios"]))
    let v = try version(d, requiredFields: SplitGroupRecord.syncFields)
    let value = try SplitGroupRecord(
      id: id(d), workspaceID: WorkspaceID(rawValue: uuid(d, "workspace_id")),
      topology: readStructureTopology(structureDict(d, "topology")),
      ratios: readStructureRatios(structureDict(d, "ratios")),
      version: v, tombstone: tombstone(record, value: d))
    try validatePortableEnvelope(
      record, dataClass: .splitGroup, identity: value.id, version: v,
      tombstone: value.tombstone, requiresAbsentOrderKey: true)
    return value
  }
  public func encode(_ value: TabArchiveEntryRecord) throws -> Data {
    try value.validate()
    var d = try common(
      id: value.id, tombstone: value.isDeleted, version: value.version,
      fields: TabArchiveEntryRecord.syncFields)
    d["snapshot"] = structureSnapshot(value.snapshot)
    d["reason"] = value.reason.rawValue
    d["archived_at"] = String(value.archivedAtWindowsMicroseconds)
    d["restored"] = value.restored
    let data = try serialize(d)
    guard data.count <= 512 * 1_024 else { throw DesktopWirePayloadCodecError.malformedPayload }
    return data
  }
  public func decodeArchiveEntry(_ record: SyncRecord, plaintext: Data) throws
    -> TabArchiveEntryRecord
  {
    guard plaintext.count <= 512 * 1_024 else { throw DesktopWirePayloadCodecError.malformedPayload }
    let d = try object(from: plaintext)
    try structureKeys(
      d, Self.structureCommonKeys.union(["snapshot", "reason", "archived_at", "restored"]))
    let v = try version(d, requiredFields: TabArchiveEntryRecord.syncFields)
    guard let reason = try SharedArchiveReason(rawValue: integer(d, "reason")),
      let timestamp = d["archived_at"] as? String, let time = Int64(timestamp),
      String(time) == timestamp
    else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    let value = try TabArchiveEntryRecord(
      id: id(d), snapshot: readStructureSnapshot(structureDict(d, "snapshot")),
      reason: reason, archivedAtWindowsMicroseconds: time,
      restored: SharedTabWireReadPolicy.strictBoolean(d, key: "restored"),
      version: v, tombstone: tombstone(record, value: d))
    try validatePortableEnvelope(
      record, dataClass: .tabArchiveEntry, identity: value.id, version: v,
      tombstone: value.tombstone, requiresAbsentOrderKey: true)
    return value
  }
  func encodeHomeTarget(_ target: SharedTabTarget?, into d: inout [String: Any]) throws {
    try SharedWorkspaceValidation.home(target)
    d["home_url"] = target.map { $0.url as Any } ?? NSNull()
    d["home_target_kind"] = target.map { $0.kind.rawValue as Any } ?? NSNull()
    d["home_local_scheme"] = target?.localScheme.map { $0.rawValue as Any } ?? NSNull()
  }
  func decodeHomeTarget(_ d: [String: Any]) throws -> SharedTabTarget? {
    guard let url = d["home_url"], let kind = d["home_target_kind"],
      let scheme = d["home_local_scheme"]
    else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    if url is NSNull, kind is NSNull, scheme is NSNull { return nil }
    let result = try readStructureTarget(["url": url, "target_kind": kind, "local_scheme": scheme])
    try SharedWorkspaceValidation.home(result)
    return result
  }
  func decodeArchivePolicy(_ d: [String: Any]) throws -> SharedArchivePolicy {
    guard let policy = try SharedArchivePolicy(rawValue: integer(d, "archive_policy")) else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    return policy
  }
  private func structureTarget(_ t: SharedTabTarget) -> [String: Any] {
    [
      "url": t.url, "target_kind": t.kind.rawValue,
      "local_scheme": t.localScheme.map { $0.rawValue as Any } ?? NSNull(),
    ]
  }
  private func readStructureTarget(_ d: [String: Any]) throws -> SharedTabTarget {
    try structureKeys(d, ["url", "target_kind", "local_scheme"])
    guard let kind = try SharedTabTargetKind(rawValue: integer(d, "target_kind")) else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    let scheme: SharedTabLocalScheme?
    if d["local_scheme"] is NSNull {
      scheme = nil
    } else {
      guard let raw = d["local_scheme"] as? String, let parsed = SharedTabLocalScheme(rawValue: raw)
      else {
        throw DesktopWirePayloadCodecError.malformedPayload
      }
      scheme = parsed
    }
    return try SharedTabTarget(kind: kind, url: string(d, "url"), localScheme: scheme)
  }
  private func structureTopology(_ t: SharedSplitTopology) -> [String: Any] {
    [
      "member_ids": t.memberIDs.map { uuid($0.rawValue) }, "axis": t.axis.rawValue,
      "arrangement": t.arrangement.rawValue,
    ]
  }
  private func readStructureTopology(_ d: [String: Any]) throws -> SharedSplitTopology {
    try structureKeys(d, ["member_ids", "axis", "arrangement"])
    guard let raw = d["member_ids"] as? [String], (2...4).contains(raw.count),
      let axis = try SharedSplitAxis(rawValue: integer(d, "axis")),
      let arrangement = try SharedSplitArrangement(rawValue: integer(d, "arrangement"))
    else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    return try SharedSplitTopology(
      memberIDs: raw.map {
        TreeNodeID(rawValue: try SharedTabWireReadPolicy.strictUUID(["id": $0], key: "id"))
      },
      axis: axis, arrangement: arrangement)
  }
  private func structureRatios(_ r: SharedSplitRatios) -> [String: Any] {
    ["primary": Int(r.primary), "secondary": Int(r.secondary)]
  }
  private func readStructureRatios(_ d: [String: Any]) throws -> SharedSplitRatios {
    try structureKeys(d, ["primary", "secondary"])
    return try SharedSplitRatios(
      primary: SharedTabWireReadPolicy.strictUInt32(d, key: "primary"),
      secondary: SharedTabWireReadPolicy.strictUInt32(d, key: "secondary"))
  }
  private func structureSplit(_ s: SharedSplitMetadata) -> [String: Any] {
    [
      "id": uuid(s.id), "workspace_id": uuid(s.workspaceID.rawValue),
      "topology": structureTopology(s.topology), "ratios": structureRatios(s.ratios),
    ]
  }
  private func readStructureSplit(_ d: [String: Any]) throws -> SharedSplitMetadata {
    try structureKeys(d, ["id", "workspace_id", "topology", "ratios"])
    let s = try SharedSplitMetadata(
      id: id(d), workspaceID: WorkspaceID(rawValue: uuid(d, "workspace_id")),
      topology: readStructureTopology(structureDict(d, "topology")),
      ratios: readStructureRatios(structureDict(d, "ratios")))
    try s.validate()
    return s
  }
  private func structureSnapshot(_ s: SharedArchiveSnapshot) -> [String: Any] {
    [
      "workspace_id": uuid(s.workspaceID.rawValue),
      "split": s.split.map { structureSplit($0) as Any } ?? NSNull(),
      "pages": s.pages.map { p -> [String: Any] in
        [
          "tree_node_id": uuid(p.treeNodeID.rawValue),
          "parent_id": p.parentID.map { uuid($0.rawValue) as Any } ?? NSNull(),
          "sort_key": p.sortKey, "title": p.title,
          "target": structureTarget(p.target),
          "home_target": p.homeTarget.map { structureTarget($0) as Any } ?? NSNull(),
        ]
      },
    ]
  }
  private func readStructureSnapshot(_ d: [String: Any]) throws -> SharedArchiveSnapshot {
    try structureKeys(d, ["workspace_id", "split", "pages"])
    guard let pages = d["pages"] as? [[String: Any]], (1...4).contains(pages.count) else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    let parsed = try pages.map { p -> SharedArchivePageSnapshot in
      try structureKeys(
        p, ["tree_node_id", "parent_id", "sort_key", "title", "target", "home_target"])
      return try SharedArchivePageSnapshot(
        treeNodeID: TreeNodeID(rawValue: uuid(p, "tree_node_id")),
        parentID: p["parent_id"] is NSNull ? nil : TreeNodeID(rawValue: uuid(p, "parent_id")),
        sortKey: string(p, "sort_key"), title: string(p, "title"),
        target: readStructureTarget(structureDict(p, "target")),
        homeTarget: p["home_target"] is NSNull
          ? nil : readStructureTarget(structureDict(p, "home_target")))
    }
    let s = try SharedArchiveSnapshot(
      workspaceID: WorkspaceID(rawValue: uuid(d, "workspace_id")), pages: parsed,
      split: d["split"] is NSNull ? nil : readStructureSplit(structureDict(d, "split")))
    try s.validate()
    return s
  }
  private func structureDict(_ d: [String: Any], _ key: String) throws -> [String: Any] {
    guard let result = d[key] as? [String: Any] else {
      throw DesktopWirePayloadCodecError.malformedPayload
    }
    return result
  }
  private func structureKeys(_ d: [String: Any], _ keys: Set<String>) throws {
    guard Set(d.keys) == keys else { throw DesktopWirePayloadCodecError.malformedPayload }
  }
}
