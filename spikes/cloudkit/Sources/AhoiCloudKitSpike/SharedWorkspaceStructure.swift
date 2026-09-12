import Foundation

/// Portable metadata only; this does not activate split/archive transport or UI.
public enum SharedSplitAxis: Int, Codable, Hashable, Sendable {
    case horizontal = 0, vertical = 1
}

public enum SharedSplitArrangement: Int, Codable, Hashable, Sendable {
    case linear = 0, mainStart = 1, mainEnd = 2
}

public enum SharedArchivePolicy: Int, Codable, Hashable, Sendable {
    case never = 0, twelveHours = 1, twentyFourHours = 2, sevenDays = 3, thirtyDays = 4
}

public enum SharedArchiveReason: Int, Codable, Hashable, Sendable {
    case automatic = 0, manual = 1
}

/// Integers preserve exact C++/Swift bytes. Native capture normalizes once;
/// the wire boundary must reject ratios outside 0...scale, fractions and Bool.
public struct SharedSplitRatios: Codable, Hashable, Sendable {
    public static let scale: UInt32 = 1_000_000
    public let primary: UInt32
    public let secondary: UInt32
}

/// Atomic membership/order/layout. Four panes are row-major TL, TR, BL, BR;
/// two/four panes use .linear. Divider edits have a separate atomic field clock.
public struct SharedSplitTopology: Codable, Hashable, Sendable {
    public let memberIDs: [TreeNodeID]
    public let axis: SharedSplitAxis
    public let arrangement: SharedSplitArrangement
}

public struct SharedSplitMetadata: Codable, Hashable, Sendable {
    public let id: UUID
    public let workspaceID: WorkspaceID
    public let topology: SharedSplitTopology
    public let ratios: SharedSplitRatios
}

public struct SharedArchivePageSnapshot: Codable, Hashable, Sendable {
    public let treeNodeID: TreeNodeID
    public let parentID: TreeNodeID?
    public let sortKey: String
    public let title: String
    public let target: SharedTabTarget
    public let homeTarget: SharedTabTarget?
}

/// The existing domain store holds either one page or an entire 2...4-page
/// split. Snapshot references never authorize replacing active native pages.
public struct SharedArchiveSnapshot: Codable, Hashable, Sendable {
    public let workspaceID: WorkspaceID
    public let pages: [SharedArchivePageSnapshot]
    public let split: SharedSplitMetadata?
}
