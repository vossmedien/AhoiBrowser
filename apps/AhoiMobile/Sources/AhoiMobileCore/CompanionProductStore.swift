import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    @discardableResult
    public func upsert(
        _ incoming: CompanionAppearanceRecord
    ) async throws -> CompanionAppearanceRecord {
        try await load()
        let result = try selectRecord(
            snapshot.productRecords.appearance.first { $0.id == incoming.id },
            incoming
        )
        snapshot.productRecords.appearance.replace(result) { $0.id == incoming.id }
        try await persist()
        return result
    }

    @discardableResult
    public func upsert(
        _ incoming: CompanionPermittedSettingRecord
    ) async throws -> CompanionPermittedSettingRecord {
        try await load()
        let result = try selectRecord(
            snapshot.productRecords.permittedSettings.first { $0.id == incoming.id },
            incoming
        )
        snapshot.productRecords.permittedSettings.replace(result) { $0.id == incoming.id }
        try await persist()
        return result
    }

    @discardableResult
    public func upsert(
        _ incoming: CompanionExtensionInventoryRecord
    ) async throws -> CompanionExtensionInventoryRecord {
        try await load()
        let result = try selectRecord(
            snapshot.productRecords.extensionInventory.first { $0.id == incoming.id },
            incoming
        )
        snapshot.productRecords.extensionInventory.replace(result) { $0.id == incoming.id }
        try await persist()
        return result
    }

    @discardableResult
    public func upsert(
        _ incoming: CompanionDeveloperAssetRecord
    ) async throws -> CompanionDeveloperAssetRecord {
        try await load()
        let result = try selectRecord(
            snapshot.productRecords.developerAssets.first { $0.id == incoming.id },
            incoming
        )
        snapshot.productRecords.developerAssets.replace(result) { $0.id == incoming.id }
        try await persist()
        return result
    }

    private func selectRecord<Record: Equatable>(
        _ existing: Record?,
        _ incoming: Record
    ) throws -> Record where Record: CompanionVersionedProductRecord {
        guard let existing else { return incoming }
        if incoming.syncVersion > existing.syncVersion { return incoming }
        if incoming.syncVersion < existing.syncVersion { return existing }
        guard incoming == existing else {
            throw CompanionProductRecordError.equalVersionConflict
        }
        return existing
    }
}

protocol CompanionVersionedProductRecord {
    var syncVersion: SyncVersion { get }
}

extension CompanionAppearanceRecord: CompanionVersionedProductRecord {
    var syncVersion: SyncVersion { version }
}
extension CompanionPermittedSettingRecord: CompanionVersionedProductRecord {
    var syncVersion: SyncVersion { version }
}
extension CompanionExtensionInventoryRecord: CompanionVersionedProductRecord {
    var syncVersion: SyncVersion { version }
}
extension CompanionDeveloperAssetRecord: CompanionVersionedProductRecord {
    var syncVersion: SyncVersion { version }
}

private extension Array {
    mutating func replace(_ element: Element, where predicate: (Element) -> Bool) {
        if let index = firstIndex(where: predicate) {
            self[index] = element
        } else {
            append(element)
        }
    }
}
