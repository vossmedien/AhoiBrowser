import CoreFoundation
import Foundation
import AhoiCloudKitSpike

public enum SharedTabWirePreparationError: Error, Equatable, Sendable {
    case writerNotActivated
    case unsupportedVersion
    case invalidFieldMap
}

/// Read-only preparation for the portable shared-tab wire extension. Version 3
/// can be decoded, but every writer remains fixed to the established v1/v2
/// contract until a separate capability gate is implemented.
public enum SharedTabWireReadPolicy {
    public static let maximumReadableVersion: UInt32 = 3
    public static let defaultWriteVersion: UInt32 = 2

    public static let treeNodeBaseFields: Set<String> = [
        "location", "kind", "title", "icon", "accent_argb", "url",
        "created_at", "modified_at", "tombstone",
    ]
    public static let remoteTabBaseFields: Set<String> = [
        "device_id", "session_id", "workspace_id", "url", "title",
        "opened_at", "last_active", "pinned", "is_incognito", "tombstone",
    ]

    public static func treeNodeFields(for version: UInt32) throws -> Set<String> {
        switch version {
        case 1, 2:
            return treeNodeBaseFields
        case 3:
            return treeNodeBaseFields.union(["is_temporary"])
        default:
            throw SharedTabWirePreparationError.unsupportedVersion
        }
    }

    public static func remoteTabFields(for version: UInt32) throws -> Set<String> {
        switch version {
        case 1, 2:
            return remoteTabBaseFields
        case 3:
            return remoteTabBaseFields.union(["tree_node_id"])
        default:
            throw SharedTabWirePreparationError.unsupportedVersion
        }
    }

    static func payloadVersion(_ value: [String: Any]) throws -> UInt32 {
        let model = try strictUInt32(value, key: "model_version")
        let versionModel = try strictUInt32(value, key: "version_model")
        guard model == versionModel, model >= 1, model <= maximumReadableVersion else {
            throw SharedTabWirePreparationError.unsupportedVersion
        }
        if model == 3 {
            _ = try strictUInt32(value, key: "version_logical")
            _ = try strictUUID(value, key: "version_device")
            guard let physical = value["version_physical"] as? String,
                  let parsed = Int64(physical),
                  parsed >= DesktopWirePayloadCodec.windowsToUnixMicroseconds,
                  String(parsed) == physical else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
        }
        return model
    }

    static func requiredTreeNodeFields(
        version: UInt32
    ) throws -> Set<String>? {
        version == 1 ? nil : try treeNodeFields(for: version)
    }

    static func requiredRemoteTabFields(
        version: UInt32
    ) throws -> Set<String>? {
        version == 1 ? nil : try remoteTabFields(for: version)
    }

    static func validateTreeNodeReadShape(
        _ value: [String: Any],
        version: UInt32
    ) throws -> Bool {
        switch version {
        case 1, 2:
            try rejectLegacyField(
                "is_temporary",
                in: value,
                fieldClockName: "is_temporary"
            )
            return false
        case 3:
            try validateVersionThreeFieldClocks(
                value,
                expected: try treeNodeFields(for: version)
            )
            return try strictBoolean(value, key: "is_temporary")
        default:
            throw SharedTabWirePreparationError.unsupportedVersion
        }
    }

    static func validateRemoteTabReadShape(
        _ value: [String: Any],
        version: UInt32
    ) throws -> TreeNodeID? {
        switch version {
        case 1, 2:
            try rejectLegacyField(
                "tree_node_id",
                in: value,
                fieldClockName: "tree_node_id"
            )
            return nil
        case 3:
            try validateVersionThreeFieldClocks(
                value,
                expected: try remoteTabFields(for: version)
            )
            guard value.keys.contains("tree_node_id") else { return nil }
            guard !(value["tree_node_id"] is NSNull) else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            return TreeNodeID(rawValue: try strictUUID(value, key: "tree_node_id"))
        default:
            throw SharedTabWirePreparationError.unsupportedVersion
        }
    }

    static func validateTreeNodeWrite(_ node: TreeNode) throws {
        try validateWriterVersion(node.version.schemaVersion)
        guard !node.isTemporary else {
            throw SharedTabWirePreparationError.writerNotActivated
        }
        try validateLegacyWriteFields(
            node.version,
            allowed: treeNodeBaseFields
        )
    }

    static func validateRemoteTabWrite(_ tab: RemoteTab) throws {
        try validateWriterVersion(tab.version.schemaVersion)
        guard tab.treeNodeID == nil else {
            throw SharedTabWirePreparationError.writerNotActivated
        }
        try validateLegacyWriteFields(
            tab.version,
            allowed: remoteTabBaseFields
        )
    }

    static func validateDecodedVersion(_ version: SyncVersion) throws {
        guard version.fieldVersions.values.allSatisfy({ $0 <= version.modifiedAt }) else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
    }

    static func strictBoolean(_ value: [String: Any], key: String) throws -> Bool {
        guard let number = value[key] as? NSNumber,
              CFGetTypeID(number) == CFBooleanGetTypeID() else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return number.boolValue
    }

    static func strictUUID(_ value: [String: Any], key: String) throws -> UUID {
        guard let raw = value[key] as? String,
              raw == raw.lowercased(),
              let result = UUID(uuidString: raw),
              result.uuidString.lowercased() == raw,
              result != zeroUUID else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    private static func validateWriterVersion(_ version: UInt32) throws {
        if version > defaultWriteVersion && version <= maximumReadableVersion {
            throw SharedTabWirePreparationError.writerNotActivated
        }
        guard version == 1 || version == defaultWriteVersion else {
            throw SharedTabWirePreparationError.unsupportedVersion
        }
    }

    private static func validateLegacyWriteFields(
        _ version: SyncVersion,
        allowed: Set<String>
    ) throws {
        let fields = Set(version.fieldVersions.keys)
        if version.schemaVersion == 1 {
            guard fields.isEmpty else {
                throw SharedTabWirePreparationError.invalidFieldMap
            }
        } else {
            guard fields.isSubset(of: allowed) else {
                throw SharedTabWirePreparationError.invalidFieldMap
            }
        }
    }

    private static func rejectLegacyField(
        _ payloadName: String,
        in value: [String: Any],
        fieldClockName: String
    ) throws {
        guard !value.keys.contains(payloadName) else {
            throw SharedTabWirePreparationError.writerNotActivated
        }
        if let fields = value["field_versions"] as? [String: Any],
           fields.keys.contains(fieldClockName) {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
    }

    private static func validateVersionThreeFieldClocks(
        _ value: [String: Any],
        expected: Set<String>
    ) throws {
        guard let fields = value["field_versions"] as? [String: Any],
              Set(fields.keys) == expected else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
        for field in expected {
            guard let stamp = fields[field] as? [String: Any] else {
                throw SharedTabWirePreparationError.invalidFieldMap
            }
            _ = try strictUInt32(stamp, key: "logical")
            _ = try strictUUID(stamp, key: "device")
            guard let physical = stamp["physical"] as? String,
                  let parsed = Int64(physical),
                  parsed >= DesktopWirePayloadCodec.windowsToUnixMicroseconds,
                  String(parsed) == physical else {
                throw SharedTabWirePreparationError.invalidFieldMap
            }
        }
    }

    private static func strictUInt32(
        _ value: [String: Any],
        key: String
    ) throws -> UInt32 {
        guard let number = value[key] as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID() else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let encoding = String(cString: number.objCType)
        guard encoding != "f", encoding != "d", encoding != "D",
              let result = UInt32(exactly: number.int64Value) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    private static let zeroUUID = UUID(
        uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    )
}
