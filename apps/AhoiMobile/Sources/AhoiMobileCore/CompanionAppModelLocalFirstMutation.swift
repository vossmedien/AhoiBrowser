import Foundation
import AhoiCloudKitSpike

struct CompanionMobilePublicationBatch: Sendable {
    var devices: [Device] = []
    var workspaces: [Workspace] = []
    var nodes: [TreeNode] = []
    var sessions: [DeviceSession] = []
    var tabs: [RemoteTab] = []

    func enqueue(using bridge: CompanionSyncBridge) async throws {
        for device in devices { try await bridge.enqueue(device) }
        for workspace in workspaces { try await bridge.enqueue(workspace) }
        for node in nodes { try await bridge.enqueue(node) }
        for session in sessions { try await bridge.enqueue(session) }
        for tab in tabs { try await bridge.enqueue(tab) }
    }
}

extension CompanionAppModel {
    /// Commits the local authority, refreshes the visible projection, and only
    /// then attempts to queue the encrypted outbound representation. A queue
    /// failure must never turn an already durable local change into a false
    /// mutation failure or invite the user to create a duplicate.
    @discardableResult
    func performLocalFirstMutation<Value>(
        _ localMutation: @MainActor () async throws -> Value,
        didCommit: @MainActor (Value) -> Void = { _ in },
        ignoreFailure: @MainActor (Error) -> Bool = { _ in false },
        enqueue: @MainActor (Value) async throws -> Void
    ) async -> Value? {
        let outboundGeneration = syncGeneration
        let outboundBridge = syncBridge
        let committed: Value
        do {
            committed = try await localMutation()
        } catch {
            if ignoreFailure(error) { return nil }
            presentOperationFailure(error)
            return nil
        }
        didCommit(committed)

        do {
            try await refreshLocalState()
        } catch {
            presentLocalProjectionFailure(error)
        }

        do {
            // Local work remains valid offline. An await during persistence or
            // readback must not enqueue it through a newly linked runtime.
            if syncGeneration == outboundGeneration, syncBridge === outboundBridge {
                try await enqueue(committed)
            } else {
                localSnapshotReseedRequired = true
            }
        } catch {
            localSnapshotReseedRequired = true
            presentSyncQueueFailure(error)
        }
        await refreshSyncVisibleUITestEvidenceIfNeeded()
        return committed
    }
}
