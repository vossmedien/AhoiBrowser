import Foundation
import AhoiCloudKitSpike

public actor LocalFirstRepository {
    private let store: any LocalCompanionStore
    private let searchIndex = LocalSearchIndex()
    let localDeviceID: DeviceID
    private var clock: HybridLogicalClock
    var snapshot: CompanionSnapshot = .empty
    private var persistedSnapshot: CompanionSnapshot = .empty
    private var didLoad = false
    private var mutationInProgress = false
    private var mutationWaiters: [CheckedContinuation<Void, Never>] = []

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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
    }

    func loadIfNeeded() async throws {
        guard !didLoad else { return }
        let loaded = try await store.load()
        snapshot = loaded
        persistedSnapshot = loaded
        observeStoredClocks()
        didLoad = true
        await searchIndex.rebuild(snapshot: loaded)
    }

    public func currentSnapshot() async throws -> CompanionSnapshot {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        return snapshot
    }

    public func search(_ query: String, limit: Int = 50) async throws -> [CompanionSearchResult] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        return await searchIndex.search(query, limit: limit)
    }

    public func replace(_ snapshot: CompanionSnapshot) async throws {
        await acquireMutation()
        defer { releaseMutation() }
        try await store.save(snapshot)
        self.snapshot = snapshot
        persistedSnapshot = snapshot
        didLoad = true
        await searchIndex.rebuild(snapshot: snapshot)
    }

    @discardableResult
    public func upsert(_ device: Device) async throws -> Device {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
    public func recordLocalHistoryVisit(
        title: String,
        url: String,
        transition: String = "link"
    ) async throws -> HistoryVisit {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let version = try nextVersion().normalized(for: [
            "device_id",
            "url",
            "title",
            "last_visit",
            "visit_count",
            "transition",
            "tombstone",
        ])
        let visit = try HistoryVisit(
            visitID: HistoryVisitID(),
            deviceID: localDeviceID,
            title: title,
            url: url,
            visitedAt: version.modifiedAt,
            transition: transition,
            version: version
        )
        snapshot.history.append(visit)
        try await persist()
        return visit
    }

    public func publishLocalMobileTab(
        tabID: UUID,
        sessionID: DeviceSessionID,
        deviceName: String,
        deviceKind: DeviceKind,
        workspaceID: WorkspaceID?,
        title: String,
        url: String,
        pinned: Bool
    ) async throws -> LocalMobileTabPublication {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let sessionPublication = try publishLocalMobileSessionInSnapshot(
            sessionID: sessionID,
            deviceName: deviceName,
            deviceKind: deviceKind,
            workspaceID: workspaceID
        )
        let storedDevice = sessionPublication.device
        let storedSession = sessionPublication.session

        let typedTabID = TabID(rawValue: tabID)
        let tabVersion = try nextVersion().normalized(for: [
            "device_id", "session_id", "workspace_id", "url", "title", "opened_at",
            "last_active", "pinned", "is_incognito", "tombstone",
        ])
        let existingTab = snapshot.remoteTabs.first { $0.id == typedTabID }
        let workspaceName = workspaceID.flatMap { id in
            snapshot.workspaces.first { $0.id == id && !$0.isDeleted }?.name
        }
        var tab = try RemoteTab(
            tabID: typedTabID,
            deviceID: localDeviceID,
            deviceKind: deviceKind,
            deviceName: deviceName,
            sessionID: sessionID,
            workspaceID: workspaceID,
            workspaceName: workspaceName,
            title: title,
            url: url,
            openedAt: existingTab?.openedAt ?? tabVersion.modifiedAt,
            lastActiveAt: tabVersion.modifiedAt,
            isOpen: true,
            pinned: pinned,
            version: tabVersion
        )
        if let existingTab {
            tab = CompanionReadModelFieldMerge.stampLocal(
                previous: existingTab,
                candidate: tab
            )
        }
        let storedTab = try mergeTabIntoSnapshot(tab)
        try await persist()
        return LocalMobileTabPublication(
            device: storedDevice,
            session: storedSession,
            tab: storedTab
        )
    }

    public func publishLocalMobileSession(
        sessionID: DeviceSessionID,
        deviceName: String,
        deviceKind: DeviceKind,
        workspaceID: WorkspaceID?
    ) async throws -> LocalMobileSessionPublication {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let publication = try publishLocalMobileSessionInSnapshot(
            sessionID: sessionID,
            deviceName: deviceName,
            deviceKind: deviceKind,
            workspaceID: workspaceID
        )
        try await persist()
        return publication
    }

    private func publishLocalMobileSessionInSnapshot(
        sessionID: DeviceSessionID,
        deviceName: String,
        deviceKind: DeviceKind,
        workspaceID: WorkspaceID?
    ) throws -> LocalMobileSessionPublication {
        let deviceVersion = try nextVersion().normalized(for: [
            "type", "display_name", "created_at", "last_seen", "retired", "tombstone",
        ])
        let existingDevice = snapshot.devices.first { $0.id == localDeviceID }
        var device = Device(
            deviceID: localDeviceID,
            name: deviceName,
            kind: deviceKind,
            createdAt: existingDevice?.createdAt ?? deviceVersion.modifiedAt,
            lastSeenAt: deviceVersion.modifiedAt,
            isOnline: true,
            version: deviceVersion
        )
        if let existingDevice {
            device = CompanionReadModelFieldMerge.stampLocal(
                previous: existingDevice,
                candidate: device
            )
        }
        let storedDevice = try mergeDeviceIntoSnapshot(device)

        let sessionVersion = try nextVersion().normalized(for: [
            "device_id", "started_at", "liveness", "tombstone",
        ])
        let existingSession = snapshot.sessions.first { $0.id == sessionID }
        var session = DeviceSession(
            sessionID: sessionID,
            deviceID: localDeviceID,
            deviceName: deviceName,
            deviceKind: deviceKind,
            workspaceID: workspaceID,
            startedAt: existingSession?.startedAt ?? sessionVersion.modifiedAt,
            lastActiveAt: sessionVersion.modifiedAt,
            isOnline: true,
            version: sessionVersion
        )
        if let existingSession {
            session = CompanionReadModelFieldMerge.stampLocal(
                previous: existingSession,
                candidate: session
            )
        }
        let storedSession = try mergeSessionIntoSnapshot(session)
        return LocalMobileSessionPublication(
            device: storedDevice,
            session: storedSession
        )
    }

    public func localOpenMobileTabs(
        sessionID: DeviceSessionID
    ) async throws -> [RemoteTab] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        return snapshot.remoteTabs.filter {
            $0.deviceID == localDeviceID &&
                $0.sessionID == sessionID &&
                $0.context == .normal &&
                $0.isOpen &&
                !$0.isDeleted
        }
    }

    public func closeLocalMobileTab(_ tabID: UUID) async throws -> RemoteTab? {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let typedID = TabID(rawValue: tabID)
        guard let existing = snapshot.remoteTabs.first(where: {
            $0.id == typedID && $0.deviceID == localDeviceID && !$0.isDeleted
        }) else { return nil }
        let version = try nextVersion().normalized(for: [
            "device_id", "session_id", "workspace_id", "url", "title", "opened_at",
            "last_active", "pinned", "is_incognito", "tombstone",
        ])
        let tombstone = Tombstone(
            entityID: tabID,
            deletedAt: version.modifiedAt,
            deletedBy: localDeviceID,
            originalParentID: existing.workspaceID?.rawValue,
            originalOrderKey: nil,
            purgeAfterMilliseconds: version.modifiedAt.physicalMilliseconds
                + 30 * 24 * 60 * 60 * 1_000
        )
        var closed = try RemoteTab(
            tabID: existing.id,
            deviceID: existing.deviceID,
            deviceKind: existing.deviceKind,
            deviceName: existing.deviceName,
            sessionID: existing.sessionID,
            workspaceID: existing.workspaceID,
            workspaceName: existing.workspaceName,
            title: existing.title,
            url: existing.url,
            openedAt: existing.openedAt,
            lastActiveAt: version.modifiedAt,
            isOpen: false,
            pinned: existing.pinned,
            version: version,
            tombstone: tombstone
        )
        closed = CompanionReadModelFieldMerge.stampLocal(
            previous: existing,
            candidate: closed
        )
        let stored = try mergeTabIntoSnapshot(closed)
        try await persist()
        return stored
    }

    @discardableResult
    public func createWorkspace(
        name: String,
        icon: String = "",
        accent: String? = nil
    ) async throws -> Workspace {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
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

    /// Applies the product retention policy as normal sync tombstones. The
    /// local snapshot remains authoritative and an enabled bridge can enqueue
    /// the returned records for deterministic deletion propagation.
    @discardableResult
    private func mergeDeviceIntoSnapshot(_ device: Device) throws -> Device {
        if let current = snapshot.devices.first(where: { $0.id == device.id }) {
            let merged = try CompanionReadModelFieldMerge.merge(current, device)
            snapshot.devices.replace(merged, where: { $0.id == device.id })
            return merged
        }
        snapshot.devices.append(device)
        return device
    }

    private func mergeSessionIntoSnapshot(_ session: DeviceSession) throws -> DeviceSession {
        if let current = snapshot.sessions.first(where: { $0.id == session.id }) {
            let merged = try CompanionReadModelFieldMerge.merge(current, session)
            snapshot.sessions.replace(merged, where: { $0.id == session.id })
            return merged
        }
        snapshot.sessions.append(session)
        return session
    }

    private func mergeTabIntoSnapshot(_ tab: RemoteTab) throws -> RemoteTab {
        guard tab.context == .normal else {
            throw CompanionModelError.incognitoNotSyncable
        }
        if let current = snapshot.remoteTabs.first(where: { $0.id == tab.id }) {
            let merged = try CompanionReadModelFieldMerge.merge(current, tab)
            snapshot.remoteTabs.replace(merged, where: { $0.id == tab.id })
            return merged
        }
        snapshot.remoteTabs.append(tab)
        return tab
    }

    func persist() async throws {
        let candidate = snapshot
        do {
            try await store.save(candidate)
            snapshot = candidate
            persistedSnapshot = candidate
            await searchIndex.rebuild(snapshot: candidate)
        } catch {
            snapshot = persistedSnapshot
            await searchIndex.rebuild(snapshot: persistedSnapshot)
            throw error
        }
    }

    func commitImportedSnapshot(_ imported: CompanionSnapshot) async throws {
        try await store.save(imported)
        snapshot = imported
        persistedSnapshot = imported
        observeStoredClocks()
        await searchIndex.rebuild(snapshot: imported)
    }

    func acquireMutation() async {
        while mutationInProgress {
            await withCheckedContinuation { continuation in
                mutationWaiters.append(continuation)
            }
        }
        mutationInProgress = true
    }

    func releaseMutation() {
        precondition(mutationInProgress)
        mutationInProgress = false
        let waiters = mutationWaiters
        mutationWaiters.removeAll(keepingCapacity: true)
        for waiter in waiters { waiter.resume() }
    }

    func nextVersion() throws -> SyncVersion {
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
            + snapshot.productRecords.appearance.map(\.version.modifiedAt)
            + snapshot.productRecords.permittedSettings.map(\.version.modifiedAt)
            + snapshot.productRecords.extensionInventory.map(\.version.modifiedAt)
            + snapshot.productRecords.developerAssets.map(\.version.modifiedAt)
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
        var visited = Set<TreeNodeID>()
        var depth = 0
        while let node = cursor {
            guard visited.insert(node.id).inserted else {
                throw LocalCompanionStoreError.treeCycle
            }
            if node.id == moving { throw LocalCompanionStoreError.treeCycle }
            depth += 1
            guard depth <= CompanionHierarchyPolicy.maximumDepth else {
                throw LocalCompanionStoreError.hierarchyTooDeep
            }
            guard let ancestorID = node.parentID else {
                cursor = nil
                continue
            }
            guard let ancestor = snapshot.treeNodes.first(where: {
                $0.id == ancestorID && !$0.isDeleted
            }), ancestor.kind == .folder,
               ancestor.workspaceID == workspaceID else {
                throw LocalCompanionStoreError.invalidParent
            }
            cursor = ancestor
        }
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
