import Foundation

#if canImport(CloudKit)
import CloudKit

public enum AppleCloudKitRecordCodecError: Error, Equatable {
    case invalidRecordID(String)
    case missingField(String)
    case invalidField(String)
    case corruptEncryptedValue(UUID)
}

/// Maps sync conflict/change metadata to normal queryable CKRecord fields and
/// the opaque transport value to CKRecord.encryptedValues. It performs no
/// cryptographic operation itself.
public struct AppleCloudKitRecordCodec {
    public static let recordType = "AhoiSyncRecord"

    public enum Fields {
        public static let encryptedValue = "encryptedValue"
        public static let entityID = "entityID"
        public static let schemaVersion = "schemaVersion"
        public static let dataClass = "dataClass"
        public static let hlcPhysical = "hlcPhysical"
        public static let hlcLogical = "hlcLogical"
        public static let hlcNodeID = "hlcNodeID"
        public static let originatingDeviceID = "originatingDeviceID"
        public static let orderComponents = "orderComponents"
        public static let orderTieBreaker = "orderTieBreaker"
        public static let orderSortKey = "orderSortKey"
        public static let isTombstone = "isTombstone"
        public static let tombstoneEntityID = "tombstoneEntityID"
        public static let tombstoneDeletedPhysical = "tombstoneDeletedPhysical"
        public static let tombstoneDeletedLogical = "tombstoneDeletedLogical"
        public static let tombstoneDeletedNodeID = "tombstoneDeletedNodeID"
        public static let tombstoneDeletedBy = "tombstoneDeletedBy"
        public static let tombstoneOriginalParentID = "tombstoneOriginalParentID"
        public static let tombstoneOriginalOrderComponents = "tombstoneOriginalOrderComponents"
        public static let tombstoneOriginalOrderTieBreaker = "tombstoneOriginalOrderTieBreaker"
        public static let tombstoneOriginalOrderSortKey = "tombstoneOriginalOrderSortKey"
        public static let tombstonePurgeAfter = "tombstonePurgeAfter"
    }

    public init() {}

    public func encode(
        _ value: SyncRecord,
        zoneID: CKRecordZone.ID
    ) throws -> CKRecord {
        let recordID = CKRecord.ID(
            recordName: value.recordID.uuidString,
            zoneID: zoneID
        )
        let record = CKRecord(recordType: Self.recordType, recordID: recordID)

        record[Fields.entityID] = value.entityID.uuidString as NSString
        record[Fields.schemaVersion] = NSNumber(value: value.schemaVersion)
        record[Fields.dataClass] = value.dataClass.rawValue as NSString
        record[Fields.hlcPhysical] = NSNumber(value: value.modifiedAt.physicalMilliseconds)
        record[Fields.hlcLogical] = NSNumber(value: value.modifiedAt.logicalCounter)
        record[Fields.hlcNodeID] = value.modifiedAt.nodeID.rawValue.uuidString as NSString
        record[Fields.originatingDeviceID] = value.originatingDevice.rawValue.uuidString as NSString
        record[Fields.isTombstone] = NSNumber(value: value.tombstone != nil)
        writeOrderKey(value.orderKey, prefix: .primary, to: record)
        writeTombstone(value.tombstone, to: record)

        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        record.encryptedValues[Fields.encryptedValue] = try encoder.encode(
            value.encryptedValue
        ) as NSData
        return record
    }

    public func decode(_ record: CKRecord) throws -> SyncRecord {
        guard let recordID = UUID(uuidString: record.recordID.recordName) else {
            throw AppleCloudKitRecordCodecError.invalidRecordID(record.recordID.recordName)
        }
        let entityID = try uuid(Fields.entityID, in: record)
        let schemaVersion = try uint32(Fields.schemaVersion, in: record)
        let dataClassValue = try string(Fields.dataClass, in: record)
        guard let dataClass = SyncDataClass(rawValue: dataClassValue) else {
            throw AppleCloudKitRecordCodecError.invalidField(Fields.dataClass)
        }

        let modifiedAt = HybridLogicalClock(
            physicalMilliseconds: try uint64(Fields.hlcPhysical, in: record),
            logicalCounter: try uint32(Fields.hlcLogical, in: record),
            nodeID: DeviceID(rawValue: try uuid(Fields.hlcNodeID, in: record))
        )
        let originatingDevice = DeviceID(
            rawValue: try uuid(Fields.originatingDeviceID, in: record)
        )
        let orderKey = try readOrderKey(prefix: .primary, from: record)
        let tombstone = try readTombstone(from: record)

        guard let encryptedData = record.encryptedValues[Fields.encryptedValue] as? Data else {
            throw AppleCloudKitRecordCodecError.missingField(Fields.encryptedValue)
        }
        guard let encryptedValue = try? JSONDecoder().decode(
            EncryptedValue.self,
            from: encryptedData
        ) else {
            throw AppleCloudKitRecordCodecError.corruptEncryptedValue(recordID)
        }

        return SyncRecord(
            recordID: recordID,
            entityID: entityID,
            schemaVersion: schemaVersion,
            dataClass: dataClass,
            modifiedAt: modifiedAt,
            originatingDevice: originatingDevice,
            orderKey: orderKey,
            encryptedValue: encryptedValue,
            tombstone: tombstone
        )
    }

    private enum OrderPrefix {
        case primary
        case tombstoneOriginal

        var components: String {
            switch self {
            case .primary: Fields.orderComponents
            case .tombstoneOriginal: Fields.tombstoneOriginalOrderComponents
            }
        }

        var tieBreaker: String {
            switch self {
            case .primary: Fields.orderTieBreaker
            case .tombstoneOriginal: Fields.tombstoneOriginalOrderTieBreaker
            }
        }

        var sortKey: String {
            switch self {
            case .primary: Fields.orderSortKey
            case .tombstoneOriginal: Fields.tombstoneOriginalOrderSortKey
            }
        }
    }

    private func writeOrderKey(
        _ orderKey: OrderKey?,
        prefix: OrderPrefix,
        to record: CKRecord
    ) {
        guard let orderKey else {
            record[prefix.components] = nil
            record[prefix.tieBreaker] = nil
            record[prefix.sortKey] = nil
            return
        }
        record[prefix.components] = orderKey.components.map(String.init).joined(separator: ",") as NSString
        record[prefix.tieBreaker] = orderKey.tieBreaker.rawValue.uuidString as NSString
        record[prefix.sortKey] = sortKey(for: orderKey) as NSString
    }

    private func readOrderKey(
        prefix: OrderPrefix,
        from record: CKRecord
    ) throws -> OrderKey? {
        let componentValue = record[prefix.components] as? String
        let tieBreakerValue = record[prefix.tieBreaker] as? String
        let sortKeyValue = record[prefix.sortKey] as? String
        if componentValue == nil, tieBreakerValue == nil, sortKeyValue == nil {
            return nil
        }
        guard let componentValue, let tieBreakerValue, let sortKeyValue,
              let tieBreakerUUID = UUID(uuidString: tieBreakerValue) else {
            throw AppleCloudKitRecordCodecError.invalidField(prefix.components)
        }
        let rawComponents = componentValue.split(separator: ",", omittingEmptySubsequences: false)
        let components = try rawComponents.map { component -> UInt16 in
            guard let value = UInt16(component) else {
                throw AppleCloudKitRecordCodecError.invalidField(prefix.components)
            }
            return value
        }
        do {
            let orderKey = try OrderKey(
                components: components,
                tieBreaker: DeviceID(rawValue: tieBreakerUUID)
            )
            guard sortKeyValue == sortKey(for: orderKey) else {
                throw AppleCloudKitRecordCodecError.invalidField(prefix.sortKey)
            }
            return orderKey
        } catch let error as AppleCloudKitRecordCodecError {
            throw error
        } catch {
            throw AppleCloudKitRecordCodecError.invalidField(prefix.components)
        }
    }

    private func writeTombstone(_ tombstone: Tombstone?, to record: CKRecord) {
        guard let tombstone else {
            return
        }
        record[Fields.tombstoneEntityID] = tombstone.entityID.uuidString as NSString
        record[Fields.tombstoneDeletedPhysical] = NSNumber(
            value: tombstone.deletedAt.physicalMilliseconds
        )
        record[Fields.tombstoneDeletedLogical] = NSNumber(
            value: tombstone.deletedAt.logicalCounter
        )
        record[Fields.tombstoneDeletedNodeID] = tombstone.deletedAt.nodeID.rawValue.uuidString as NSString
        record[Fields.tombstoneDeletedBy] = tombstone.deletedBy.rawValue.uuidString as NSString
        if let originalParentID = tombstone.originalParentID {
            record[Fields.tombstoneOriginalParentID] = originalParentID.uuidString as NSString
        } else {
            record[Fields.tombstoneOriginalParentID] = nil
        }
        writeOrderKey(tombstone.originalOrderKey, prefix: .tombstoneOriginal, to: record)
        record[Fields.tombstonePurgeAfter] = NSNumber(value: tombstone.purgeAfterMilliseconds)
    }

    private func readTombstone(from record: CKRecord) throws -> Tombstone? {
        guard try bool(Fields.isTombstone, in: record) else {
            return nil
        }
        let originalParentID: UUID?
        if let rawParent = record[Fields.tombstoneOriginalParentID] as? String {
            guard let parsed = UUID(uuidString: rawParent) else {
                throw AppleCloudKitRecordCodecError.invalidField(
                    Fields.tombstoneOriginalParentID
                )
            }
            originalParentID = parsed
        } else {
            originalParentID = nil
        }

        return Tombstone(
            entityID: try uuid(Fields.tombstoneEntityID, in: record),
            deletedAt: HybridLogicalClock(
                physicalMilliseconds: try uint64(
                    Fields.tombstoneDeletedPhysical,
                    in: record
                ),
                logicalCounter: try uint32(Fields.tombstoneDeletedLogical, in: record),
                nodeID: DeviceID(
                    rawValue: try uuid(Fields.tombstoneDeletedNodeID, in: record)
                )
            ),
            deletedBy: DeviceID(rawValue: try uuid(Fields.tombstoneDeletedBy, in: record)),
            originalParentID: originalParentID,
            originalOrderKey: try readOrderKey(prefix: .tombstoneOriginal, from: record),
            purgeAfterMilliseconds: try uint64(Fields.tombstonePurgeAfter, in: record)
        )
    }

    private func sortKey(for orderKey: OrderKey) -> String {
        let path = orderKey.components
            .map { String(format: "%04x", $0) }
            .joined(separator: ".")
        return path + "!" + orderKey.tieBreaker.rawValue.uuidString.lowercased()
    }

    private func string(_ field: String, in record: CKRecord) throws -> String {
        guard let value = record[field] as? String else {
            throw AppleCloudKitRecordCodecError.missingField(field)
        }
        return value
    }

    private func uuid(_ field: String, in record: CKRecord) throws -> UUID {
        let value = try string(field, in: record)
        guard let result = UUID(uuidString: value) else {
            throw AppleCloudKitRecordCodecError.invalidField(field)
        }
        return result
    }

    private func number(_ field: String, in record: CKRecord) throws -> NSNumber {
        guard let value = record[field] as? NSNumber else {
            throw AppleCloudKitRecordCodecError.missingField(field)
        }
        return value
    }

    private func uint64(_ field: String, in record: CKRecord) throws -> UInt64 {
        let value = try number(field, in: record)
        guard let result = UInt64(value.stringValue) else {
            throw AppleCloudKitRecordCodecError.invalidField(field)
        }
        return result
    }

    private func uint32(_ field: String, in record: CKRecord) throws -> UInt32 {
        let value = try uint64(field, in: record)
        guard value <= UInt32.max else {
            throw AppleCloudKitRecordCodecError.invalidField(field)
        }
        return UInt32(value)
    }

    private func bool(_ field: String, in record: CKRecord) throws -> Bool {
        try number(field, in: record).boolValue
    }
}
#endif
