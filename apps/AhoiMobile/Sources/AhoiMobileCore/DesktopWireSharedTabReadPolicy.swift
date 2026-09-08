import CoreFoundation
import Foundation
import AhoiCloudKitSpike

public enum SharedTabWirePreparationError: Error, Equatable, Sendable {
    case writerNotActivated
    case unsupportedVersion
    case invalidFieldMap
}

/// The one current codec format. These helpers validate values; they do not
/// grant native projection/capture support, consent or transport authority.
public enum SharedTabWireReadPolicy {
    public static let maximumReadableVersion = SharedSyncFormat.currentVersion
    public static let defaultWriteVersion = SharedSyncFormat.currentVersion

    public static let treeNodeBaseFields: Set<String> = [
        "location", "kind", "title", "icon", "accent_argb", "url",
        "created_at", "modified_at", "is_temporary", "tombstone",
    ]
    public static let remoteTabBaseFields: Set<String> = [
        "device_id", "session_id", "workspace_id", "url", "title",
        "opened_at", "last_active", "pinned", "is_incognito", "tree_node_id", "tombstone",
    ]

    public static func treeNodeFields(for version: UInt32) throws -> Set<String> {
        try validateWriterVersion(version)
        return treeNodeBaseFields
    }

    public static func remoteTabFields(for version: UInt32) throws -> Set<String> {
        try validateWriterVersion(version)
        return remoteTabBaseFields
    }

    static func payloadVersion(_ value: [String: Any]) throws -> UInt32 {
        let model = try strictUInt32(value, key: "model_version")
        guard model == SharedSyncFormat.currentVersion,
              try strictUInt32(value, key: "version_model") == model else {
            throw SharedTabWirePreparationError.unsupportedVersion
        }
        _ = try parseClock(value, physicalKey: "version_physical",
                           logicalKey: "version_logical", deviceKey: "version_device")
        return model
    }

    static func requiredTreeNodeFields(version: UInt32) throws -> Set<String>? {
        try treeNodeFields(for: version)
    }

    static func requiredRemoteTabFields(version: UInt32) throws -> Set<String>? {
        try remoteTabFields(for: version)
    }

    static func validateTreeNodeReadShape(
        _ value: [String: Any], version: UInt32
    ) throws -> Bool {
        _ = try fieldClocks(value, expected: treeNodeFields(for: version))
        return try strictBoolean(value, key: "is_temporary")
    }

    static func validateRemoteTabReadShape(
        _ value: [String: Any], version: UInt32
    ) throws -> TreeNodeID? {
        _ = try fieldClocks(value, expected: remoteTabFields(for: version))
        let pageID = try strictUUID(value, key: "tree_node_id")
        guard pageID != (try strictUUID(value, key: "id")) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return TreeNodeID(rawValue: pageID)
    }

    static func validateTreeNodeWrite(_ node: TreeNode) throws {
        try validateWriterVersion(node.version.schemaVersion)
        try validateWriteFields(node.version, allowed: treeNodeBaseFields)
        if node.kind == .folder {
            guard !node.isTemporary, node.url == nil, node.targetKind == nil,
                  node.localScheme == nil else { throw SharedTabTargetError.invalidTarget }
            return
        }
        guard let kind = node.targetKind else { throw SharedTabTargetError.invalidTarget }
        try SharedTabTarget(kind: kind, url: node.url ?? "", localScheme: node.localScheme)
            .validatePage(isTemporary: node.isTemporary)
    }

    static func validateRemoteTabWrite(_ tab: RemoteTab) throws {
        guard tab.context == .normal else { throw CompanionModelError.incognitoNotSyncable }
        try validateWriterVersion(tab.version.schemaVersion)
        try validateWriteFields(tab.version, allowed: remoteTabBaseFields)
        guard let pageID = tab.treeNodeID, pageID.rawValue != tab.id.rawValue,
              pageID.rawValue != zeroUUID else { throw SharedTabTargetError.missingPageLink }
        guard let kind = tab.targetKind else { throw SharedTabTargetError.invalidTarget }
        try SharedTabTarget(kind: kind, url: tab.url, localScheme: tab.localScheme)
            .validatePresence(treeNodeID: pageID)
    }

    static func validateDecodedVersion(_ version: SyncVersion) throws {
        guard !version.fieldVersions.isEmpty else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
        try SharedSyncFormat.validate(version, fields: Set(version.fieldVersions.keys))
    }

    static func strictBoolean(_ value: [String: Any], key: String) throws -> Bool {
        guard let number = value[key] as? NSNumber,
              CFGetTypeID(number) == CFBooleanGetTypeID() else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return number.boolValue
    }

    static func strictUUID(_ value: [String: Any], key: String) throws -> UUID {
        guard let raw = value[key] as? String, let result = UUID(uuidString: raw),
              result.uuidString.lowercased() == raw, result != zeroUUID else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    static func validateWriterVersion(_ version: UInt32) throws {
        guard version == SharedSyncFormat.currentVersion else {
            throw SharedTabWirePreparationError.unsupportedVersion
        }
    }

    private static func validateWriteFields(_ version: SyncVersion, allowed: Set<String>) throws {
        try SharedSyncFormat.validate(version, fields: allowed, allowLocalEmpty: true)
    }

    static func fieldClocks(
        _ value: [String: Any], expected: Set<String>
    ) throws -> [String: HybridLogicalClock] {
        guard let fields = value["field_versions"] as? [String: Any],
              Set(fields.keys) == expected else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
        var result: [String: HybridLogicalClock] = [:]
        for (name, raw) in fields {
            guard let stamp = raw as? [String: Any],
                  Set(stamp.keys) == ["physical", "logical", "device"] else {
                throw SharedTabWirePreparationError.invalidFieldMap
            }
            result[name] = try parseClock(stamp, physicalKey: "physical",
                                           logicalKey: "logical", deviceKey: "device")
        }
        return result
    }

    static func parseClock(
        _ value: [String: Any], physicalKey: String, logicalKey: String, deviceKey: String
    ) throws -> HybridLogicalClock {
        let physical = try canonicalWindowsMicroseconds(value, key: physicalKey)
        let micros = UInt64(physical - DesktopWirePayloadCodec.windowsToUnixMicroseconds)
        return HybridLogicalClock(
            physicalMilliseconds: micros / 1_000,
            submillisecondMicroseconds: UInt16(micros % 1_000),
            logicalCounter: try strictUInt32(value, key: logicalKey),
            nodeID: DeviceID(rawValue: try strictUUID(value, key: deviceKey))
        )
    }

    static func canonicalWindowsMicroseconds(_ value: [String: Any], key: String) throws -> Int64 {
        guard let text = value[key] as? String, let time = Int64(text),
              time >= DesktopWirePayloadCodec.windowsToUnixMicroseconds,
              String(time) == text else { throw DesktopWirePayloadCodecError.malformedPayload }
        return time
    }

    static func validateClock(_ clock: HybridLogicalClock) throws {
        guard SharedSyncFormat.isValidClock(clock) else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
    }

    static func strictUInt32(_ value: [String: Any], key: String) throws -> UInt32 {
        guard let number = value[key] as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID(),
              let result = UInt32(exactly: number.doubleValue) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    static func strictInteger(_ value: [String: Any], key: String) throws -> Int {
        guard let number = value[key] as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID(),
              let result = Int(exactly: number.doubleValue) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    private static let zeroUUID = UUID(
        uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    )
}
