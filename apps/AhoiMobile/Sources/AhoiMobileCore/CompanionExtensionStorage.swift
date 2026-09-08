import CoreFoundation
import Foundation
import AhoiCloudKitSpike

/// Recognized desktop-only sync storage, not arbitrary extension data. Native
/// applies only to the reviewed, trusted installed package; iOS is read-only.
public struct CompanionExtensionStorage: Equatable, Sendable {
    public static let extensionID = "dbepggeogbaibhgnhhndojpepiihcmeb"
    public static let reviewedVersion = "2.4.2"
    public static let defaults: [String: Bool] = [
        "smoothScroll": true, "filterLinkHints": false,
        "hideHud": false, "hideUpdateNotifications": false,
    ]
    public let key: String
    public let value: Bool?

    public init(key: String, value: Bool?) throws {
        guard Self.defaults[key] != nil else {
            throw CompanionProductRecordError.invalidExtensionInventory
        }
        self.key = key
        self.value = value
    }

    public var settingID: String { Self.settingID(for: key) }
    public var recordID: UUID { CompanionBrowserSettingCatalog.recordID(for: settingID) }
    public static let recordIDs: Set<UUID> = Set(defaults.keys.map {
        CompanionBrowserSettingCatalog.recordID(for: settingID(for: $0))
    })

    public func record(version: SyncVersion) throws -> CompanionPermittedSettingRecord {
        try CompanionPermittedSettingRecord(
            id: recordID, settingID: settingID,
            valueJSON: value.map { $0 ? "true" : "false" } ?? "null",
            version: version.normalized(for: ["setting_id", "value_json", "tombstone"]),
            tombstone: nil
        )
    }

    public static func decode(_ record: CompanionPermittedSettingRecord) -> Self? {
        guard !record.isDeleted,
              record.id == CompanionBrowserSettingCatalog.recordID(for: record.settingID),
              let key = defaults.keys.first(where: { settingID(for: $0) == record.settingID }),
              let raw = try? JSONSerialization.jsonObject(
                with: Data(record.valueJSON.utf8), options: [.fragmentsAllowed]
              ) else { return nil }
        if raw is NSNull { return try? Self(key: key, value: nil) }
        guard let number = raw as? NSNumber,
              CFGetTypeID(number) == CFBooleanGetTypeID() else { return nil }
        return try? Self(key: key, value: number.boolValue)
    }

    private static func settingID(for key: String) -> String {
        "ahoi.extension.\(extensionID).storage.sync.\(key)"
    }
}
