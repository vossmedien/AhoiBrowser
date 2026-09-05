import Foundation
import SwiftUI
import AhoiCloudKitSpike

public typealias CompanionRemoteCommandClock = @MainActor @Sendable () -> UInt64
public typealias CompanionRemoteCommandSleeper = @Sendable (UInt64) async -> Void

@MainActor
public final class CompanionAppModel: ObservableObject {
    @Published public internal(set) var snapshot: CompanionSnapshot = .empty
    @Published public private(set) var searchResults: [CompanionSearchResult] = []
    @Published public internal(set) var loadError: String?
    @Published public internal(set) var syncStatus: CloudKitSyncStatus?
    @Published public internal(set) var remoteControlIdentity: RemoteControlProvisioningIdentity?
    @Published public internal(set) var remoteCommandStatus: String?
    @Published public internal(set) var recentRemoteCommands: [CompanionRemoteCommandStatusItem] = []
    @Published public internal(set) var syncSafetyState = CloudKitSyncSafetyState()
    @Published public internal(set) var physicalDeletionRecoveryRequired = false
    @Published public private(set) var historyRetentionDays: Int
    @Published public internal(set) var isSyncConfigured: Bool
    @Published public internal(set) var isBookmarkSyncEnabled: Bool
    @Published public internal(set) var keyLifecycleStatus: CompanionKeyLifecycleStatus
    @Published public internal(set) var syncVisibleEvidence: CompanionSyncVisibleEvidence?

    public let repository: LocalFirstRepository
    let defaults: UserDefaults
    var syncProvider: CloudKitSyncProvider?
    var syncBridge: CompanionSyncBridge?
    let syncRuntimeFactory: CompanionSyncRuntimeFactory?
    let mobileSessionID: DeviceSessionID?
    private let mobileDeviceName: String
    private let mobileDeviceKind: DeviceKind
    var providerPrepared = false
    var commandLabels: [UUID: String] = [:]
    var syncGeneration: UInt64 = 0
    private var syncInProgress = false
    var syncRequestedWhileInProgress = false
    private var syncWaiters: [CheckedContinuation<Void, Never>] = []
    var syncRuntimeCancellation: Task<Void, Never>?
    var syncRuntimeCancellationGeneration: UInt64?
    var syncPreferenceIntentGeneration: UInt64 = 0
    var desiredSyncEnabled = false
    var syncActivationInProgress = false
    var syncActivationCompletedIntentGeneration: UInt64?
    var syncActivationWaiters: [CheckedContinuation<Void, Never>] = []
    var eventDrivenSyncTask: Task<Void, Never>?
    var eventDrivenSyncRequested = false
    var eventDrivenSyncGeneration: UInt64 = 0
    var localSnapshotReseedRequired = false
    var remoteCommandExpiryTask: Task<Void, Never>?
    var remoteCommandExpiryGeneration: UInt64 = 0
    let remoteCommandClock: CompanionRemoteCommandClock
    let remoteCommandSleeper: CompanionRemoteCommandSleeper
#if DEBUG
    var syncVisibleUITestRuntime: CompanionSyncVisibleUITestRuntime?
#endif

    public init(
        repository: LocalFirstRepository,
        syncProvider: CloudKitSyncProvider? = nil,
        syncBridge: CompanionSyncBridge? = nil,
        syncRuntimeFactory: CompanionSyncRuntimeFactory? = nil,
        mobileSessionID: DeviceSessionID? = nil,
        mobileDeviceName: String = "Ahoi Mobile",
        mobileDeviceKind: DeviceKind = .iPhone,
        defaults: UserDefaults = .standard,
        remoteCommandClock: @escaping CompanionRemoteCommandClock = {
            UInt64(max(Date().timeIntervalSince1970 * 1_000, 0))
        },
        remoteCommandSleeper: @escaping CompanionRemoteCommandSleeper = { delay in
            guard delay > 0 else { return }
            try? await Task.sleep(for: .milliseconds(Int64(clamping: delay)))
        }
    ) {
        self.repository = repository
        self.defaults = defaults
        self.syncProvider = syncProvider
        self.syncBridge = syncBridge
        self.syncRuntimeFactory = syncRuntimeFactory
        self.mobileSessionID = mobileSessionID
        self.mobileDeviceName = mobileDeviceName
        self.mobileDeviceKind = mobileDeviceKind
        self.remoteCommandClock = remoteCommandClock
        self.remoteCommandSleeper = remoteCommandSleeper
        self.isSyncConfigured = syncProvider != nil && syncBridge != nil
        self.isBookmarkSyncEnabled = defaults.bool(forKey: Self.bookmarkSyncApprovalKey)
        self.desiredSyncEnabled = syncProvider != nil && syncBridge != nil
        self.keyLifecycleStatus = syncProvider != nil && syncBridge != nil
            ? .ready(keyVersion: 1)
            : .disabled
        self.syncVisibleEvidence = nil
        let storedRetention = defaults.integer(
            forKey: CompanionSyncPreferences.historyRetentionDaysKey
        )
        self.historyRetentionDays =
            CompanionSyncPreferences.historyRetentionChoices.contains(storedRetention)
            ? storedRetention
            : CompanionSyncPreferences.defaultHistoryRetentionDays
        bindEventDrivenSync(to: syncProvider)
#if DEBUG
        configureSyncVisibleUITestRuntimeIfRequested()
#endif
    }

    public convenience init() {
        self.init(repository: LocalFirstRepository(store: InMemoryCompanionStore()))
    }

    public var isRemoteControlAvailable: Bool {
        syncBridge?.remoteControlConfigured == true
    }

    public func load() async {
        do {
            try await repository.load()
            snapshot = try await repository.currentSnapshot()
            searchResults = try await repository.search("")
            loadError = nil
            syncStatus = syncProvider?.status()
            syncSafetyState = syncProvider?.safetyState() ?? .init()
            if let syncBridge {
                do {
                    remoteControlIdentity = try await syncBridge.remoteControlIdentity()
                } catch RemoteCommandSignerError.identityRevoked {
                    // Explicit command-key revocation is a stable product
                    // state, not a browser-wide load failure. Settings keeps
                    // the signer facade available only for conscious rotation.
                    remoteControlIdentity = nil
                } catch {
                    presentOperationFailure(error)
                }
            }
        } catch {
            presentOperationFailure(error)
        }
    }

    public func refreshSearch(query: String) async {
        do {
            searchResults = try await repository.search(query)
            loadError = nil
        } catch {
            presentOperationFailure(error)
        }
    }

    public func save(_ workspace: Workspace) async {
        _ = await performLocalFirstMutation({
            try await repository.upsert(workspace)
            return workspace
        }, enqueue: { committed in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(committed)
        })
    }

    public func save(_ node: TreeNode) async {
        _ = await performLocalFirstMutation({
            try await repository.upsert(node)
            return node
        }, enqueue: { committed in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(committed)
        })
    }

    @discardableResult
    public func createWorkspace(name: String, icon: String = "") async -> Workspace? {
        await performLocalFirstMutation({
            try await repository.createWorkspace(name: name, icon: icon)
        }, enqueue: { workspace in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(workspace)
        })
    }

    public func renameWorkspace(_ id: WorkspaceID, name: String) async {
        _ = await performLocalFirstMutation({
            try await repository.updateWorkspace(id, name: name)
        }, enqueue: { workspace in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(workspace)
        })
    }

    public func deleteWorkspace(_ id: WorkspaceID) async {
        _ = await performLocalFirstMutation({
            try await repository.deleteWorkspace(id)
        }, enqueue: { deletion in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(deletion.workspace)
            for node in deletion.nodes { try await bridge.enqueue(node) }
        })
    }

    @discardableResult
    public func createFolder(
        workspaceID: WorkspaceID,
        parentID: TreeNodeID? = nil,
        title: String
    ) async -> TreeNode? {
        await createNode(
            workspaceID: workspaceID,
            parentID: parentID,
            kind: .folder,
            title: title,
            url: nil
        )
    }

    @discardableResult
    public func createSavedPage(
        workspaceID: WorkspaceID,
        parentID: TreeNodeID? = nil,
        title: String,
        url: String
    ) async -> TreeNode? {
        await createNode(
            workspaceID: workspaceID,
            parentID: parentID,
            kind: .savedPage,
            title: title,
            url: url
        )
    }

    public func renameTreeNode(_ id: TreeNodeID, title: String) async {
        _ = await performLocalFirstMutation({
            try await repository.updateTreeNode(id, title: title)
        }, enqueue: { node in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(node)
        })
    }

    public func moveTreeNode(
        _ id: TreeNodeID,
        workspaceID: WorkspaceID,
        parentID: TreeNodeID?
    ) async {
        _ = await performLocalFirstMutation({
            try await repository.moveTreeNode(
                id,
                to: workspaceID,
                parentID: parentID
            )
        }, enqueue: { node in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(node)
        })
    }

    public func reorderTreeNode(
        _ id: TreeNodeID,
        before successorID: TreeNodeID?
    ) async {
        _ = await performLocalFirstMutation({
            try await repository.reorderTreeNode(id, before: successorID)
        }, enqueue: { node in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(node)
        })
    }

    public func deleteTreeNode(_ id: TreeNodeID) async {
        _ = await performLocalFirstMutation({
            try await repository.deleteTreeNode(id)
        }, enqueue: { nodes in
            guard let bridge = self.syncBridge else { return }
            for node in nodes { try await bridge.enqueue(node) }
        })
    }

    public func save(_ tab: RemoteTab) async {
        _ = await performLocalFirstMutation({
            try await repository.upsert(tab)
            return tab
        }, enqueue: { committed in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(committed)
        })
    }

    public func recordMobileNavigation(
        title: String,
        url: String,
        transition: String = "link"
    ) async {
        _ = await performLocalFirstMutation({
            try await repository.recordLocalHistoryVisit(
                title: title,
                url: url,
                transition: transition
            )
        }, enqueue: { visit in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(visit)
        })
    }

    public func publishMobileTab(_ tab: MobileTabRecord) async {
        guard tab.mode == .normal,
              let mobileSessionID,
              let url = tab.url else { return }
        _ = await performLocalFirstMutation({
            try await repository.publishLocalMobileTab(
                tabID: tab.id,
                sessionID: mobileSessionID,
                deviceName: mobileDeviceName,
                deviceKind: mobileDeviceKind,
                workspaceID: tab.workspaceID,
                title: tab.effectiveTitle.isEmpty ? url : tab.effectiveTitle,
                url: url,
                pinned: tab.isSaved
            )
        }, enqueue: { publication in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(publication.device)
            try await bridge.enqueue(publication.session)
            try await bridge.enqueue(publication.tab)
        })
    }

    /// Reconciles the durable browser session with the public device-tab
    /// projection. This closes records that disappeared while the app was not
    /// running, restores every surviving normal tab after launch, and refreshes
    /// the device/session heartbeat without ever publishing private tabs.
    public func reconcilePublishedMobileTabs(_ tabs: [MobileTabRecord]) async {
        guard let mobileSessionID else { return }
        let currentTabs = tabs.filter { $0.mode == .normal && $0.url != nil }
        let currentIDs = Set(currentTabs.map(\.id))
        _ = await performLocalFirstMutation({
            var outbound = CompanionMobilePublicationBatch()
            let publishedTabs = try await repository.localOpenMobileTabs(
                sessionID: mobileSessionID
            )
            for stale in publishedTabs where !currentIDs.contains(stale.id.rawValue) {
                if let closed = try await repository.closeLocalMobileTab(stale.id.rawValue) {
                    outbound.tabs.append(closed)
                }
            }

            let session = try await repository.publishLocalMobileSession(
                sessionID: mobileSessionID,
                deviceName: mobileDeviceName,
                deviceKind: mobileDeviceKind,
                workspaceID: currentTabs.first?.workspaceID
            )
            outbound.devices.append(session.device)
            outbound.sessions.append(session.session)

            let publishedByID = Dictionary(
                uniqueKeysWithValues: publishedTabs.map { ($0.id.rawValue, $0) }
            )
            for tab in currentTabs {
                guard let url = tab.url else { continue }
                let title = tab.effectiveTitle.isEmpty ? url : tab.effectiveTitle
                if let published = publishedByID[tab.id],
                   published.url == url,
                   published.title == title,
                   published.workspaceID == tab.workspaceID,
                   published.pinned == tab.isSaved,
                   published.isOpen,
                   !published.isDeleted {
                    continue
                }
                let publication = try await repository.publishLocalMobileTab(
                    tabID: tab.id,
                    sessionID: mobileSessionID,
                    deviceName: mobileDeviceName,
                    deviceKind: mobileDeviceKind,
                    workspaceID: tab.workspaceID,
                    title: title,
                    url: url,
                    pinned: tab.isSaved
                )
                outbound.devices.append(publication.device)
                outbound.sessions.append(publication.session)
                outbound.tabs.append(publication.tab)
            }
            return outbound
        }, enqueue: { outbound in
            guard let bridge = self.syncBridge else { return }
            try await outbound.enqueue(using: bridge)
        })
    }

    public func closePublishedMobileTab(_ id: UUID) async {
        _ = await performLocalFirstMutation({
            try await repository.closeLocalMobileTab(id)
        }, enqueue: { closed in
            guard let bridge = self.syncBridge, let closed else { return }
            try await bridge.enqueue(closed)
        })
    }

    public func sync() async {
#if DEBUG
        if syncVisibleUITestRuntime != nil {
            await refreshSyncVisibleUITestEvidenceIfNeeded()
            return
        }
#endif
        if syncInProgress {
            syncRequestedWhileInProgress = true
            await withCheckedContinuation { continuation in
                syncWaiters.append(continuation)
            }
            return
        }
        syncInProgress = true
        defer {
            syncInProgress = false
            let completedWaiters = syncWaiters
            syncWaiters.removeAll()
            completedWaiters.forEach { $0.resume() }
        }

        repeat {
            syncRequestedWhileInProgress = false
            await performSync()
        } while syncRequestedWhileInProgress && syncProvider != nil
    }

    private func performSync() async {
        guard let syncProvider else { return }
        guard let bridge = syncBridge else {
            loadError = CompanionL10n.string(
                "sync.configuration_missing",
                fallback: "CloudKit is not fully configured. Local data remains available."
            )
            return
        }
        let generation = syncGeneration
        do {
            await bridge.setBookmarkSyncEnabled(isBookmarkSyncEnabled)
            guard isCurrentSyncRuntime(syncProvider, generation: generation) else { return }
            if !providerPrepared {
                try await syncProvider.prepare()
                guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                    return
                }
                try await bridge.restorePersistedRemoteCommandStates()
                guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                    return
                }
                try await bridge.enqueueLocalSnapshot()
                guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                    return
                }
                localSnapshotReseedRequired = false
                providerPrepared = true
            } else if localSnapshotReseedRequired {
                try await bridge.enqueueLocalSnapshot()
                guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                    return
                }
                localSnapshotReseedRequired = false
            }
            guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                return
            }
            try await bridge.syncNow()
            guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                return
            }
            snapshot = try await repository.currentSnapshot()
            searchResults = try await repository.search("")
            guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                return
            }
            await refreshRemoteCommandStates(using: bridge)
        } catch {
            guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
                return
            }
            presentOperationFailure(error)
        }
        guard isCurrentSyncRuntime(syncProvider, generation: generation) else {
            return
        }
        syncStatus = syncProvider.status()
        syncSafetyState = syncProvider.safetyState()
        physicalDeletionRecoveryRequired = await syncProvider
            .hasPhysicalDeletionQuarantine()
    }

    func isCurrentSyncRuntime(
        _ provider: CloudKitSyncProvider,
        generation: UInt64
    ) -> Bool {
        syncGeneration == generation && syncProvider === provider
    }

    func bindEventDrivenSync(to provider: CloudKitSyncProvider?) {
        provider?.setEventDrivenSyncHandler { [weak self] in
            Task { @MainActor [weak self] in
                self?.scheduleEventDrivenSync()
            }
        }
    }

    private func scheduleEventDrivenSync() {
        guard desiredSyncEnabled else { return }
        let providerStatus = syncProvider?.status()
        if syncInProgress, providerStatus?.phase != .retryScheduled {
            // A normal enqueue during an active pass belongs to the same
            // coalesced sync loop and must not spawn a competing task.
            syncRequestedWhileInProgress = true
            return
        }
        eventDrivenSyncRequested = true
        guard eventDrivenSyncTask == nil else { return }
        eventDrivenSyncGeneration &+= 1
        let taskGeneration = eventDrivenSyncGeneration
        eventDrivenSyncTask = Task { @MainActor [weak self] in
            await Task.yield()
            guard let self else { return }
            while !Task.isCancelled,
                  self.desiredSyncEnabled,
                  self.eventDrivenSyncRequested {
                self.eventDrivenSyncRequested = false
                let status = self.syncProvider?.status()
                if status?.phase == .retryScheduled {
                    let rawDelay = status?.retryAfterSeconds ?? 0
                    let boundedDelay = rawDelay.isFinite && rawDelay > 0
                        ? min(rawDelay, 3_600)
                        : 2
                    let delay = max(boundedDelay, 2)
                    let delayMilliseconds = Int64((delay * 1_000).rounded(.up))
                    try? await Task.sleep(for: .milliseconds(delayMilliseconds))
                    guard !Task.isCancelled, self.desiredSyncEnabled else { break }
                }
                await self.sync()
            }
            guard self.eventDrivenSyncGeneration == taskGeneration else { return }
            self.eventDrivenSyncTask = nil
        }
    }

    public func setHistoryRetentionDays(_ days: Int) async {
        guard CompanionSyncPreferences.historyRetentionChoices.contains(days) else {
            loadError = CompanionL10n.string(
                "sync.retention.invalid",
                fallback: "This retention period is not supported."
            )
            return
        }
        let tombstones = await performLocalFirstMutation({
            let committed = try await repository.applyHistoryRetention(days: days)
            defaults.set(days, forKey: CompanionSyncPreferences.historyRetentionDaysKey)
            historyRetentionDays = days
            return committed
        }, enqueue: { committed in
            guard let bridge = self.syncBridge else { return }
            for visit in committed { try await bridge.enqueue(visit) }
        })
        if let tombstones, !tombstones.isEmpty {
            await sync()
        }
    }

    public func deleteHistoryVisit(_ id: HistoryVisitID) async {
        if let tombstone = await performLocalFirstMutation({
            try await repository.deleteHistoryVisit(id)
        }, enqueue: { tombstone in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(tombstone)
        }) {
            await syncIfNeeded(afterDeleting: [tombstone])
        }
    }

    public func deleteHistory(sinceMilliseconds: UInt64) async {
        if let tombstones = await performLocalFirstMutation({
            try await repository.deleteHistory(
                sinceMilliseconds: sinceMilliseconds
            )
        }, enqueue: { tombstones in
            guard let bridge = self.syncBridge else { return }
            for tombstone in tombstones { try await bridge.enqueue(tombstone) }
        }) {
            await syncIfNeeded(afterDeleting: tombstones)
        }
    }

    private func syncIfNeeded(afterDeleting tombstones: [HistoryVisit]) async {
        guard !tombstones.isEmpty, syncBridge != nil else { return }
        await sync()
    }

    public func confirmAccountTransition(allowLocalUpload: Bool) async {
        guard let syncProvider else { return }
        do {
            try await syncProvider.confirmAccountTransition(
                allowLocalUpload: allowLocalUpload
            )
            syncSafetyState = syncProvider.safetyState()
            syncStatus = syncProvider.status()
            if allowLocalUpload { await sync() }
        } catch {
            presentOperationFailure(error)
            syncStatus = syncProvider.status()
            syncSafetyState = syncProvider.safetyState()
        }
    }

    public func confirmZoneRecovery() async {
        guard let syncProvider else { return }
        do {
            try await syncProvider.confirmZoneRecovery()
            syncSafetyState = syncProvider.safetyState()
            await sync()
        } catch {
            presentOperationFailure(error)
            syncStatus = syncProvider.status()
            syncSafetyState = syncProvider.safetyState()
        }
    }

    public func retryQuarantinedSyncRecords() async {
        guard let syncProvider else { return }
        do {
            try await syncProvider.retryQuarantinedRecords()
            await sync()
        } catch {
            guard self.syncProvider === syncProvider else { return }
            presentOperationFailure(error)
            syncStatus = syncProvider.status()
        }
        guard self.syncProvider === syncProvider else { return }
        physicalDeletionRecoveryRequired = await syncProvider
            .hasPhysicalDeletionQuarantine()
    }

    public func restorePhysicallyDeletedSyncRecords() async {
        guard let syncProvider, let syncBridge else { return }
        do {
            try await syncBridge.restorePhysicallyDeletedRecords()
            await sync()
        } catch {
            guard self.syncProvider === syncProvider else { return }
            presentOperationFailure(error)
            syncStatus = syncProvider.status()
        }
        guard self.syncProvider === syncProvider else { return }
        physicalDeletionRecoveryRequired = await syncProvider
            .hasPhysicalDeletionQuarantine()
    }

    private func createNode(
        workspaceID: WorkspaceID,
        parentID: TreeNodeID?,
        kind: TreeNodeKind,
        title: String,
        url: String?
    ) async -> TreeNode? {
        await performLocalFirstMutation({
            try await repository.createTreeNode(
                workspaceID: workspaceID,
                parentID: parentID,
                kind: kind,
                title: title,
                url: url
            )
        }, enqueue: { node in
            guard let bridge = self.syncBridge else { return }
            try await bridge.enqueue(node)
        })
    }

    func refreshLocalState() async throws {
        snapshot = try await repository.currentSnapshot()
        searchResults = try await repository.search("")
        loadError = nil
    }

}
