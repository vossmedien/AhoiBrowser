import Foundation

/// Serializes session writes and coalesces bursts to the newest revision.
/// This prevents a slower, older task from overwriting a newer tab snapshot.
public actor MobileBrowserSessionSaveCoordinator {
    private let store: any MobileBrowserSessionStoring
    private var pending: (revision: UInt64, snapshot: MobileBrowserSessionSnapshot)?
    private var isDraining = false
    private var lastCommittedRevision: UInt64 = 0

    public init(store: any MobileBrowserSessionStoring) {
        self.store = store
    }

    public func enqueue(
        _ snapshot: MobileBrowserSessionSnapshot,
        revision: UInt64
    ) async throws {
        guard revision > lastCommittedRevision else { return }
        if pending.map({ revision > $0.revision }) != false {
            pending = (revision, snapshot)
        }
        guard !isDraining else { return }

        isDraining = true
        defer { isDraining = false }
        while let next = pending {
            pending = nil
            guard next.revision > lastCommittedRevision else { continue }
            try await store.save(next.snapshot)
            lastCommittedRevision = next.revision
        }
    }
}
