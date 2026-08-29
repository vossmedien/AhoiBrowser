import Foundation

/// Serializes session writes and coalesces bursts to the newest revision.
/// This prevents a slower, older task from overwriting a newer tab snapshot.
public actor MobileBrowserSessionSaveCoordinator {
    private struct CommitWaiter {
        let id: UUID
        let revision: UInt64
        let continuation: CheckedContinuation<Void, any Error>
    }

    private let store: any MobileBrowserSessionStoring
    private var pending: (revision: UInt64, snapshot: MobileBrowserSessionSnapshot)?
    private var isDraining = false
    private var lastCommittedRevision: UInt64 = 0
    private var waiters: [CommitWaiter] = []

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
        if isDraining {
            try await waitUntilCommitted(revision)
            return
        }

        isDraining = true
        do {
            while let next = pending {
                pending = nil
                guard next.revision > lastCommittedRevision else { continue }
                do {
                    try await store.save(next.snapshot)
                } catch {
                    if pending.map({ next.revision > $0.revision }) != false {
                        pending = next
                    }
                    throw error
                }
                lastCommittedRevision = next.revision
                resumeCommittedWaiters()
            }
            isDraining = false
        } catch {
            isDraining = false
            failAllWaiters(with: error)
            throw error
        }

        // A newer enqueuer may have coalesced this revision while the actor was
        // re-entrant. Returning is only valid once this caller's requested
        // revision (or a newer one) is durably committed.
        try await waitUntilCommitted(revision)
    }

    private func waitUntilCommitted(_ revision: UInt64) async throws {
        guard revision > lastCommittedRevision else { return }
        let waiterID = UUID()
        try await withTaskCancellationHandler {
            try Task.checkCancellation()
            try await withCheckedThrowingContinuation { continuation in
                if revision <= lastCommittedRevision {
                    continuation.resume()
                } else if Task.isCancelled {
                    continuation.resume(throwing: CancellationError())
                } else {
                    waiters.append(.init(
                        id: waiterID,
                        revision: revision,
                        continuation: continuation
                    ))
                }
            }
        } onCancel: {
            Task { await self.cancelWaiter(waiterID) }
        }
    }

    private func cancelWaiter(_ id: UUID) {
        guard let index = waiters.firstIndex(where: { $0.id == id }) else { return }
        let waiter = waiters.remove(at: index)
        waiter.continuation.resume(throwing: CancellationError())
    }

    private func resumeCommittedWaiters() {
        let committed = waiters.filter { $0.revision <= lastCommittedRevision }
        waiters.removeAll { $0.revision <= lastCommittedRevision }
        for waiter in committed { waiter.continuation.resume() }
    }

    private func failAllWaiters(with error: any Error) {
        let failed = waiters
        waiters.removeAll()
        for waiter in failed { waiter.continuation.resume(throwing: error) }
    }
}
