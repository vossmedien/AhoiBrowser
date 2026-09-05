import Foundation
import AhoiCloudKitSpike

extension DesktopWirePayloadCodec {
    public func encode(_ capability: DeviceCapabilityRecord) throws -> Data {
        try capability.validate()
        var value = try common(id: capability.id, tombstone: capability.isDeleted,
                               version: capability.version, fields: DeviceCapabilityRecord.syncFields)
        value["device_id"] = uuid(capability.deviceID.rawValue)
        value["readable_models"] = capability.readableModels.map { Int($0) }
        value["writable_models"] = capability.writableModels.map { Int($0) }
        value["features"] = capability.features
        return try serialize(value)
    }

    public func decodeCapability(
        _ record: SyncRecord, plaintext: Data, knownDevices: [DeviceID: Device]
    ) throws -> DeviceCapabilityRecord {
        let value = try object(from: plaintext)
        guard try SharedTabWireReadPolicy.payloadVersion(value) == 2 else {
            throw DeviceCapabilityError.invalidVersion
        }
        let deviceID = DeviceID(rawValue: try SharedTabWireReadPolicy.strictUUID(value, key: "device_id"))
        guard let device = knownDevices[deviceID], !device.isDeleted, !device.isRevoked else {
            throw DeviceCapabilityError.unknownDevice
        }
        let parsedVersion = try version(value, requiredFields: DeviceCapabilityRecord.syncFields)
        try validateCapabilityClockShape(value, device: deviceID)
        guard let features = value["features"] as? [String] else { throw DeviceCapabilityError.invalidCapabilities }
        let result = try DeviceCapabilityRecord(
            id: SharedTabWireReadPolicy.strictUUID(value, key: "id"), deviceID: deviceID,
            readableModels: capabilityModels(value, "readable_models"),
            writableModels: capabilityModels(value, "writable_models"), features: features,
            version: parsedVersion, tombstone: tombstone(record, value: value)
        )
        guard record.dataClass == .deviceCapability, record.schemaVersion == 2,
              record.recordID == result.id, record.entityID == result.id,
              record.orderKey == nil, record.modifiedAt == parsedVersion.modifiedAt,
              record.originatingDevice == deviceID,
              try SharedTabWireReadPolicy.strictBoolean(value, key: "tombstone") == result.isDeleted else {
            throw CompanionSyncBridgeError.envelopeMismatch
        }
        return result
    }

    private func capabilityModels(_ value: [String: Any], _ key: String) throws -> [UInt32] {
        guard let values = value[key] as? [Any], (1...32).contains(values.count) else {
            throw DeviceCapabilityError.invalidCapabilities
        }
        return try values.map { try SharedTabWireReadPolicy.strictUInt32(["value": $0], key: "value") }
    }

    private func validateCapabilityClockShape(_ value: [String: Any], device: DeviceID) throws {
        guard try SharedTabWireReadPolicy.strictUUID(value, key: "version_device") == device.rawValue,
              let fields = value["field_versions"] as? [String: Any],
              Set(fields.keys) == DeviceCapabilityRecord.syncFields else { throw DeviceCapabilityError.invalidVersion }
        _ = try SharedTabWireReadPolicy.strictUInt32(value, key: "version_logical")
        for raw in [value["version_physical"]] + fields.values.map({ ($0 as? [String: Any])?["physical"] }) {
            guard let text = raw as? String, let time = Int64(text),
                  time >= Self.windowsToUnixMicroseconds, String(time) == text else {
                throw DeviceCapabilityError.invalidVersion
            }
        }
        for raw in fields.values {
            guard let field = raw as? [String: Any],
                  try SharedTabWireReadPolicy.strictUUID(field, key: "device") == device.rawValue else {
                throw DeviceCapabilityError.invalidVersion
            }
            _ = try SharedTabWireReadPolicy.strictUInt32(field, key: "logical")
        }
    }
}
