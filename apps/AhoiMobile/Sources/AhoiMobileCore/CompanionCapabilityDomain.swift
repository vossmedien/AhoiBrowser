import Foundation
import AhoiCloudKitSpike

enum CompanionCapabilityDomain {
    static func merge(
        _ old: DeviceCapabilityRecord, _ new: DeviceCapabilityRecord
    ) throws -> DeviceCapabilityRecord {
        try old.validate()
        try new.validate()
        guard old.id == new.id, old.deviceID == new.deviceID else {
            throw CompanionFieldMergeError.identityMismatch
        }
        var result = old
        if try CompanionFieldMerge.incomingWins("capabilities", CapabilityValues(old), CapabilityValues(new),
                                                old.version, new.version) {
            result.readableModels = new.readableModels
            result.writableModels = new.writableModels
            result.features = new.features
        }
        if try CompanionFieldMerge.incomingWins("tombstone", old.isDeleted, new.isDeleted, old.version, new.version) {
            result.tombstone = new.tombstone
        }
        var version = CompanionFieldMerge.mergedVersion(old.version, new.version,
                                                        fields: DeviceCapabilityRecord.syncFields)
        if !same(result, version, old), !same(result, version, new) {
            version = try CompanionFieldMerge.dominatingMergeVersion(version)
        }
        result.version = version
        if let tombstone = result.tombstone {
            let (retention, overflow) = version.modifiedAt.physicalMilliseconds.addingReportingOverflow(2_592_000_000)
            result.tombstone = Tombstone(entityID: result.id, deletedAt: version.modifiedAt,
                                         deletedBy: result.deviceID, originalParentID: nil, originalOrderKey: nil,
                                         purgeAfterMilliseconds: max(tombstone.purgeAfterMilliseconds,
                                                                     overflow ? UInt64.max : retention))
        }
        try result.validate()
        return result
    }

    private static func same(_ lhs: DeviceCapabilityRecord, _ version: SyncVersion,
                             _ rhs: DeviceCapabilityRecord) -> Bool {
        CapabilityValues(lhs) == CapabilityValues(rhs) && lhs.isDeleted == rhs.isDeleted &&
            version.fieldVersions == rhs.version.fieldVersions
    }

    private struct CapabilityValues: Equatable {
        let readers: [UInt32]
        let writers: [UInt32]
        let features: [String]
        init(_ record: DeviceCapabilityRecord) {
            readers = record.readableModels
            writers = record.writableModels
            features = record.features
        }
    }
}

extension CompanionSyncBridge {
    // No automatic publication is installed here: the local native/domain
    // implementation cannot advertise feature readiness until its gates exist.
    func makeCapabilityRecord(_ value: DeviceCapabilityRecord) throws -> SyncRecord {
        try codec.makeRecord(recordID: value.id, entityID: value.id, dataClass: .deviceCapability,
                             version: value.version, plaintext: wireCodec.encode(value), tombstone: value.tombstone)
    }
}
