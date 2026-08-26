import Foundation
import SwiftUI
import AhoiCloudKitSpike

public struct CompanionRemoteCommandStatusItem: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let action: String
    public let targetDeviceID: DeviceID
    public let status: RemoteCommandStatus
    public let resultCode: String
    public let expiresAtMilliseconds: UInt64

    public var isTerminal: Bool {
        status == .executed || status == .failed
    }
}

@MainActor
public final class CompanionAppModel: ObservableObject {
    @Published public private(set) var snapshot: CompanionSnapshot = .empty
    @Published public private(set) var searchResults: [CompanionSearchResult] = []
    @Published public private(set) var loadError: String?
    @Published public private(set) var syncStatus: CloudKitSyncStatus?
    @Published public private(set) var remoteControlIdentity: RemoteControlProvisioningIdentity?
    @Published public private(set) var remoteCommandStatus: String?
    @Published public private(set) var recentRemoteCommands: [CompanionRemoteCommandStatusItem] = []
    @Published public private(set) var syncSafetyState = CloudKitSyncSafetyState()
    @Published public private(set) var historyRetentionDays: Int
    @Published public private(set) var isSyncConfigured: Bool

    public let repository: LocalFirstRepository
    private let defaults: UserDefaults
    private var syncProvider: CloudKitSyncProvider?
    private var syncBridge: CompanionSyncBridge?
    private let syncRuntimeFactory: (() -> CompanionCloudKitRuntime?)?
    private var providerPrepared = false
    private var commandLabels: [UUID: String] = [:]
    private var commandFollowUpTasks: [UUID: Task<Void, Never>] = [:]

    public init(
        repository: LocalFirstRepository,
        syncProvider: CloudKitSyncProvider? = nil,
        syncBridge: CompanionSyncBridge? = nil,
        syncRuntimeFactory: (() -> CompanionCloudKitRuntime?)? = nil,
        defaults: UserDefaults = .standard
    ) {
        self.repository = repository
        self.defaults = defaults
        self.syncProvider = syncProvider
        self.syncBridge = syncBridge
        self.syncRuntimeFactory = syncRuntimeFactory
        self.isSyncConfigured = syncProvider != nil && syncBridge != nil
        let storedRetention = defaults.integer(
            forKey: CompanionSyncPreferences.historyRetentionDaysKey
        )
        self.historyRetentionDays =
            CompanionSyncPreferences.historyRetentionChoices.contains(storedRetention)
            ? storedRetention
            : CompanionSyncPreferences.defaultHistoryRetentionDays
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
                remoteControlIdentity = try? await syncBridge.remoteControlIdentity()
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

    public func sync() async {
        guard let syncProvider else { return }
        do {
            if !providerPrepared {
                try await syncProvider.prepare()
                if let syncBridge {
                    try await syncBridge.enqueueLocalSnapshot()
                }
                providerPrepared = true
            }
            if let syncBridge {
                try await syncBridge.syncNow()
                snapshot = try await repository.currentSnapshot()
                searchResults = try await repository.search("")
                await refreshRemoteCommandStates(using: syncBridge)
            } else {
                try await syncProvider.syncNow()
            }
        } catch {
            loadError = error.localizedDescription
        }
        syncStatus = syncProvider.status()
        syncSafetyState = syncProvider.safetyState()
    }

    /// Applies the user-owned transport preference at runtime. Disabling drops
    /// the CKSyncEngine/provider immediately while retaining all local data;
    /// enabling constructs CKContainer only after this call.
    public func setSyncEnabled(_ enabled: Bool) async {
        if !enabled {
            await syncProvider?.cancel()
            providerPrepared = false
            syncProvider = nil
            syncBridge = nil
            isSyncConfigured = false
            syncStatus = nil
            syncSafetyState = .init()
            remoteControlIdentity = nil
            return
        }
        guard syncProvider == nil else { return }
        guard let runtime = syncRuntimeFactory?() else {
            isSyncConfigured = false
            loadError = CompanionL10n.string(
                "sync.configuration_missing",
                fallback: "CloudKit is not fully configured. Local data remains available."
            )
            return
        }
        syncProvider = runtime.provider
        syncBridge = runtime.bridge
        isSyncConfigured = true
        remoteControlIdentity = try? await runtime.bridge.remoteControlIdentity()
        await sync()
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

    public func confirmAccountTransition(allowLocalUpload: Bool) async {
        guard let syncProvider else { return }
        do {
            try await syncProvider.confirmAccountTransition(
                allowLocalUpload: allowLocalUpload
            )
            syncSafetyState = syncProvider.safetyState()
            await sync()
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

    public func remotelyOpen(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .open(.init(url: tab.url, workspaceID: tab.workspaceID)),
            target: tab.deviceID,
            action: CompanionL10n.string(
                "remote.action.open",
                fallback: "Open"
            )
        )
    }

    public func sendLink(
        _ url: String,
        to target: DeviceID,
        workspaceID: WorkspaceID?
    ) async {
        await sendRemoteCommand(
            .open(.init(url: url, workspaceID: workspaceID)),
            target: target,
            action: CompanionL10n.string(
                "remote.action.send_link",
                fallback: "Send link"
            )
        )
    }

    public func remotelyFocus(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .focus(.init(tabID: tab.id, context: .normal)),
            target: tab.deviceID,
            action: CompanionL10n.string(
                "remote.action.focus",
                fallback: "Focus"
            )
        )
    }

    public func remotelyClose(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .close([.init(tabID: tab.id, context: .normal)]),
            target: tab.deviceID,
            action: CompanionL10n.string(
                "remote.action.close",
                fallback: "Close"
            )
        )
    }

    public func visibleTabs(for workspaceID: WorkspaceID?) -> [RemoteTab] {
        snapshot.visibleRemoteTabs.filter { tab in
            guard let workspaceID else { return true }
            return tab.workspaceID == workspaceID
        }
    }

    public var actionableRemoteTabIDs: Set<TabID> {
        Set(snapshot.visibleRemoteTabs.filter {
            snapshot.isRemoteTabActionable($0)
        }.map(\.id))
    }

    private func sendRemoteCommand(
        _ command: RemoteCommand,
        target: DeviceID,
        action: String
    ) async {
        guard let syncBridge else {
            remoteCommandStatus = CompanionL10n.string(
                "remote.not_configured",
                fallback: "Remote control is not configured."
            )
            return
        }
        do {
            remoteCommandStatus = CompanionL10n.format(
                "remote.signing",
                fallback: "%@ is being signed…",
                action
            )
            let state = try await syncBridge.enqueueRemoteCommand(
                targetDeviceID: target,
                command: command
            )
            commandLabels[state.id] = action
            updateRemoteCommandStatusItem(state, action: action)
            remoteCommandStatus = CompanionL10n.format(
                "remote.queued_securely",
                fallback: "%@ was queued securely.",
                action
            )
            try await syncBridge.syncNow()
            let updated = await syncBridge.remoteCommandState(state.id)
            if let updated {
                updateRemoteCommandStatusItem(updated, action: action)
            }
            remoteCommandStatus = statusText(updated?.status ?? .queued, action: action)
            snapshot = try await repository.currentSnapshot()
            syncStatus = syncProvider?.status()
            syncSafetyState = syncProvider?.safetyState() ?? .init()
            loadError = nil
            beginCommandFollowUp(
                commandID: state.id,
                expiresAtMilliseconds: state.envelope.payload.expiresAtMilliseconds
            )
        } catch {
            remoteCommandStatus = CompanionL10n.format(
                "remote.send_failed",
                fallback: "%@ was not sent.",
                action
            )
            loadError = error.localizedDescription
            syncStatus = syncProvider?.status()
        }
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

    private func statusText(_ status: RemoteCommandStatus, action: String) -> String {
        switch status {
        case .queued:
            CompanionL10n.format(
                "remote.status.queued",
                fallback: "%@ was sent; confirmation is pending.",
                action
            )
        case .delivered:
            CompanionL10n.string(
                "remote.status.delivered",
                fallback: "The Mac verified the command."
            )
        case .executed:
            CompanionL10n.format(
                "remote.status.executed",
                fallback: "%@ was completed on the Mac.",
                action
            )
        case .failed:
            CompanionL10n.string(
                "remote.status.failed",
                fallback: "The Mac rejected the command safely."
            )
        }
    }

    private func updateRemoteCommandStatusItem(
        _ state: RemoteCommandState,
        action: String
    ) {
        let item = CompanionRemoteCommandStatusItem(
            id: state.id,
            action: action,
            targetDeviceID: state.envelope.payload.targetDeviceID,
            status: state.status,
            resultCode: state.resultCode,
            expiresAtMilliseconds: state.envelope.payload.expiresAtMilliseconds
        )
        recentRemoteCommands.removeAll { $0.id == item.id }
        recentRemoteCommands.insert(item, at: 0)
        if recentRemoteCommands.count > 20 {
            recentRemoteCommands.removeLast(recentRemoteCommands.count - 20)
        }
    }

    private func refreshRemoteCommandStates(using bridge: CompanionSyncBridge) async {
        let states = await bridge.remoteCommandStates(Set(commandLabels.keys))
        for state in states {
            let action = commandLabels[state.id] ?? CompanionL10n.string(
                "remote.action.generic",
                fallback: "Remote command"
            )
            updateRemoteCommandStatusItem(state, action: action)
            if recentRemoteCommands.first(where: { $0.id == state.id })?.isTerminal == true {
                commandFollowUpTasks[state.id]?.cancel()
                commandFollowUpTasks[state.id] = nil
            }
        }
    }

    private func beginCommandFollowUp(
        commandID: UUID,
        expiresAtMilliseconds: UInt64
    ) {
        commandFollowUpTasks[commandID]?.cancel()
        commandFollowUpTasks[commandID] = Task { [weak self] in
            while !Task.isCancelled {
                let now = UInt64(Date().timeIntervalSince1970 * 1_000)
                guard now < expiresAtMilliseconds else { break }
                try? await Task.sleep(for: .seconds(5))
                guard let self, let bridge = self.syncBridge else { break }
                do {
                    try await bridge.syncNow()
                    await self.refreshRemoteCommandStates(using: bridge)
                    self.syncStatus = self.syncProvider?.status()
                    if self.recentRemoteCommands.first(where: {
                        $0.id == commandID
                    })?.isTerminal == true {
                        break
                    }
                } catch {
                    self.syncStatus = self.syncProvider?.status()
                }
            }
            self?.commandFollowUpTasks[commandID] = nil
        }
    }
}
