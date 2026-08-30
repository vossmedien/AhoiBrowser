import Foundation
import SwiftUI
import AhoiCloudKitSpike

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
    @Published public internal(set) var keyLifecycleStatus: CompanionKeyLifecycleStatus

    public let repository: LocalFirstRepository
    private let defaults: UserDefaults
    var syncProvider: CloudKitSyncProvider?
    var syncBridge: CompanionSyncBridge?
    let syncRuntimeFactory: CompanionSyncRuntimeFactory?
    private let mobileSessionID: DeviceSessionID?
    private let mobileDeviceName: String
    private let mobileDeviceKind: DeviceKind
    var providerPrepared = false
    var commandLabels: [UUID: String] = [:]
    var commandFollowUpTasks: [UUID: Task<Void, Never>] = [:]
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

    public init(
        repository: LocalFirstRepository,
        syncProvider: CloudKitSyncProvider? = nil,
        syncBridge: CompanionSyncBridge? = nil,
        syncRuntimeFactory: CompanionSyncRuntimeFactory? = nil,
        mobileSessionID: DeviceSessionID? = nil,
        mobileDeviceName: String = "Ahoi Mobile",
        mobileDeviceKind: DeviceKind = .iPhone,
        defaults: UserDefaults = .standard
    ) {
        self.repository = repository
        self.defaults = defaults
        self.syncProvider = syncProvider
        self.syncBridge = syncBridge
        self.syncRuntimeFactory = syncRuntimeFactory
        self.mobileSessionID = mobileSessionID
        self.mobileDeviceName = mobileDeviceName
        self.mobileDeviceKind = mobileDeviceKind
        self.isSyncConfigured = syncProvider != nil && syncBridge != nil
        self.desiredSyncEnabled = syncProvider != nil && syncBridge != nil
        self.keyLifecycleStatus = syncProvider != nil && syncBridge != nil
            ? .ready(keyVersion: 1)
            : .disabled
        let storedRetention = defaults.integer(
            forKey: CompanionSyncPreferences.historyRetentionDaysKey
        )
        self.historyRetentionDays =
            CompanionSyncPreferences.historyRetentionChoices.contains(storedRetention)
            ? storedRetention
            : CompanionSyncPreferences.defaultHistoryRetentionDays
        bindEventDrivenSync(to: syncProvider)
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
                    loadError = error.localizedDescription
                }
            }
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func refreshSearch(query: String) async {
        do {
            searchResults = try await repository.search(query)
            loadError = nil
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func save(_ workspace: Workspace) async {
        do {
            try await repository.upsert(workspace)
            try await syncBridge?.enqueue(workspace)
            snapshot = try await repository.currentSnapshot()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func save(_ node: TreeNode) async {
        do {
            try await repository.upsert(node)
            try await syncBridge?.enqueue(node)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    @discardableResult
    public func createWorkspace(name: String, icon: String = "") async -> Workspace? {
        do {
            let workspace = try await repository.createWorkspace(name: name, icon: icon)
            try await syncBridge?.enqueue(workspace)
            try await refreshLocalState()
            return workspace
        } catch {
            loadError = error.localizedDescription
            return nil
        }
    }

    public func renameWorkspace(_ id: WorkspaceID, name: String) async {
        do {
            let workspace = try await repository.updateWorkspace(id, name: name)
            try await syncBridge?.enqueue(workspace)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func deleteWorkspace(_ id: WorkspaceID) async {
        do {
            let deletion = try await repository.deleteWorkspace(id)
            try await syncBridge?.enqueue(deletion.workspace)
            for node in deletion.nodes { try await syncBridge?.enqueue(node) }
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
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
        do {
            let node = try await repository.updateTreeNode(id, title: title)
            try await syncBridge?.enqueue(node)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func moveTreeNode(
        _ id: TreeNodeID,
        workspaceID: WorkspaceID,
        parentID: TreeNodeID?
    ) async {
        do {
            let node = try await repository.moveTreeNode(
                id,
                to: workspaceID,
                parentID: parentID
            )
            try await syncBridge?.enqueue(node)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func deleteTreeNode(_ id: TreeNodeID) async {
        do {
            let nodes = try await repository.deleteTreeNode(id)
            for node in nodes { try await syncBridge?.enqueue(node) }
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func save(_ tab: RemoteTab) async {
        do {
            try await repository.upsert(tab)
            try await syncBridge?.enqueue(tab)
            snapshot = try await repository.currentSnapshot()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func recordMobileNavigation(
        title: String,
        url: String,
        transition: String = "link"
    ) async {
        do {
            let visit = try await repository.recordLocalHistoryVisit(
                title: title,
                url: url,
                transition: transition
            )
            try await syncBridge?.enqueue(visit)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func publishMobileTab(_ tab: MobileTabRecord) async {
        guard tab.mode == .normal,
              let mobileSessionID,
              let url = tab.url else { return }
        do {
            let publication = try await repository.publishLocalMobileTab(
                tabID: tab.id,
                sessionID: mobileSessionID,
                deviceName: mobileDeviceName,
                deviceKind: mobileDeviceKind,
                workspaceID: tab.workspaceID,
                title: tab.title.isEmpty ? url : tab.title,
                url: url,
                pinned: tab.isSaved
            )
            try await syncBridge?.enqueue(publication.device)
            try await syncBridge?.enqueue(publication.session)
            try await syncBridge?.enqueue(publication.tab)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    /// Reconciles the durable browser session with the public device-tab
    /// projection. This closes records that disappeared while the app was not
    /// running, restores every surviving normal tab after launch, and refreshes
    /// the device/session heartbeat without ever publishing private tabs.
    public func reconcilePublishedMobileTabs(_ tabs: [MobileTabRecord]) async {
        guard let mobileSessionID else { return }
        let currentTabs = tabs.filter { $0.mode == .normal && $0.url != nil }
        let currentIDs = Set(currentTabs.map(\.id))
        do {
            let publishedTabs = try await repository.localOpenMobileTabs(
                sessionID: mobileSessionID
            )
            for stale in publishedTabs where !currentIDs.contains(stale.id.rawValue) {
                if let closed = try await repository.closeLocalMobileTab(stale.id.rawValue) {
                    try await syncBridge?.enqueue(closed)
                }
            }

            let session = try await repository.publishLocalMobileSession(
                sessionID: mobileSessionID,
                deviceName: mobileDeviceName,
                deviceKind: mobileDeviceKind,
                workspaceID: currentTabs.first?.workspaceID
            )
            try await syncBridge?.enqueue(session.device)
            try await syncBridge?.enqueue(session.session)

            let publishedByID = Dictionary(
                uniqueKeysWithValues: publishedTabs.map { ($0.id.rawValue, $0) }
            )
            for tab in currentTabs {
                guard let url = tab.url else { continue }
                let title = tab.title.isEmpty ? url : tab.title
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
                try await syncBridge?.enqueue(publication.device)
                try await syncBridge?.enqueue(publication.session)
                try await syncBridge?.enqueue(publication.tab)
            }
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func closePublishedMobileTab(_ id: UUID) async {
        do {
            guard let closed = try await repository.closeLocalMobileTab(id) else { return }
            try await syncBridge?.enqueue(closed)
            try await refreshLocalState()
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func sync() async {
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
                providerPrepared = true
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
            loadError = error.localizedDescription
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
        do {
            let tombstones = try await repository.applyHistoryRetention(days: days)
            for visit in tombstones {
                try await syncBridge?.enqueue(visit)
            }
            defaults.set(days, forKey: CompanionSyncPreferences.historyRetentionDaysKey)
            historyRetentionDays = days
            try await refreshLocalState()
            if !tombstones.isEmpty {
                await sync()
            }
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func deleteHistoryVisit(_ id: HistoryVisitID) async {
        do {
            let tombstone = try await repository.deleteHistoryVisit(id)
            try await syncBridge?.enqueue(tombstone)
            try await refreshLocalState()
            await syncIfNeeded(afterDeleting: [tombstone])
        } catch {
            loadError = error.localizedDescription
        }
    }

    public func deleteHistory(sinceMilliseconds: UInt64) async {
        do {
            let tombstones = try await repository.deleteHistory(
                sinceMilliseconds: sinceMilliseconds
            )
            for tombstone in tombstones {
                try await syncBridge?.enqueue(tombstone)
            }
            try await refreshLocalState()
            await syncIfNeeded(afterDeleting: tombstones)
        } catch {
            loadError = error.localizedDescription
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
            loadError = error.localizedDescription
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
            loadError = error.localizedDescription
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
            loadError = error.localizedDescription
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
            loadError = error.localizedDescription
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
        do {
            let node = try await repository.createTreeNode(
                workspaceID: workspaceID,
                parentID: parentID,
                kind: kind,
                title: title,
                url: url
            )
            try await syncBridge?.enqueue(node)
            try await refreshLocalState()
            return node
        } catch {
            loadError = error.localizedDescription
            return nil
        }
    }

    private func refreshLocalState() async throws {
        snapshot = try await repository.currentSnapshot()
        searchResults = try await repository.search("")
        loadError = nil
    }

}
