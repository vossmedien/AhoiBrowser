import Foundation
import SwiftUI
import AhoiCloudKitSpike

@MainActor
public final class CompanionAppModel: ObservableObject {
    @Published public private(set) var snapshot: CompanionSnapshot = .empty
    @Published public private(set) var searchResults: [CompanionSearchResult] = []
    @Published public private(set) var loadError: String?
    @Published public private(set) var syncStatus: CloudKitSyncStatus?
    @Published public private(set) var remoteControlIdentity: RemoteControlProvisioningIdentity?
    @Published public private(set) var remoteCommandStatus: String?
    @Published public private(set) var isSyncConfigured: Bool

    public let repository: LocalFirstRepository
    private var syncProvider: CloudKitSyncProvider?
    private var syncBridge: CompanionSyncBridge?
    private let syncRuntimeFactory: (() -> CompanionCloudKitRuntime?)?
    private var providerPrepared = false

    public init(
        repository: LocalFirstRepository,
        syncProvider: CloudKitSyncProvider? = nil,
        syncBridge: CompanionSyncBridge? = nil,
        syncRuntimeFactory: (() -> CompanionCloudKitRuntime?)? = nil
    ) {
        self.repository = repository
        self.syncProvider = syncProvider
        self.syncBridge = syncBridge
        self.syncRuntimeFactory = syncRuntimeFactory
        self.isSyncConfigured = syncProvider != nil && syncBridge != nil
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
            } else {
                try await syncProvider.syncNow()
            }
        } catch {
            loadError = error.localizedDescription
        }
        syncStatus = syncProvider.status()
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
            remoteControlIdentity = nil
            return
        }
        guard syncProvider == nil else { return }
        guard let runtime = syncRuntimeFactory?() else {
            isSyncConfigured = false
            loadError = "CloudKit ist nicht vollständig eingerichtet. Lokale Daten bleiben verfügbar."
            return
        }
        syncProvider = runtime.provider
        syncBridge = runtime.bridge
        isSyncConfigured = true
        remoteControlIdentity = try? await runtime.bridge.remoteControlIdentity()
        await sync()
    }

    public func remotelyOpen(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .open(.init(url: tab.url, workspaceID: tab.workspaceID)),
            target: tab.deviceID,
            action: "Öffnen"
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
            action: "Link senden"
        )
    }

    public func remotelyFocus(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .focus(.init(tabID: tab.id, context: .normal)),
            target: tab.deviceID,
            action: "Fokussieren"
        )
    }

    public func remotelyClose(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .close([.init(tabID: tab.id, context: .normal)]),
            target: tab.deviceID,
            action: "Schließen"
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
            remoteCommandStatus = "Fernsteuerung ist nicht eingerichtet."
            return
        }
        do {
            remoteCommandStatus = "\(action) wird signiert …"
            let state = try await syncBridge.enqueueRemoteCommand(
                targetDeviceID: target,
                command: command
            )
            remoteCommandStatus = "\(action) ist sicher vorgemerkt."
            try await syncBridge.syncNow()
            let updated = await syncBridge.remoteCommandState(state.id)
            remoteCommandStatus = statusText(updated?.status ?? .queued, action: action)
            snapshot = try await repository.currentSnapshot()
            syncStatus = syncProvider?.status()
            loadError = nil
        } catch {
            remoteCommandStatus = "\(action) wurde nicht gesendet."
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
        case .queued: "\(action) wurde gesendet; Bestätigung steht aus."
        case .delivered: "Der Mac hat den Befehl geprüft."
        case .executed: "\(action) wurde am Mac ausgeführt."
        case .failed: "Der Mac hat den Befehl sicher abgelehnt."
        }
    }
}
