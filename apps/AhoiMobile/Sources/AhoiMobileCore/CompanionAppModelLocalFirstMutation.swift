import Foundation
import AhoiCloudKitSpike

struct CompanionMobilePublicationBatch {
    var devices: [Device] = []
    var sessions: [DeviceSession] = []
    var tabs: [RemoteTab] = []

    func enqueue(using bridge: CompanionSyncBridge) async throws {
        for device in devices { try await bridge.enqueue(device) }
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
        enqueue: @MainActor (Value) async throws -> Void
    ) async -> Value? {
        let committed: Value
        do {
            committed = try await localMutation()
        } catch {
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
            try await enqueue(committed)
        } catch {
            localSnapshotReseedRequired = true
            presentSyncQueueFailure(error)
        }
        await refreshSyncVisibleUITestEvidenceIfNeeded()
        return committed
    }
}
