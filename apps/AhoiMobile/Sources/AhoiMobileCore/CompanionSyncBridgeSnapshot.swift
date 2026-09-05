import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    /// Seeds transport from the durable local authority when sync is enabled
    /// after offline-only edits. Existing envelopes with the same authoritative
    /// version/tombstone metadata are reused byte-for-byte: AES-GCM nonces stay
    /// stable for unchanged data and the file-backed record store persists at
    /// most once for the complete seed.
    public func enqueueLocalSnapshot() async throws {
        let authorizationMutationEpoch = provider
            .currentDeveloperAssetAuthorizationMutationEpoch()
        let snapshot = try await repository.currentSnapshot()
        var existingByID: [UUID: SyncRecord] = [:]
        for record in try await provider.allRecords() {
            existingByID[record.recordID] = record
        }
        var records: [SyncRecord] = []
        var developerAssetIDs = Set<UUID>()

        func appendIfRequired(
            id: UUID,
            dataClass: SyncDataClass,
            version: SyncVersion,
            orderKey: OrderKey? = nil,
            tombstone: Tombstone?,
            plaintext: () throws -> Data
        ) throws {
            let canonicalPlaintext = try plaintext()
            guard shouldSeedTransport(
                existing: existingByID[id],
                id: id,
                dataClass: dataClass,
                version: version,
                orderKey: orderKey,
                canonicalPlaintext: canonicalPlaintext,
                tombstone: tombstone
            ) else { return }
            records.append(try codec.makeRecord(
                recordID: id,
                entityID: id,
                dataClass: dataClass,
                version: version,
                plaintext: canonicalPlaintext,
                orderKey: orderKey,
                tombstone: tombstone
            ))
        }

        for device in snapshot.devices {
            try appendIfRequired(
                id: device.id.rawValue,
                dataClass: .device,
                version: device.version,
                tombstone: device.tombstone
            ) { try wireCodec.encode(device) }
        }
        for workspace in snapshot.workspaces {
            try appendIfRequired(
                id: workspace.id.rawValue,
                dataClass: .workspace,
                version: workspace.version,
                tombstone: workspace.tombstone
            ) { try wireCodec.encode(workspace) }
        }
        for node in snapshot.treeNodes {
            try appendIfRequired(
                id: node.id.rawValue,
                dataClass: .treeNode,
                version: node.version,
                orderKey: node.orderKey,
                tombstone: node.tombstone
            ) { try wireCodec.encode(node) }
        }
        if bookmarkSyncEnabled {
            for bookmark in snapshot.bookmarks {
                try appendIfRequired(
                    id: bookmark.id.rawValue, dataClass: .bookmark,
                    version: bookmark.version, tombstone: bookmark.tombstone
                ) { try wireCodec.encode(bookmark) }
            }
        }
        for session in snapshot.sessions {
            try appendIfRequired(
                id: session.id.rawValue,
                dataClass: .deviceSession,
                version: session.version,
                tombstone: session.tombstone
            ) { try wireCodec.encode(session) }
        }
        for tab in snapshot.remoteTabs where tab.context == .normal {
            try appendIfRequired(
                id: tab.id.rawValue,
                dataClass: .deviceTab,
                version: tab.version,
                tombstone: tab.tombstone
            ) { try wireCodec.encode(tab) }
        }
        for visit in snapshot.history {
            try appendIfRequired(
                id: visit.id.rawValue,
                dataClass: .historyVisit,
                version: visit.version,
                tombstone: visit.tombstone
            ) { try wireCodec.encode(visit) }
        }
        for value in snapshot.productRecords.appearance {
            try appendIfRequired(
                id: value.id,
                dataClass: .appearance,
                version: value.version,
                tombstone: value.tombstone
            ) { try wireCodec.encode(value) }
        }
        for value in snapshot.productRecords.permittedSettings {
            try appendIfRequired(
                id: value.id,
                dataClass: .permittedSetting,
                version: value.version,
                tombstone: value.tombstone
            ) { try wireCodec.encode(value) }
        }
        for value in snapshot.productRecords.extensionInventory {
            try appendIfRequired(
                id: value.id,
                dataClass: .extensionInventory,
                version: value.version,
                tombstone: value.tombstone
            ) { try wireCodec.encode(value) }
        }
        for value in snapshot.productRecords.developerAssets
            where value.isDeleted || value.optedIn {
            developerAssetIDs.insert(value.id)
            try appendIfRequired(
                id: value.id,
                dataClass: .developerAsset,
                version: value.version,
                tombstone: value.tombstone
            ) { try wireCodec.encode(value) }
        }
        try await provider.enqueueLocalSnapshot(
            records,
            authorizedDeveloperAssetIDs: developerAssetIDs,
            scanStartedAtMutationEpoch: authorizationMutationEpoch
        )
    }

    private func shouldSeedTransport(
        existing: SyncRecord?,
        id: UUID,
        dataClass: SyncDataClass,
        version: SyncVersion,
        orderKey: OrderKey?,
        canonicalPlaintext: Data,
        tombstone: Tombstone?
    ) -> Bool {
        guard let existing else { return true }
        let metadataMatches = existing.recordID == id && existing.entityID == id &&
            existing.dataClass == dataClass &&
            existing.schemaVersion == version.schemaVersion &&
            existing.modifiedAt == version.modifiedAt &&
            existing.originatingDevice == version.modifiedBy &&
            existing.orderKey == orderKey &&
            existing.tombstone == tombstone
        if metadataMatches,
           let existingPlaintext = try? codec.openData(existing),
           existingPlaintext == canonicalPlaintext {
            return false
        }
        if existing.modifiedAt != version.modifiedAt {
            return existing.modifiedAt < version.modifiedAt
        }
        if existing.originatingDevice != version.modifiedBy {
            return existing.originatingDevice < version.modifiedBy
        }
        if existing.schemaVersion != version.schemaVersion {
            return existing.schemaVersion < version.schemaVersion
        }
        // Equal authority metadata with a different class/entity/tombstone is
        // corrupt transport metadata. Replace it from the durable domain model.
        return true
    }
}
