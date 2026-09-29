import Foundation

extension MobileBrowserController {
    public func flushSession() async {
        _ = await flushSessionForIntentAcknowledgment()
    }

    /// The coordinator may coalesce into a newer snapshot. An older captured
    /// pending set is conservative for receipts known BEFORE this flush: UUIDs
    /// never reappear after acknowledgment. New intent IDs are not pruned.
    func flushSessionForIntentAcknowledgment() async -> Set<UUID>? {
        let performanceStart = performanceRecorder.beginSessionFlush()
        let snapshot = persistentSnapshot()
        sessionRevision &+= 1
        do {
            try await saveCoordinator.enqueue(snapshot, revision: sessionRevision)
            performanceRecorder.completeSessionFlush(startedAt: performanceStart, succeeded: true)
            return Set(snapshot.tabs.flatMap { $0.pendingSharedMutations.map(\.id) })
        } catch {
            performanceRecorder.completeSessionFlush(startedAt: performanceStart, succeeded: false)
            lastError = MobileBrowserSessionFailurePresentation.saveMessage
            return nil
        }
    }
}

extension LocalFirstRepository {
    func pruneMobileIntentReceipts(knownBeforeFlush: Set<UUID>, stillPending: Set<UUID>) async throws {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let removable = knownBeforeFlush.subtracting(stillPending)
        guard !snapshot.mobileAppliedIntents.isDisjoint(with: removable) else { return }
        let previous = snapshot.mobileAppliedIntents
        snapshot.mobileAppliedIntents.subtract(removable)
        do { try await persist() }
        catch { snapshot.mobileAppliedIntents = previous; throw error }
    }
}

extension CompanionAppModel {
    func flushSharedBrowserSession(_ browser: MobileBrowserController) async {
        let known = snapshot.mobileAppliedIntents
        guard let pending = await browser.flushSessionForIntentAcknowledgment() else { return }
        do {
            try await repository.pruneMobileIntentReceipts(knownBeforeFlush: known, stillPending: pending)
        } catch {
            // Retaining an old receipt is safe; never remove it on failed flush
            // or turn successful local browser persistence into a false failure.
            presentOperationFailure(error)
        }
    }
}
