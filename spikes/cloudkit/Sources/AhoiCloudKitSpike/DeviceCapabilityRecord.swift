import Foundation

public enum DeviceCapabilityError: Error, Equatable, Sendable {
    case invalidIdentity
    case invalidCapabilities
    case invalidVersion
    case unknownDevice
}

/// A wire-v2 declaration tied to a separately authenticated DeviceRecord.
/// Constructing this value neither enrolls a device nor activates a writer.
public struct DeviceCapabilityRecord: Codable, Hashable, Sendable, Identifiable {
    public static let syncFields: Set<String> = ["device_id", "capabilities", "tombstone"]
    public let id: UUID
    public let deviceID: DeviceID
    public var readableModels: [UInt32]
    public var writableModels: [UInt32]
    public var features: [String]
    public var version: SyncVersion
    public var tombstone: Tombstone?
    public var isDeleted: Bool { tombstone != nil }

    public init(
        id: UUID? = nil, deviceID: DeviceID, readableModels: [UInt32], writableModels: [UInt32],
        features: [String], version: SyncVersion, tombstone: Tombstone? = nil
    ) throws {
        self.id = id ?? SharedTabContract.capabilityID(for: deviceID)
        self.deviceID = deviceID
        self.readableModels = readableModels
        self.writableModels = writableModels
        self.features = features
        self.version = version
        self.tombstone = tombstone
        try validate()
    }

    public func validate() throws {
        let zero = UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
        guard deviceID.rawValue != zero, deviceID != SharedTabContract.systemActor,
              id == SharedTabContract.capabilityID(for: deviceID) else {
            throw DeviceCapabilityError.invalidIdentity
        }
        for models in [readableModels, writableModels] {
            guard !models.isEmpty, models.count <= 32, models == models.sorted(),
                  Set(models).count == models.count, models.allSatisfy({ (1...32).contains($0) }) else {
                throw DeviceCapabilityError.invalidCapabilities
            }
        }
        guard features.count <= 32, features == features.sorted(), Set(features).count == features.count,
              features.allSatisfy({ !$0.isEmpty && $0.utf8.count <= 64 &&
                  $0.utf8.allSatisfy { $0 >= 0x21 && $0 <= 0x7e } }) else {
            throw DeviceCapabilityError.invalidCapabilities
        }
        guard version.schemaVersion == 2, version.modifiedBy == deviceID,
              version.modifiedAt.nodeID == deviceID, Self.validClock(version.modifiedAt),
              Set(version.fieldVersions.keys) == Self.syncFields,
              version.fieldVersions.values.allSatisfy({ $0.nodeID == deviceID && Self.validClock($0) &&
                  $0 <= version.modifiedAt }) else {
            throw DeviceCapabilityError.invalidVersion
        }
        if let tombstone {
            guard tombstone.entityID == id, tombstone.deletedBy == deviceID,
                  tombstone.deletedAt == version.modifiedAt,
                  tombstone.purgeAfterMilliseconds > tombstone.deletedAt.physicalMilliseconds else {
                throw DeviceCapabilityError.invalidVersion
            }
        }
    }

    private static func validClock(_ clock: HybridLogicalClock) -> Bool {
        guard clock.submillisecondMicroseconds <= 999 else { return false }
        let (whole, overflow) = clock.physicalMilliseconds.multipliedReportingOverflow(by: 1_000)
        guard !overflow else { return false }
        let (value, fractionOverflow) = whole.addingReportingOverflow(UInt64(clock.submillisecondMicroseconds))
        return !fractionOverflow && value <= UInt64(Int64.max - 11_644_473_600_000_000)
    }

    private enum CodingKeys: String, CodingKey {
        case id, deviceID, readableModels, writableModels, features, version, tombstone
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        try self.init(
            id: c.decode(UUID.self, forKey: .id), deviceID: c.decode(DeviceID.self, forKey: .deviceID),
            readableModels: c.decode([UInt32].self, forKey: .readableModels),
            writableModels: c.decode([UInt32].self, forKey: .writableModels),
            features: c.decode([String].self, forKey: .features),
            version: c.decode(SyncVersion.self, forKey: .version),
            tombstone: c.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }
}
