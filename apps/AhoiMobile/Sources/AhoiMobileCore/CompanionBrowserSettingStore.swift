import Foundation
import AhoiCloudKitSpike

extension LocalFirstRepository {
    /// The native search picker authors this one supported choice. It never
    /// exports a template URL, custom endpoint, query, credential or profile path.
    @discardableResult
    public func setBrowserSearchEngine(
        _ engine: MobileSearchEngine?
    ) async throws -> CompanionPermittedSettingRecord {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        let id = CompanionBrowserSettingCatalog.searchEngineSettingID
        let recordID = CompanionBrowserSettingCatalog.recordID(for: id)
        let valueJSON = try engine.map {
            String(decoding: try JSONEncoder().encode($0.rawValue), as: UTF8.self)
        } ?? "null"
        if let existing = snapshot.productRecords.permittedSettings.first(where: {
            $0.id == recordID && !$0.isDeleted && $0.valueJSON == valueJSON
        }) { return existing }
        let before = snapshot
        let beforeClock = clock
        var version = try nextVersion()
        let fields: Set<String> = ["setting_id", "value_json", "tombstone"]
        if let existing = snapshot.productRecords.permittedSettings.first(where: {
            $0.id == recordID
        }) {
            version.fieldVersions = existing.version.normalized(for: fields).fieldVersions
            version.fieldVersions["value_json"] = version.modifiedAt
            if existing.isDeleted { version.fieldVersions["tombstone"] = version.modifiedAt }
        } else {
            version = version.normalized(for: fields)
        }
        let record = try CompanionPermittedSettingRecord(
            id: recordID, settingID: id, valueJSON: valueJSON,
            version: version, tombstone: nil
        )
        snapshot.productRecords.permittedSettings.replace(record) { $0.id == recordID }
        do {
            try await persist()
            return record
        } catch {
            snapshot = before
            clock = beforeClock
            throw error
        }
    }
}
