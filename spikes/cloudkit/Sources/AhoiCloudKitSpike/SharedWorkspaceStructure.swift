import Foundation

/// Portable metadata only; this does not activate split/archive transport or UI.
public enum SharedSplitAxis: Int, Codable, Hashable, Sendable {
  case horizontal = 0
  case vertical = 1
}

public enum SharedSplitArrangement: Int, Codable, Hashable, Sendable {
  case linear = 0
  case mainStart = 1
  case mainEnd = 2
}

public enum SharedArchivePolicy: Int, Codable, Hashable, Sendable {
  case never = 0
  case twelveHours = 1
  case twentyFourHours = 2
  case sevenDays = 3
  case thirtyDays = 4
}

public enum SharedArchiveReason: Int, Codable, Hashable, Sendable {
  case automatic = 0
  case manual = 1
}

/// Integers preserve exact C++/Swift bytes. Native capture normalizes once;
/// the wire boundary must reject ratios outside 0...scale, fractions and Bool.
public struct SharedSplitRatios: Codable, Hashable, Sendable {
  public static let scale: UInt32 = 1_000_000
  public let primary: UInt32
  public let secondary: UInt32
  public init(primary: UInt32, secondary: UInt32) {
    self.primary = primary
    self.secondary = secondary
  }
}

/// Atomic membership/order/layout. Four panes are row-major TL, TR, BL, BR;
/// two/four panes use .linear. Divider edits have a separate atomic field clock.
public struct SharedSplitTopology: Codable, Hashable, Sendable {
  public let memberIDs: [TreeNodeID]
  public let axis: SharedSplitAxis
  public let arrangement: SharedSplitArrangement
  public init(memberIDs: [TreeNodeID], axis: SharedSplitAxis, arrangement: SharedSplitArrangement) {
    self.memberIDs = memberIDs
    self.axis = axis
    self.arrangement = arrangement
  }
}

public struct SharedSplitMetadata: Codable, Hashable, Sendable {
  public let id: UUID
  public let workspaceID: WorkspaceID
  public let topology: SharedSplitTopology
  public let ratios: SharedSplitRatios
  public init(
    id: UUID, workspaceID: WorkspaceID, topology: SharedSplitTopology, ratios: SharedSplitRatios
  ) {
    self.id = id
    self.workspaceID = workspaceID
    self.topology = topology
    self.ratios = ratios
  }
  public func validate() throws {
    let count = topology.memberIDs.count
    guard SharedWorkspaceValidation.id(id), SharedWorkspaceValidation.id(workspaceID.rawValue),
      (2...4).contains(count), Set(topology.memberIDs).count == count,
      id != workspaceID.rawValue, !topology.memberIDs.contains(where: { $0.rawValue == id }),
      topology.memberIDs.allSatisfy({ SharedWorkspaceValidation.id($0.rawValue) }),
      count == 3 || topology.arrangement == .linear,
      ratios.primary <= SharedSplitRatios.scale, ratios.secondary <= SharedSplitRatios.scale
    else { throw SharedWorkspaceValidation.Error.invalidStructure }
  }
}

public struct SharedArchivePageSnapshot: Codable, Hashable, Sendable {
  public let treeNodeID: TreeNodeID
  public let parentID: TreeNodeID?
  public let sortKey: String
  public let title: String
  public let target: SharedTabTarget
  public let homeTarget: SharedTabTarget?
  public init(
    treeNodeID: TreeNodeID, parentID: TreeNodeID?, sortKey: String, title: String,
    target: SharedTabTarget, homeTarget: SharedTabTarget?
  ) {
    self.treeNodeID = treeNodeID
    self.parentID = parentID
    self.sortKey = sortKey
    self.title = title
    self.target = target
    self.homeTarget = homeTarget
  }
}

/// The existing domain store holds either one page or an entire 2...4-page
/// split. Snapshot references never authorize replacing active native pages.
public struct SharedArchiveSnapshot: Codable, Hashable, Sendable {
  public let workspaceID: WorkspaceID
  public let pages: [SharedArchivePageSnapshot]
  public let split: SharedSplitMetadata?
  public init(
    workspaceID: WorkspaceID, pages: [SharedArchivePageSnapshot], split: SharedSplitMetadata?
  ) {
    self.workspaceID = workspaceID
    self.pages = pages
    self.split = split
  }
  public func validate() throws {
    guard SharedWorkspaceValidation.id(workspaceID.rawValue), (1...4).contains(pages.count),
      (pages.count == 1) == (split == nil), Set(pages.map(\.treeNodeID)).count == pages.count
    else {
      throw SharedWorkspaceValidation.Error.invalidStructure
    }
    for p in pages {
      guard SharedWorkspaceValidation.id(p.treeNodeID.rawValue),
        p.parentID.map({ SharedWorkspaceValidation.id($0.rawValue) && $0 != p.treeNodeID }) ?? true,
        !p.sortKey.isEmpty, p.sortKey.utf8.count <= 1_024,
        p.sortKey.utf8.allSatisfy({ $0 >= 0x21 && $0 <= 0x7e }),
        p.title.utf8.count <= 65_536, !p.title.contains("\0")
      else {
        throw SharedWorkspaceValidation.Error.invalidStructure
      }
      try p.target.validate()
      try SharedWorkspaceValidation.home(p.homeTarget)
    }
    if let split {
      try split.validate()
      guard split.workspaceID == workspaceID, split.topology.memberIDs == pages.map(\.treeNodeID)
      else {
        throw SharedWorkspaceValidation.Error.invalidStructure
      }
    }
  }
  public func archiveID() throws -> UUID {
    try validate()
    guard let first = pages.first else { throw SharedWorkspaceValidation.Error.invalidStructure }
    return SharedTabContract.archiveID(
      subject: split?.id ?? first.treeNodeID.rawValue, isSplit: split != nil)
  }
}

public enum SharedWorkspaceValidation {
  public enum Error: Swift.Error, Equatable { case invalidStructure }
  public static func id(_ value: UUID) -> Bool {
    value.uuidString != "00000000-0000-0000-0000-000000000000"
  }
  public static func home(_ value: SharedTabTarget?) throws {
    guard let value else { return }
    try value.validate()
    guard value.kind != .newTab else { throw Error.invalidStructure }
  }
}
