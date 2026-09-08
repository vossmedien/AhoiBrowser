import Foundation
import AhoiCloudKitSpike

extension DesktopWirePayloadCodec {
    static let appearanceFields: Set<String> = [
        "color_mode", "accent_argb", "use_system_accent", "tombstone",
    ]
    static let permittedSettingFields: Set<String> = [
        "setting_id", "value_json", "tombstone",
    ]
    static let extensionInventoryFields: Set<String> = [
        "device_id", "extension_id", "name", "extension_version", "enabled",
        "tombstone",
    ]
    static let developerAssetFields: Set<String> = [
        "kind", "name", "scope", "source", "enabled", "opted_in", "tombstone",
    ]

    func optionalARGB(_ value: [String: Any]) throws -> UInt32? {
        guard value.keys.contains("accent_argb") else { return nil }
        guard let signed = Int32(exactly: try integer(value, "accent_argb")) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return UInt32(bitPattern: signed)
    }

    public func encode(_ record: CompanionAppearanceRecord) throws -> Data {
        var value = try common(
            id: record.id,
            tombstone: record.isDeleted,
            version: record.version,
            fields: Self.appearanceFields
        )
        value["color_mode"] = record.colorMode.rawValue
        value["accent_argb"] = record.accentARGB.map {
            Int(Int32(bitPattern: $0))
        }
        value["use_system_accent"] = record.useSystemAccent
        return try serialize(value)
    }

    public func encode(_ record: CompanionPermittedSettingRecord) throws -> Data {
        var value = try common(
            id: record.id,
            tombstone: record.isDeleted,
            version: record.version,
            fields: Self.permittedSettingFields
        )
        value["setting_id"] = record.settingID
        value["value_json"] = record.valueJSON
        return try serialize(value)
    }

    public func encode(_ record: CompanionExtensionInventoryRecord) throws -> Data {
        var value = try common(
            id: record.id,
            tombstone: record.isDeleted,
            version: record.version,
            fields: Self.extensionInventoryFields
        )
        value["device_id"] = uuid(record.deviceID.rawValue)
        value["extension_id"] = record.extensionID
        value["name"] = record.name
        value["extension_version"] = record.extensionVersion
        value["enabled"] = record.enabled
        return try serialize(value)
    }

    public func encode(_ record: CompanionDeveloperAssetRecord) throws -> Data {
        var value = try common(
            id: record.id,
            tombstone: record.isDeleted,
            version: record.version,
            fields: Self.developerAssetFields
        )
        value["asset_kind"] = record.kind.rawValue
        value["name"] = record.name
        value["scope"] = record.scope
        value["source"] = record.source
        value["enabled"] = record.enabled
        value["opted_in"] = record.optedIn
        return try serialize(value)
    }

    public func decodeAppearance(
        _ envelope: SyncRecord,
        plaintext: Data
    ) throws -> CompanionAppearanceRecord {
        let value = try object(from: plaintext)
        guard let colorMode = CompanionColorMode(rawValue: try string(value, "color_mode")) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let result = try CompanionAppearanceRecord(
            id: try id(value),
            colorMode: colorMode,
            accentARGB: try optionalARGB(value),
            useSystemAccent: try SharedTabWireReadPolicy.strictBoolean(value, key: "use_system_accent"),
            version: try version(value, requiredFields: Self.appearanceFields),
            tombstone: try tombstone(envelope, value: value)
        )
        try validatePortableEnvelope(envelope, dataClass: .appearance, identity: result.id,
                                     version: result.version, tombstone: result.tombstone)
        return result
    }

    public func decodePermittedSetting(
        _ envelope: SyncRecord,
        plaintext: Data
    ) throws -> CompanionPermittedSettingRecord {
        let value = try object(from: plaintext)
        let result = try CompanionPermittedSettingRecord(
            id: try id(value),
            settingID: try string(value, "setting_id"),
            valueJSON: try string(value, "value_json"),
            version: try version(value, requiredFields: Self.permittedSettingFields),
            tombstone: try tombstone(envelope, value: value)
        )
        try validatePortableEnvelope(envelope, dataClass: .permittedSetting, identity: result.id,
                                     version: result.version, tombstone: result.tombstone)
        return result
    }

    public func decodeExtensionInventory(
        _ envelope: SyncRecord,
        plaintext: Data
    ) throws -> CompanionExtensionInventoryRecord {
        let value = try object(from: plaintext)
        let result = try CompanionExtensionInventoryRecord(
            id: try id(value),
            deviceID: DeviceID(rawValue: try uuid(value, "device_id")),
            extensionID: try string(value, "extension_id"),
            name: try string(value, "name"),
            extensionVersion: try string(value, "extension_version"),
            enabled: try SharedTabWireReadPolicy.strictBoolean(value, key: "enabled"),
            version: try version(value, requiredFields: Self.extensionInventoryFields),
            tombstone: try tombstone(envelope, value: value)
        )
        try validatePortableEnvelope(envelope, dataClass: .extensionInventory, identity: result.id,
                                     version: result.version, tombstone: result.tombstone)
        return result
    }

    public func decodeDeveloperAsset(
        _ envelope: SyncRecord,
        plaintext: Data
    ) throws -> CompanionDeveloperAssetRecord {
        let value = try object(from: plaintext)
        guard let kind = CompanionDeveloperAssetKind(rawValue: try integer(value, "asset_kind")) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let result = try CompanionDeveloperAssetRecord(
            id: try id(value),
            kind: kind,
            name: try string(value, "name"),
            scope: try string(value, "scope"),
            source: try string(value, "source"),
            enabled: try SharedTabWireReadPolicy.strictBoolean(value, key: "enabled"),
            optedIn: try SharedTabWireReadPolicy.strictBoolean(value, key: "opted_in"),
            version: try version(value, requiredFields: Self.developerAssetFields),
            tombstone: try tombstone(envelope, value: value)
        )
        try validatePortableEnvelope(envelope, dataClass: .developerAsset, identity: result.id,
                                     version: result.version, tombstone: result.tombstone)
        return result
    }
}
