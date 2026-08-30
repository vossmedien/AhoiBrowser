import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    public func applyHistoryRetention(
        days: Int,
        nowMilliseconds: UInt64 = UInt64(Date().timeIntervalSince1970 * 1_000)
    ) async throws -> [HistoryVisit] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard CompanionSyncPreferences.historyRetentionChoices.contains(days) else {
            throw LocalCompanionStoreError.invalidSnapshot
        }
        guard days != -1 else { return [] }
        let retentionMilliseconds = UInt64(days) * 24 * 60 * 60 * 1_000
        let cutoff = nowMilliseconds > retentionMilliseconds
            ? nowMilliseconds - retentionMilliseconds
            : 0
        var deleted: [HistoryVisit] = []
        for index in snapshot.history.indices
            where !snapshot.history[index].isDeleted
                && snapshot.history[index].visitedAt.physicalMilliseconds < cutoff {
            let previous = snapshot.history[index]
            let version = try nextVersion()
            var candidate = previous
            candidate.version = version
            candidate.tombstone = makeTombstone(
                entityID: previous.id.rawValue,
                version: version,
                parentID: nil,
                orderKey: nil
            )
            candidate = CompanionReadModelFieldMerge.stampLocal(
                previous: previous,
                candidate: candidate
            )
            snapshot.history[index] = candidate
            deleted.append(candidate)
        }
        if !deleted.isEmpty {
            try await persist()
        }
        return deleted
    }

    /// Deletes one visible visit through the same tombstone path used by
    /// retention so the operation converges on every synced device.
    @discardableResult
    public func deleteHistoryVisit(_ id: HistoryVisitID) async throws -> HistoryVisit {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard let index = snapshot.history.firstIndex(where: {
            $0.id == id && !$0.isDeleted
        }) else {
            throw LocalCompanionStoreError.notFound
        }
        let deleted = try tombstoneHistoryVisit(at: index)
        try await persist()
        return deleted
    }

    /// Deletes visits at or after the supplied instant. Passing zero removes
    /// all visible visits. Returned tombstones are ready for the sync outbox.
    @discardableResult
    public func deleteHistory(
        sinceMilliseconds: UInt64
    ) async throws -> [HistoryVisit] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let indexes = snapshot.history.indices.filter {
            !snapshot.history[$0].isDeleted &&
                snapshot.history[$0].visitedAt.physicalMilliseconds >= sinceMilliseconds
        }
        let deleted = try indexes.map(tombstoneHistoryVisit(at:))
        if !deleted.isEmpty {
            try await persist()
        }
        return deleted
    }

    private func tombstoneHistoryVisit(at index: Int) throws -> HistoryVisit {
        let previous = snapshot.history[index]
        let version = try nextVersion()
        var candidate = previous
        candidate.version = version
        candidate.tombstone = makeTombstone(
            entityID: previous.id.rawValue,
            version: version,
            parentID: nil,
            orderKey: nil
        )
        candidate = CompanionReadModelFieldMerge.stampLocal(
            previous: previous,
            candidate: candidate
        )
        snapshot.history[index] = candidate
        return candidate
    }

}
