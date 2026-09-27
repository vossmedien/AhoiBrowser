import Foundation
import AhoiCloudKitSpike

public struct CompanionSnapshot: Codable, Equatable, Sendable {
    public static let remoteSessionVisibleAgeMilliseconds: UInt64 =
        7 * 24 * 60 * 60 * 1_000
    public static let remoteSessionActionableAgeMilliseconds: UInt64 =
        15 * 60 * 1_000

    public var devices: [Device]
    public var workspaces: [Workspace]
    public var treeNodes: [TreeNode]
    public var sessions: [DeviceSession]
    public var remoteTabs: [RemoteTab]
    public var history: [HistoryVisit]
    public var productRecords: CompanionProductSnapshot
    public var bookmarks: [BookmarkRecord]
    public var deviceCapabilities: [DeviceCapabilityRecord]
    public var splitGroups: [SplitGroupRecord]
    public var archiveEntries: [TabArchiveEntryRecord]
    /// Local idempotency receipts; never emitted by a wire codec/SyncBridge.
    public var mobileAppliedIntents: Set<UUID>

    public init(
        devices: [Device] = [],
        workspaces: [Workspace] = [],
        treeNodes: [TreeNode] = [],
        sessions: [DeviceSession] = [],
        remoteTabs: [RemoteTab] = [],
        history: [HistoryVisit] = [],
        productRecords: CompanionProductSnapshot = .empty,
        bookmarks: [BookmarkRecord] = [],
        deviceCapabilities: [DeviceCapabilityRecord] = [],
        splitGroups: [SplitGroupRecord] = [],
        archiveEntries: [TabArchiveEntryRecord] = [],
        mobileAppliedIntents: Set<UUID> = []
    ) {
        self.devices = devices
        self.workspaces = workspaces
        self.treeNodes = treeNodes
        self.sessions = sessions
        self.remoteTabs = remoteTabs
        self.history = history
        self.productRecords = productRecords
        self.bookmarks = bookmarks
        self.deviceCapabilities = deviceCapabilities
        self.splitGroups = splitGroups
        self.archiveEntries = archiveEntries
        self.mobileAppliedIntents = mobileAppliedIntents
    }

    public static let empty = Self()

    public var visibleWorkspaces: [Workspace] {
        workspaces.filter { !$0.isDeleted }.sorted {
            if $0.sortKey != $1.sortKey { return $0.sortKey < $1.sortKey }
            return $0.id < $1.id
        }
    }

    public var visibleTreeNodes: [TreeNode] {
        treeNodesForPresentation.filter { !$0.isDeleted }.sorted {
            if $0.syncSortKey != $1.syncSortKey {
                return $0.syncSortKey < $1.syncSortKey
            }
            return $0.id < $1.id
        }
    }

    /// A merge tombstone changes where a late offline node is presented, not
    /// the authoritative location register received from its writer. Keep raw
    /// `treeNodes` for persistence, field merge and outbound serialization.
    /// Recomputing this projection also handles target-before-source delivery,
    /// merge chains and a later undo without minting a competing wire clock.
    public var treeNodesForPresentation: [TreeNode] {
        let destinations = mergedWorkspaceDestinations
        guard !destinations.isEmpty else { return treeNodes }
        var projected = treeNodes.map { node in
            var result = node
            if !node.isDeleted, let destination = destinations[node.workspaceID] {
                result.workspaceID = destination
            }
            return result
        }
        let byID = Dictionary(grouping: projected, by: \.id)
        for index in projected.indices {
            let original = treeNodes[index]
            guard !original.isDeleted,
                  projected[index].workspaceID != original.workspaceID,
                  let parentID = original.parentID else { continue }
            // Preserve a valid subtree whose parent followed the same merge
            // (or was already moved). A missing/cross-workspace parent cannot
            // hide the late node in the destination's Library.
            let parents = byID[parentID] ?? []
            if parents.count != 1 || parents[0].isDeleted ||
                parents[0].kind != .folder ||
                parents[0].workspaceID != projected[index].workspaceID {
                projected[index].parentID = nil
            }
        }
        return projected
    }

    func presentationNode(_ id: TreeNodeID) -> TreeNode? {
        let matches = treeNodesForPresentation.filter { $0.id == id }
        return matches.count == 1 ? matches.first : nil
    }

    /// Nil means the original Workspace and its merge chain have no known,
    /// unambiguous live destination. Missing targets may arrive in a later
    /// fetch; cycles and ordinary deletion must not fabricate a destination.
    func liveWorkspaceDestination(_ id: WorkspaceID) -> WorkspaceID? {
        let byID = Dictionary(grouping: workspaces, by: \.id)
        return Self.liveWorkspaceDestination(id, in: byID)
    }

    private var mergedWorkspaceDestinations: [WorkspaceID: WorkspaceID] {
        let byID = Dictionary(grouping: workspaces, by: \.id)
        var result: [WorkspaceID: WorkspaceID] = [:]
        for workspace in workspaces where workspace.isDeleted && workspace.mergedInto != nil {
            if let destination = Self.liveWorkspaceDestination(workspace.id, in: byID) {
                result[workspace.id] = destination
            }
        }
        return result
    }

    private static func liveWorkspaceDestination(
        _ id: WorkspaceID, in workspaces: [WorkspaceID: [Workspace]]
    ) -> WorkspaceID? {
        var cursor = id
        var visited = Set<WorkspaceID>()
        while visited.insert(cursor).inserted {
            guard let matches = workspaces[cursor], matches.count == 1,
                  let workspace = matches.first else { return nil }
            if !workspace.isDeleted { return workspace.id }
            guard let target = workspace.mergedInto else { return nil }
            cursor = target
        }
        return nil
    }

    public var visibleRemoteTabs: [RemoteTab] {
        visibleRemoteTabs(atMilliseconds: Self.nowMilliseconds())
    }

    public func visibleRemoteTabs(atMilliseconds now: UInt64) -> [RemoteTab] {
        let cutoff = now > Self.remoteSessionVisibleAgeMilliseconds
            ? now - Self.remoteSessionVisibleAgeMilliseconds
            : 0
        var liveSessions: [DeviceSessionID: DeviceSession] = [:]
        let permittedDeviceIDs = Set(devices.lazy.filter {
            !$0.isDeleted && !$0.isRevoked
        }.map(\.id))
        for session in sessions where !session.isDeleted && session.isOnline &&
            session.lastActiveAt.physicalMilliseconds >= cutoff {
            if liveSessions[session.id].map({
                $0.version.modifiedAt >= session.version.modifiedAt
            }) != true {
                liveSessions[session.id] = session
            }
        }
        return remoteTabs.filter { tab in
            guard !tab.isDeleted, tab.context == .normal,
                  permittedDeviceIDs.contains(tab.deviceID),
                  let session = liveSessions[tab.sessionID] else {
                return false
            }
            return session.deviceID == tab.deviceID
        }
            .sorted { $0.lastActiveAt > $1.lastActiveAt }
    }

    public func isRemoteTabActionable(
        _ tab: RemoteTab,
        atMilliseconds suppliedNow: UInt64? = nil
    ) -> Bool {
        let now = suppliedNow ?? Self.nowMilliseconds()
        let cutoff = now > Self.remoteSessionActionableAgeMilliseconds
            ? now - Self.remoteSessionActionableAgeMilliseconds
            : 0
        guard devices.contains(where: {
            $0.id == tab.deviceID && !$0.isDeleted && !$0.isRevoked
        }) else { return false }
        return sessions.contains {
            $0.id == tab.sessionID && $0.deviceID == tab.deviceID &&
                !$0.isDeleted && $0.isOnline &&
                $0.lastActiveAt.physicalMilliseconds >= cutoff
        }
    }

    public var visibleHistory: [HistoryVisit] {
        history.filter { !$0.isDeleted }.sorted { $0.visitedAt > $1.visitedAt }
    }

    private static func nowMilliseconds() -> UInt64 {
        UInt64(Date().timeIntervalSince1970 * 1_000)
    }
}

public struct LocalMobileTabPublication: Sendable {
    public let device: Device
    public let session: DeviceSession
    public let tab: RemoteTab
}

public struct LocalMobileSessionPublication: Sendable {
    public let device: Device
    public let session: DeviceSession
}

public protocol LocalCompanionStore: Sendable {
    func load() async throws -> CompanionSnapshot
    func save(_ snapshot: CompanionSnapshot) async throws
}

public enum LocalCompanionStoreError: Error, Equatable, Sendable {
    case invalidSnapshot
    case notFound
    case invalidParent
    case treeCycle
    case hierarchyTooDeep
    /// A merge undo whose records changed after the merge (ADR 0012).
    case mergeUndoOutdated
}

public enum CompanionHierarchyPolicy {
    /// Keeps drag targets and indentation usable while bounding parent-chain
    /// validation. Remote/corrupt trees are still rendered iteratively with a
    /// capped visual depth, so they cannot overflow the process stack.
    public static let maximumDepth = 64
}
