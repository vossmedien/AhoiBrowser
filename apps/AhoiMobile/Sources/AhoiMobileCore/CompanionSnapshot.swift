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

    public init(
        devices: [Device] = [],
        workspaces: [Workspace] = [],
        treeNodes: [TreeNode] = [],
        sessions: [DeviceSession] = [],
        remoteTabs: [RemoteTab] = [],
        history: [HistoryVisit] = [],
        productRecords: CompanionProductSnapshot = .empty,
        bookmarks: [BookmarkRecord] = []
    ) {
        self.devices = devices
        self.workspaces = workspaces
        self.treeNodes = treeNodes
        self.sessions = sessions
        self.remoteTabs = remoteTabs
        self.history = history
        self.productRecords = productRecords
        self.bookmarks = bookmarks
    }

    private enum CodingKeys: String, CodingKey {
        case devices, workspaces, treeNodes, sessions, remoteTabs, history, productRecords, bookmarks
    }

    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        let bookmarks = values.contains(.bookmarks)
            ? try values.decode([BookmarkRecord].self, forKey: .bookmarks) : []
        self.init(
            devices: try values.decode([Device].self, forKey: .devices),
            workspaces: try values.decode([Workspace].self, forKey: .workspaces),
            treeNodes: try values.decode([TreeNode].self, forKey: .treeNodes),
            sessions: try values.decode([DeviceSession].self, forKey: .sessions),
            remoteTabs: try values.decode([RemoteTab].self, forKey: .remoteTabs),
            history: try values.decode([HistoryVisit].self, forKey: .history),
            productRecords: try values.decode(CompanionProductSnapshot.self, forKey: .productRecords),
            bookmarks: bookmarks
        )
        try CompanionBookmarkHierarchy.validate(bookmarks)
    }

    public static let empty = Self()

    public var visibleWorkspaces: [Workspace] {
        workspaces.filter { !$0.isDeleted }.sorted {
            if $0.sortKey != $1.sortKey { return $0.sortKey < $1.sortKey }
            return $0.id < $1.id
        }
    }

    public var visibleTreeNodes: [TreeNode] {
        treeNodes.filter { !$0.isDeleted }.sorted {
            if $0.syncSortKey != $1.syncSortKey {
                return $0.syncSortKey < $1.syncSortKey
            }
            return $0.id < $1.id
        }
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
}

public enum CompanionHierarchyPolicy {
    /// Keeps drag targets and indentation usable while bounding parent-chain
    /// validation. Remote/corrupt trees are still rendered iteratively with a
    /// capped visual depth, so they cannot overflow the process stack.
    public static let maximumDepth = 64
}
