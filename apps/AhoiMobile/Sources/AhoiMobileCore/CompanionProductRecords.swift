import Foundation
import AhoiCloudKitSpike

public enum CompanionProductRecordError: Error, Equatable, Sendable {
    case invalidAppearance
    case invalidPermittedSetting
    case invalidExtensionInventory
    case developerAssetNotOptedIn
    case developerAssetContainsSecretMaterial
    case equalVersionConflict
}

public struct CompanionProductSnapshot: Codable, Equatable, Sendable {
    public var appearance: [CompanionAppearanceRecord]
    public var permittedSettings: [CompanionPermittedSettingRecord]
    public var extensionInventory: [CompanionExtensionInventoryRecord]
    public var developerAssets: [CompanionDeveloperAssetRecord]

    public init(
        appearance: [CompanionAppearanceRecord] = [],
        permittedSettings: [CompanionPermittedSettingRecord] = [],
        extensionInventory: [CompanionExtensionInventoryRecord] = [],
        developerAssets: [CompanionDeveloperAssetRecord] = []
    ) {
        self.appearance = appearance
        self.permittedSettings = permittedSettings
        self.extensionInventory = extensionInventory
        self.developerAssets = developerAssets
    }

    public static let empty = Self()
}

public enum CompanionColorMode: String, Codable, CaseIterable, Sendable {
    case system
    case light
    case dark
}

public struct CompanionAppearanceRecord: Codable, Equatable, Sendable, Identifiable {
    public let id: UUID
    public var colorMode: CompanionColorMode
    public var accentARGB: UInt32?
    public var useSystemAccent: Bool
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        id: UUID,
        colorMode: CompanionColorMode,
        accentARGB: UInt32?,
        useSystemAccent: Bool,
        version: SyncVersion,
        tombstone: Tombstone?
    ) throws {
        guard !useSystemAccent || accentARGB == nil else {
            throw CompanionProductRecordError.invalidAppearance
        }
        self.id = id
        self.colorMode = colorMode
        self.accentARGB = accentARGB
        self.useSystemAccent = useSystemAccent
        self.version = version
        self.tombstone = tombstone
    }

    public var isDeleted: Bool { tombstone != nil }
}

public struct CompanionPermittedSettingRecord: Codable, Equatable, Sendable, Identifiable {
    public let id: UUID
    public var settingID: String
    public var valueJSON: String
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        id: UUID,
        settingID: String,
        valueJSON: String,
        version: SyncVersion,
        tombstone: Tombstone?
    ) throws {
        guard !settingID.isEmpty, settingID.utf8.count <= 128,
              !valueJSON.isEmpty, valueJSON.utf8.count <= 8_192,
              let data = valueJSON.data(using: .utf8),
              (try? JSONSerialization.jsonObject(
                  with: data,
                  options: [.fragmentsAllowed]
              )) != nil else {
            throw CompanionProductRecordError.invalidPermittedSetting
        }
        self.id = id
        self.settingID = settingID
        self.valueJSON = valueJSON
        self.version = version
        self.tombstone = tombstone
    }

    public var isDeleted: Bool { tombstone != nil }
}

public struct CompanionExtensionInventoryRecord: Codable, Equatable, Sendable, Identifiable {
    public let id: UUID
    public let deviceID: DeviceID
    public var extensionID: String
    public var name: String
    public var extensionVersion: String
    public var enabled: Bool
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        id: UUID,
        deviceID: DeviceID,
        extensionID: String,
        name: String,
        extensionVersion: String,
        enabled: Bool,
        version: SyncVersion,
        tombstone: Tombstone?
    ) throws {
        guard extensionID.utf8.count == 32,
              extensionID.utf8.allSatisfy({ $0 >= 97 && $0 <= 112 }),
              name.utf8.count <= 256,
              extensionVersion.utf8.count <= 64 else {
            throw CompanionProductRecordError.invalidExtensionInventory
        }
        self.id = id
        self.deviceID = deviceID
        self.extensionID = extensionID
        self.name = name
        self.extensionVersion = extensionVersion
        self.enabled = enabled
        self.version = version
        self.tombstone = tombstone
    }

    public var isDeleted: Bool { tombstone != nil }
}

public enum CompanionDeveloperAssetKind: Int, Codable, CaseIterable, Sendable {
    case css
    case less
    case sass
    case javaScript
    case headerProfile
}

public struct CompanionDeveloperAssetRecord: Codable, Equatable, Sendable, Identifiable {
    public let id: UUID
    public var kind: CompanionDeveloperAssetKind
    public var name: String
    public var scope: String
    public var source: String
    public var enabled: Bool
    public var optedIn: Bool
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        id: UUID,
        kind: CompanionDeveloperAssetKind,
        name: String,
        scope: String,
        source: String,
        enabled: Bool,
        optedIn: Bool,
        version: SyncVersion,
        tombstone: Tombstone?
    ) throws {
        guard tombstone != nil || optedIn else {
            throw CompanionProductRecordError.developerAssetNotOptedIn
        }
        guard name.utf8.count <= 256, scope.utf8.count <= 2_048,
              source.utf8.count <= 512 * 1_024,
              tombstone != nil || (!name.isEmpty && !scope.isEmpty && !source.isEmpty) else {
            throw CompanionProductRecordError.developerAssetContainsSecretMaterial
        }
        if kind == .headerProfile, tombstone == nil {
            try Self.validateMetadataOnlyHeaderProfile(source)
        }
        self.id = id
        self.kind = kind
        self.name = name
        self.scope = scope
        self.source = source
        self.enabled = enabled
        self.optedIn = optedIn
        self.version = version
        self.tombstone = tombstone
    }

    public var isDeleted: Bool { tombstone != nil }

    private static func validateMetadataOnlyHeaderProfile(_ source: String) throws {
        guard let data = source.data(using: .utf8),
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              root.count == 2, (root["version"] as? NSNumber)?.intValue == 1,
              let rules = root["rules"] as? [[String: Any]], rules.count <= 100,
              rules.allSatisfy({ rule in
                  guard rule.count == 2, rule["action"] as? String == "remove",
                        let name = rule["name"] as? String,
                        !name.isEmpty, name.utf8.count <= 128 else { return false }
                  return name.utf8.allSatisfy {
                      ($0 >= 48 && $0 <= 57) || ($0 >= 65 && $0 <= 90) ||
                          ($0 >= 97 && $0 <= 122) || $0 == 45
                  }
              }) else {
            throw CompanionProductRecordError.developerAssetContainsSecretMaterial
        }
    }
}
