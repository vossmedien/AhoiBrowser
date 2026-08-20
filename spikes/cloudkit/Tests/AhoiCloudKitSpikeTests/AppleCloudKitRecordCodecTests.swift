import Foundation
import XCTest
@testable import AhoiCloudKitSpike

#if canImport(CloudKit)
import CloudKit

final class AppleCloudKitRecordCodecTests: XCTestCase {
    private let codec = AppleCloudKitRecordCodec()
    private let zoneID = CKRecordZone.ID(
        zoneName: "AhoiBrowserSyncZoneTests",
        ownerName: CKCurrentUserDefaultName
    )

    func testLiveRecordRoundTripKeepsMetadataQueryableAndValueEncrypted() throws {
        let source = try makeLiveRecord(dataClass: .treeNode)

        let cloudRecord = try codec.encode(source, zoneID: zoneID)

        XCTAssertEqual(
            cloudRecord[AppleCloudKitRecordCodec.Fields.entityID] as? String,
            source.entityID.uuidString
        )
        XCTAssertEqual(
            cloudRecord[AppleCloudKitRecordCodec.Fields.dataClass] as? String,
            SyncDataClass.treeNode.rawValue
        )
        XCTAssertNotNil(cloudRecord[AppleCloudKitRecordCodec.Fields.hlcPhysical])
        XCTAssertNotNil(cloudRecord[AppleCloudKitRecordCodec.Fields.hlcLogical])
        XCTAssertNotNil(cloudRecord[AppleCloudKitRecordCodec.Fields.hlcNodeID])
        XCTAssertNotNil(cloudRecord[AppleCloudKitRecordCodec.Fields.originatingDeviceID])
        XCTAssertNotNil(cloudRecord[AppleCloudKitRecordCodec.Fields.orderSortKey])
        XCTAssertNil(cloudRecord[AppleCloudKitRecordCodec.Fields.encryptedValue])
        XCTAssertNotNil(
            cloudRecord.encryptedValues[
                AppleCloudKitRecordCodec.Fields.encryptedValue
            ] as? Data
        )
        XCTAssertEqual(try codec.decode(cloudRecord), source)
    }

    func testTombstoneRoundTripUsesNormalConflictAndRecoveryMetadata() throws {
        let device = DeviceID(
            rawValue: UUID(uuidString: "40000000-0000-0000-0000-000000000004")!
        )
        let entityID = UUID(uuidString: "50000000-0000-0000-0000-000000000005")!
        let deletedAt = HybridLogicalClock(
            physicalMilliseconds: 5_000,
            logicalCounter: 3,
            nodeID: device
        )
        let source = SyncRecord(
            recordID: UUID(uuidString: "60000000-0000-0000-0000-000000000006")!,
            entityID: entityID,
            dataClass: .tombstone,
            modifiedAt: deletedAt,
            originatingDevice: device,
            encryptedValue: syntheticEncryptedValue,
            tombstone: .init(
                entityID: entityID,
                deletedAt: deletedAt,
                deletedBy: device,
                originalParentID: UUID(
                    uuidString: "70000000-0000-0000-0000-000000000007"
                )!,
                originalOrderKey: try OrderKey(
                    components: [12, 34],
                    tieBreaker: device
                ),
                purgeAfterMilliseconds: 9_000
            )
        )

        let cloudRecord = try codec.encode(source, zoneID: zoneID)
        let decoded = try codec.decode(cloudRecord)

        XCTAssertEqual(
            (cloudRecord[AppleCloudKitRecordCodec.Fields.isTombstone] as? NSNumber)?.boolValue,
            true
        )
        XCTAssertNotNil(
            cloudRecord[AppleCloudKitRecordCodec.Fields.tombstoneDeletedPhysical]
        )
        XCTAssertNotNil(
            cloudRecord[AppleCloudKitRecordCodec.Fields.tombstoneOriginalOrderSortKey]
        )
        XCTAssertEqual(decoded, source)
        XCTAssertNoThrow(try SyncBoundary().authorize(decoded))
    }

    func testDecodedDeniedClassIsStoppedByBoundary() throws {
        let source = try makeLiveRecord(dataClass: .httpAuthSecret)
        let decoded = try codec.decode(try codec.encode(source, zoneID: zoneID))

        XCTAssertThrowsError(try SyncBoundary().authorize(decoded)) { error in
            XCTAssertEqual(
                error as? SyncBoundaryError,
                .dataClassDenied(.httpAuthSecret)
            )
        }
    }

    func testTamperedOrderSortMetadataIsRejected() throws {
        let source = try makeLiveRecord(dataClass: .treeNode)
        let cloudRecord = try codec.encode(source, zoneID: zoneID)
        cloudRecord[AppleCloudKitRecordCodec.Fields.orderSortKey] = "tampered" as NSString

        XCTAssertThrowsError(try codec.decode(cloudRecord)) { error in
            XCTAssertEqual(
                error as? AppleCloudKitRecordCodecError,
                .invalidField(AppleCloudKitRecordCodec.Fields.orderSortKey)
            )
        }
    }

    private var syntheticEncryptedValue: EncryptedValue {
        .init(
            keyVersion: 1,
            nonce: Data(repeating: 8, count: 12),
            ciphertextAndTag: Data(repeating: 9, count: 32)
        )
    }

    private func makeLiveRecord(dataClass: SyncDataClass) throws -> SyncRecord {
        let device = DeviceID(
            rawValue: UUID(uuidString: "10000000-0000-0000-0000-000000000001")!
        )
        return SyncRecord(
            recordID: UUID(uuidString: "20000000-0000-0000-0000-000000000002")!,
            entityID: UUID(uuidString: "30000000-0000-0000-0000-000000000003")!,
            schemaVersion: 1,
            dataClass: dataClass,
            modifiedAt: .init(
                physicalMilliseconds: 1_234,
                logicalCounter: 2,
                nodeID: device
            ),
            originatingDevice: device,
            orderKey: try OrderKey(components: [10, 20], tieBreaker: device),
            encryptedValue: syntheticEncryptedValue
        )
    }
}
#endif
