import CoreFoundation
import Foundation
import AhoiCloudKitSpike

/// Shared desired setup, not per-device observed inventory and not a claim
/// that WebKit can run a Chromium extension. The existing format3 setting value
/// carries the whole source/install/enable tuple atomically.
public struct CompanionExtensionSetup: Equatable, Sendable {
    public enum Source: String, Sendable {
        case chromeWebStore
        case pinnedUblockClassic
    }

    public let extensionID: String
    public let source: Source
    public let installed: Bool
    public let enabled: Bool

    public init(extensionID: String, source: Source, installed: Bool, enabled: Bool) throws {
        guard extensionID.utf8.count == 32,
              extensionID.utf8.allSatisfy({ (97...112).contains($0) }),
              installed || !enabled else {
            throw CompanionProductRecordError.invalidExtensionInventory
        }
        let pinned = "fkgkibajhfbepljeaefdnfnegdcjomkh"
        let legacyStore = "cjpalhdlnbpafiamejdnhcphjbkeiagm"
        guard source == .pinnedUblockClassic ? extensionID == pinned :
            (extensionID != pinned && extensionID != legacyStore) else {
            throw CompanionProductRecordError.invalidExtensionInventory
        }
        self.extensionID = extensionID
        self.source = source
        self.installed = installed
        self.enabled = enabled
    }

    public var settingID: String { Self.prefix + extensionID + Self.suffix }
    public var recordID: UUID { CompanionBrowserSettingCatalog.recordID(for: settingID) }

    public func record(version: SyncVersion) throws -> CompanionPermittedSettingRecord {
        let value: [String: Any] = [
            "source": source.rawValue, "installed": installed, "enabled": enabled,
        ]
        let json = try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
        return try CompanionPermittedSettingRecord(
            id: recordID, settingID: settingID,
            valueJSON: String(decoding: json, as: UTF8.self),
            version: version.normalized(for: ["setting_id", "value_json", "tombstone"]),
            tombstone: nil
        )
    }

    public static func decode(_ record: CompanionPermittedSettingRecord) -> Self? {
        guard !record.isDeleted,
              record.id == CompanionBrowserSettingCatalog.recordID(for: record.settingID),
              record.settingID.hasPrefix(prefix), record.settingID.hasSuffix(suffix),
              record.settingID.utf8.count == prefix.utf8.count + 32 + suffix.utf8.count,
              let raw = try? JSONSerialization.jsonObject(with: Data(record.valueJSON.utf8)),
              let value = raw as? [String: Any],
              Set(value.keys) == ["source", "installed", "enabled"],
              let sourceValue = value["source"] as? String,
              let source = Source(rawValue: sourceValue),
              let installed = boolean(value["installed"]),
              let enabled = boolean(value["enabled"]) else { return nil }
        let id = String(record.settingID.dropFirst(prefix.count).dropLast(suffix.count))
        return try? Self(extensionID: id, source: source, installed: installed, enabled: enabled)
    }

    private static let prefix = "ahoi.extension."
    private static let suffix = ".desired"
    private static func boolean(_ value: Any?) -> Bool? {
        guard let number = value as? NSNumber,
              CFGetTypeID(number) == CFBooleanGetTypeID() else { return nil }
        return number.boolValue
    }
}
