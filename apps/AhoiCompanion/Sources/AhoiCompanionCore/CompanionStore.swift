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

    public init(
        devices: [Device] = [],
        workspaces: [Workspace] = [],
        treeNodes: [TreeNode] = [],
        sessions: [DeviceSession] = [],
        remoteTabs: [RemoteTab] = [],
        history: [HistoryVisit] = []
    ) {
        self.devices = devices
        self.workspaces = workspaces
        self.treeNodes = treeNodes
        self.sessions = sessions
        self.remoteTabs = remoteTabs
        self.history = history
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

public protocol LocalCompanionStore: Sendable {
    func load() async throws -> CompanionSnapshot
    func save(_ snapshot: CompanionSnapshot) async throws
}

public enum LocalCompanionStoreError: Error, Equatable, Sendable {
    case invalidSnapshot
    case notFound
    case invalidParent
    case treeCycle
}

/// Deterministic local backend used by previews and tests. It has no network,
/// iCloud, or account dependency and exercises exactly the same repository
/// seam as the file-backed implementation.
public actor InMemoryCompanionStore: LocalCompanionStore {
    private var value: CompanionSnapshot

    public init(snapshot: CompanionSnapshot = .empty) {
        self.value = snapshot
    }

    public func load() async throws -> CompanionSnapshot {
        value
    }

    public func save(_ snapshot: CompanionSnapshot) async throws {
        value = snapshot
    }
}

/// Small, recoverable JSON persistence seam. A production target can replace
/// this with SwiftData or SQLite without changing the repository or CloudKit
/// provider. The app's container is expected to use Apple's file-protection
/// defaults; this type deliberately does not pretend to encrypt payloads.
public final class FileCompanionStore: LocalCompanionStore, @unchecked Sendable {
    private let fileURL: URL
    private let lock = NSLock()
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(fileURL: URL) {
        self.fileURL = fileURL
        self.encoder = JSONEncoder()
        self.decoder = JSONDecoder()
        self.encoder.outputFormatting = [.sortedKeys]
    }

    public func load() async throws -> CompanionSnapshot {
        try lock.withLock {
            guard FileManager.default.fileExists(atPath: fileURL.path) else {
                return .empty
            }
            let data = try Data(contentsOf: fileURL)
            return try decoder.decode(CompanionSnapshot.self, from: data)
        }
    }

    public func save(_ snapshot: CompanionSnapshot) async throws {
        let data = try encoder.encode(snapshot)
        try lock.withLock {
            let directory = fileURL.deletingLastPathComponent()
            try FileManager.default.createDirectory(
                at: directory,
                withIntermediateDirectories: true
            )
            try data.write(to: fileURL, options: [.atomic])
        }
    }
}

public enum CompanionSearchResultKind: String, Codable, Sendable {
    case workspace
    case folder
    case savedPage
    case remoteTab
    case history
}

public struct CompanionSearchResult: Codable, Equatable, Hashable, Sendable, Identifiable {
    public let id: UUID
    public let kind: CompanionSearchResultKind
    public let title: String
    public let detail: String
    public let url: String?
    public let deviceName: String?
    public let workspaceName: String?

    public init(
        id: UUID,
        kind: CompanionSearchResultKind,
        title: String,
        detail: String,
        url: String? = nil,
        deviceName: String? = nil,
        workspaceName: String? = nil
    ) {
        self.id = id
        self.kind = kind
        self.title = title
        self.detail = detail
        self.url = url
        self.deviceName = deviceName
        self.workspaceName = workspaceName
    }
}

/// Search stays local because the URL/title payloads are stored in
/// `CKRecord.encryptedValues` and therefore cannot be server-side queried.
public actor LocalSearchIndex {
    private var records: [CompanionSearchResult] = []

    public init() {}

    public func rebuild(snapshot: CompanionSnapshot) {
        var result: [CompanionSearchResult] = []
        result.reserveCapacity(
            snapshot.workspaces.count
                + snapshot.treeNodes.count
                + snapshot.remoteTabs.count
                + snapshot.history.count
        )

        for workspace in snapshot.visibleWorkspaces {
            result.append(.init(
                id: workspace.id.rawValue,
                kind: .workspace,
                title: workspace.name,
                detail: "Workspace"
            ))
        }
        for node in snapshot.visibleTreeNodes {
            result.append(.init(
                id: node.id.rawValue,
                kind: node.kind == .folder ? .folder : .savedPage,
                title: node.title,
                detail: node.kind == .folder ? "Ordner" : "Gespeicherte Seite",
                url: node.url,
                workspaceName: snapshot.workspaces.first { $0.id == node.workspaceID }?.name
            ))
        }
        for tab in snapshot.visibleRemoteTabs {
            result.append(.init(
                id: tab.id.rawValue,
                kind: .remoteTab,
                title: tab.title,
                detail: tab.url,
                url: tab.url,
                deviceName: tab.deviceName,
                workspaceName: tab.workspaceName
            ))
        }
        for visit in snapshot.visibleHistory {
            result.append(.init(
                id: visit.id.rawValue,
                kind: .history,
                title: visit.title,
                detail: visit.url,
                url: visit.url
            ))
        }
        records = result
    }

    public func search(_ query: String, limit: Int = 50) -> [CompanionSearchResult] {
        let normalizedQuery = query.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        guard !normalizedQuery.isEmpty else {
            return Array(records.prefix(limit))
        }

        return records.filter { record in
            [record.title, record.detail, record.url, record.deviceName, record.workspaceName]
                .compactMap { $0 }
                .contains {
                    $0.folding(
                        options: [.caseInsensitive, .diacriticInsensitive],
                        locale: .current
                    ).contains(normalizedQuery)
                }
        }.prefix(limit).map { $0 }
    }
}

public actor LocalFirstRepository {
    private let store: any LocalCompanionStore
    private let searchIndex = LocalSearchIndex()
    private let localDeviceID: DeviceID
    private var clock: HybridLogicalClock
    private var snapshot: CompanionSnapshot = .empty
    private var didLoad = false

    public init(
        store: any LocalCompanionStore,
        localDeviceID: DeviceID = DeviceID()
    ) {
        self.store = store
        self.localDeviceID = localDeviceID
        self.clock = HybridLogicalClock(
            physicalMilliseconds: UInt64(Date().timeIntervalSince1970 * 1_000),
            nodeID: localDeviceID
        )
    }

    public func load() async throws {
        guard !didLoad else { return }
        snapshot = try await store.load()
        observeStoredClocks()
        didLoad = true
        await searchIndex.rebuild(snapshot: snapshot)
    }

    public func currentSnapshot() async throws -> CompanionSnapshot {
        try await load()
        return snapshot
    }

    public func search(_ query: String, limit: Int = 50) async throws -> [CompanionSearchResult] {
        try await load()
        return await searchIndex.search(query, limit: limit)
    }

    public func replace(_ snapshot: CompanionSnapshot) async throws {
        try await store.save(snapshot)
        self.snapshot = snapshot
        didLoad = true
        await searchIndex.rebuild(snapshot: snapshot)
    }

    @discardableResult
    public func upsert(_ device: Device) async throws -> Device {
        try await load()
        let merged: Device
        if let current = snapshot.devices.first(where: { $0.id == device.id }) {
            merged = try CompanionReadModelFieldMerge.merge(current, device)
            snapshot.devices.replace(merged, where: { $0.id == device.id })
        } else {
            merged = device
            snapshot.devices.append(device)
        }
        try await persist()
        return merged
    }

    @discardableResult
    public func upsert(_ workspace: Workspace) async throws -> Workspace {
        try await load()
        let merged: Workspace
        if let current = snapshot.workspaces.first(where: { $0.id == workspace.id }) {
            merged = try CompanionFieldMerge.merge(current, workspace)
            snapshot.workspaces.replace(merged, where: { $0.id == workspace.id })
        } else {
            merged = workspace
            snapshot.workspaces.append(workspace)
        }
        try await persist()
        return merged
    }

    @discardableResult
    public func upsert(_ node: TreeNode) async throws -> TreeNode {
        try await load()
        let merged: TreeNode
        if let current = snapshot.treeNodes.first(where: { $0.id == node.id }) {
            merged = try CompanionFieldMerge.merge(current, node)
            snapshot.treeNodes.replace(merged, where: { $0.id == node.id })
        } else {
            merged = node
            snapshot.treeNodes.append(node)
        }
        try await persist()
        return merged
    }

    @discardableResult
    public func upsert(_ session: DeviceSession) async throws -> DeviceSession {
        try await load()
        let merged: DeviceSession
        if let current = snapshot.sessions.first(where: { $0.id == session.id }) {
            merged = try CompanionReadModelFieldMerge.merge(current, session)
            snapshot.sessions.replace(merged, where: { $0.id == session.id })
        } else {
            merged = session
            snapshot.sessions.append(session)
        }
        try await persist()
        return merged
    }

    @discardableResult
    public func upsert(_ tab: RemoteTab) async throws -> RemoteTab {
        try await load()
        guard tab.context == .normal else {
            throw CompanionModelError.incognitoNotSyncable
        }
        let merged: RemoteTab
        if let current = snapshot.remoteTabs.first(where: { $0.id == tab.id }) {
            merged = try CompanionReadModelFieldMerge.merge(current, tab)
            snapshot.remoteTabs.replace(merged, where: { $0.id == tab.id })
        } else {
            merged = tab
            snapshot.remoteTabs.append(tab)
        }
        try await persist()
        return merged
    }

    @discardableResult
    public func append(_ visit: HistoryVisit) async throws -> HistoryVisit {
        try await load()
        let merged: HistoryVisit
        if let current = snapshot.history.first(where: { $0.id == visit.id }) {
            merged = try CompanionReadModelFieldMerge.merge(current, visit)
            snapshot.history.replace(merged, where: { $0.id == visit.id })
        } else {
            merged = visit
            snapshot.history.append(visit)
        }
        try await persist()
        return merged
    }

    @discardableResult
    public func createWorkspace(
        name: String,
        icon: String = "",
        accent: String? = nil
    ) async throws -> Workspace {
        try await load()
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { throw LocalCompanionStoreError.invalidSnapshot }
        let version = try nextVersion()
        let workspace = CompanionFieldMerge.stampLocal(
            previous: nil,
            candidate: Workspace(
                workspaceID: WorkspaceID(),
                name: trimmed,
                icon: icon,
                accent: accent,
                sortKey: workspaceOrderKey(version),
                createdAt: version.modifiedAt,
                version: version
            )
        )
        snapshot.workspaces.append(workspace)
        try await persist()
        return workspace
    }

    @discardableResult
    public func updateWorkspace(
        _ id: WorkspaceID,
        name: String? = nil,
        icon: String? = nil,
        accent: String?? = nil
    ) async throws -> Workspace {
        try await load()
        guard let index = snapshot.workspaces.firstIndex(where: { $0.id == id }) else {
            throw LocalCompanionStoreError.notFound
        }
        let previous = snapshot.workspaces[index]
        var candidate = previous
        if let name {
            let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty else { throw LocalCompanionStoreError.invalidSnapshot }
            candidate.name = trimmed
        }
        if let icon { candidate.icon = icon }
        if let accent { candidate.accent = accent }
        candidate.version = try nextVersion()
        candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
        snapshot.workspaces[index] = candidate
        try await persist()
        return candidate
    }

    @discardableResult
    public func deleteWorkspace(
        _ id: WorkspaceID
    ) async throws -> (workspace: Workspace, nodes: [TreeNode]) {
        try await load()
        guard let index = snapshot.workspaces.firstIndex(where: { $0.id == id }) else {
            throw LocalCompanionStoreError.notFound
        }
        let previous = snapshot.workspaces[index]
        let version = try nextVersion()
        var deleted = previous
        deleted.version = version
        deleted.tombstone = makeTombstone(
            entityID: id.rawValue,
            version: version,
            parentID: nil,
            orderKey: nil
        )
        deleted = CompanionFieldMerge.stampLocal(previous: previous, candidate: deleted)
        snapshot.workspaces[index] = deleted
        var deletedNodes: [TreeNode] = []
        for nodeIndex in snapshot.treeNodes.indices
            where snapshot.treeNodes[nodeIndex].workspaceID == id
                && !snapshot.treeNodes[nodeIndex].isDeleted {
            let oldNode = snapshot.treeNodes[nodeIndex]
            let nodeVersion = try nextVersion()
            var node = oldNode
            node.version = nodeVersion
            node.tombstone = makeTombstone(
                entityID: node.id.rawValue,
                version: nodeVersion,
                parentID: node.parentID?.rawValue,
                orderKey: node.orderKey
            )
            node = CompanionFieldMerge.stampLocal(previous: oldNode, candidate: node)
            snapshot.treeNodes[nodeIndex] = node
            deletedNodes.append(node)
        }
        try await persist()
        return (deleted, deletedNodes)
    }

    @discardableResult
    public func createTreeNode(
        workspaceID: WorkspaceID,
        parentID: TreeNodeID? = nil,
        kind: TreeNodeKind,
        title: String,
        url: String? = nil,
        icon: String = "",
        accent: String? = nil
    ) async throws -> TreeNode {
        try await load()
        guard snapshot.workspaces.contains(where: {
            $0.id == workspaceID && !$0.isDeleted
        }) else {
            throw LocalCompanionStoreError.notFound
        }
        try validateParent(parentID, workspaceID: workspaceID, moving: nil)
        let siblings = snapshot.visibleTreeNodes.filter {
            $0.workspaceID == workspaceID && $0.parentID == parentID
        }.sorted { $0.syncSortKey < $1.syncSortKey }
        let version = try nextVersion()
        let node = CompanionFieldMerge.stampLocal(
            previous: nil,
            candidate: try TreeNode(
                treeNodeID: TreeNodeID(),
                workspaceID: workspaceID,
                parentID: parentID,
                kind: kind,
                title: title,
                url: url,
                icon: icon,
                accent: accent,
                orderKey: try OrderKey.between(
                    siblings.last?.orderKey,
                    nil,
                    tieBreaker: localDeviceID
                ),
                createdAt: version.modifiedAt,
                version: version
            )
        )
        snapshot.treeNodes.append(node)
        try await persist()
        return node
    }

    @discardableResult
    public func updateTreeNode(
        _ id: TreeNodeID,
        title: String? = nil,
        url: String?? = nil,
        icon: String? = nil,
        accent: String?? = nil
    ) async throws -> TreeNode {
        try await load()
        guard let index = snapshot.treeNodes.firstIndex(where: { $0.id == id }) else {
            throw LocalCompanionStoreError.notFound
        }
        let previous = snapshot.treeNodes[index]
        var candidate = previous
        if let title { candidate.title = title }
        if let url { candidate.url = url }
        if let icon { candidate.icon = icon }
        if let accent { candidate.accent = accent }
        candidate.version = try nextVersion()
        candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
        snapshot.treeNodes[index] = candidate
        try await persist()
        return candidate
    }

    @discardableResult
    public func moveTreeNode(
        _ id: TreeNodeID,
        to workspaceID: WorkspaceID,
        parentID: TreeNodeID?
    ) async throws -> TreeNode {
        try await load()
        guard let index = snapshot.treeNodes.firstIndex(where: { $0.id == id }) else {
            throw LocalCompanionStoreError.notFound
        }
        try validateParent(parentID, workspaceID: workspaceID, moving: id)
        let siblings = snapshot.visibleTreeNodes.filter {
            $0.id != id && $0.workspaceID == workspaceID && $0.parentID == parentID
        }.sorted { $0.syncSortKey < $1.syncSortKey }
        let previous = snapshot.treeNodes[index]
        var candidate = previous
        candidate.workspaceID = workspaceID
        candidate.parentID = parentID
        candidate.orderKey = try OrderKey.between(
            siblings.last?.orderKey,
            nil,
            tieBreaker: localDeviceID
        )
        candidate.wireSortKey = nil
        candidate.version = try nextVersion()
        candidate = CompanionFieldMerge.stampLocal(previous: previous, candidate: candidate)
        snapshot.treeNodes[index] = candidate
        try await persist()
        return candidate
    }

    @discardableResult
    public func deleteTreeNode(_ id: TreeNodeID) async throws -> [TreeNode] {
        try await load()
        guard snapshot.treeNodes.contains(where: { $0.id == id }) else {
            throw LocalCompanionStoreError.notFound
        }
        var pending = [id]
        var ids = Set<TreeNodeID>()
        while let current = pending.popLast(), ids.insert(current).inserted {
            pending.append(contentsOf: snapshot.treeNodes.filter {
                $0.parentID == current && !$0.isDeleted
            }.map(\.id))
        }
        var result: [TreeNode] = []
        for index in snapshot.treeNodes.indices where ids.contains(snapshot.treeNodes[index].id) {
            let previous = snapshot.treeNodes[index]
            guard !previous.isDeleted else { continue }
            let version = try nextVersion()
            var deleted = previous
            deleted.version = version
            deleted.tombstone = makeTombstone(
                entityID: deleted.id.rawValue,
                version: version,
                parentID: deleted.parentID?.rawValue,
                orderKey: deleted.orderKey
            )
            deleted = CompanionFieldMerge.stampLocal(previous: previous, candidate: deleted)
            snapshot.treeNodes[index] = deleted
            result.append(deleted)
        }
        try await persist()
        return result
    }

    private func persist() async throws {
        try await store.save(snapshot)
        await searchIndex.rebuild(snapshot: snapshot)
    }

    private func nextVersion() throws -> SyncVersion {
        clock = try clock.ticking(
            at: UInt64(Date().timeIntervalSince1970 * 1_000)
        )
        return SyncVersion(modifiedAt: clock, modifiedBy: localDeviceID)
    }

    private func observeStoredClocks() {
        let clocks = snapshot.devices.map(\.version.modifiedAt)
            + snapshot.workspaces.map(\.version.modifiedAt)
            + snapshot.treeNodes.map(\.version.modifiedAt)
            + snapshot.sessions.map(\.version.modifiedAt)
            + snapshot.remoteTabs.map(\.version.modifiedAt)
            + snapshot.history.map(\.version.modifiedAt)
        guard let newest = clocks.max(), newest >= clock else { return }
        clock = HybridLogicalClock(
            physicalMilliseconds: newest.physicalMilliseconds,
            submillisecondMicroseconds: newest.submillisecondMicroseconds,
            logicalCounter: newest.logicalCounter,
            nodeID: localDeviceID
        )
    }

    private func validateParent(
        _ parentID: TreeNodeID?,
        workspaceID: WorkspaceID,
        moving: TreeNodeID?
    ) throws {
        guard let parentID else { return }
        guard parentID != moving,
              let parent = snapshot.treeNodes.first(where: {
                  $0.id == parentID && !$0.isDeleted
              }),
              parent.kind == .folder,
              parent.workspaceID == workspaceID else {
            throw LocalCompanionStoreError.invalidParent
        }
        var cursor: TreeNode? = parent
        while let node = cursor {
            if node.id == moving { throw LocalCompanionStoreError.treeCycle }
            cursor = node.parentID.flatMap { ancestor in
                snapshot.treeNodes.first { $0.id == ancestor && !$0.isDeleted }
            }
        }
    }

    private func workspaceOrderKey(_ version: SyncVersion) -> String {
        String(format: "%016llx", version.modifiedAt.physicalMicroseconds)
            + "!" + localDeviceID.rawValue.uuidString.lowercased()
    }

    private func makeTombstone(
        entityID: UUID,
        version: SyncVersion,
        parentID: UUID?,
        orderKey: OrderKey?
    ) -> Tombstone {
        Tombstone(
            entityID: entityID,
            deletedAt: version.modifiedAt,
            deletedBy: localDeviceID,
            originalParentID: parentID,
            originalOrderKey: orderKey,
            purgeAfterMilliseconds: version.modifiedAt.physicalMilliseconds
                + UInt64(30 * 24 * 60 * 60 * 1_000)
        )
    }
}

private extension Array {
    mutating func replace(_ element: Element, where predicate: (Element) -> Bool) {
        if let index = firstIndex(where: predicate) {
            self[index] = element
        } else {
            append(element)
        }
    }
}
