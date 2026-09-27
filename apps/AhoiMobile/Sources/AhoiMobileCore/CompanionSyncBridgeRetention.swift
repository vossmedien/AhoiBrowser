import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    func performBoundedSyncNow() async throws {
        // Reconcile a committed domain marker before a rebuilt CKSyncEngine
        // requeues an old encrypted transport copy. A missing server receipt
        // merely defers maintenance; it never authorizes local payload loss.
        try await compactAcknowledgedWorkspaces()
        var passID: UInt64?
        do {
            passID = try await provider.fetchChanges()
            try await importFetchedRecords()
            // Import can enqueue a composite record after field-level conflict
            // resolution. A second import/send covers one server conflict.
            guard let passID else { throw CloudKitSyncProviderError.boundedSyncPassRequired }
            try await provider.sendPendingChanges(passID: passID)
            try await importFetchedRecords()
            try await provider.sendPendingChanges(passID: passID)
            try await provider.finalizeBoundedSync(passID: passID)
        } catch {
            if let passID { provider.abortBoundedSyncPass(passID) }
            throw error
        }
        // savedRecords may have acknowledged a tombstone in this pass.
        try await compactAcknowledgedWorkspaces()
    }

    private func compactAcknowledgedWorkspaces() async throws {
        let now = UInt64(max(0, Date().timeIntervalSince1970 * 1_000))
        let snapshot = try await repository.currentSnapshot()
        let expired = snapshot.workspaces.compactMap { workspace -> WorkspaceID? in
            guard let tombstone = workspace.tombstone,
                  Self.retentionExpired(tombstone, now: now) else { return nil }
            return workspace.id
        }
        let committed = snapshot.deletionWatermarks.compactMap { watermark -> WorkspaceID? in
            watermark.dataClass == .workspace
                ? WorkspaceID(rawValue: watermark.entityID) : nil
        }
        for id in Set(expired).union(committed).sorted() {
            guard let record = try await provider.locallyPersistedRecord(
                forRecordID: id.rawValue),
                  record.dataClass == .workspace,
                  record.recordID == id.rawValue,
                  record.entityID == id.rawValue,
                  let tombstone = record.tombstone,
                  Self.retentionExpired(tombstone, now: now),
                  let plaintext = try? codec.openData(record),
                  let decoded = try? wireCodec.decodeWorkspace(
                      record, plaintext: plaintext) else { continue }
            let current = snapshot.workspaces.filter { $0.id == id }
            if current.count == 1 {
                guard let currentBytes = try? wireCodec.encode(current[0]),
                      let decodedBytes = try? wireCodec.encode(decoded),
                      currentBytes == decodedBytes else { continue }
            } else {
                guard current.isEmpty,
                      let watermark = snapshot.deletionWatermarks.first(where: {
                          $0.dataClass == .workspace && $0.entityID == id.rawValue
                      }),
                      watermark.version == decoded.version,
                      watermark.mergedInto == decoded.mergedInto else { continue }
            }
            let repository = self.repository
            do {
                _ = try await provider.compactAcknowledgedWorkspace(record) {
                    _ = try await repository.compactWorkspaceTombstone(
                        matching: decoded, nowMilliseconds: now)
                }
            } catch CompanionWorkspaceCompactionError.sourceChanged {
                // A local edit won the race after the snapshot scan. Its next
                // exact candidate will be considered on the next bounded pass.
                continue
            }
        }
    }

    private static func retentionExpired(_ tombstone: Tombstone, now: UInt64) -> Bool {
        let (floor, overflow) = tombstone.deletedAt.physicalMilliseconds
            .addingReportingOverflow(30 * 24 * 60 * 60 * 1_000)
        return !overflow && now >= floor && now >= tombstone.purgeAfterMilliseconds
    }
}
