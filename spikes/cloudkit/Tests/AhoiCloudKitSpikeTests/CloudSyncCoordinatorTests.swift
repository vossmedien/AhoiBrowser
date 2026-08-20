import Foundation
import XCTest
@testable import AhoiCloudKitSpike

final class CloudSyncCoordinatorTests: XCTestCase {
    func testCoordinatorStopsDeniedRecordBeforeTransport() async throws {
        let transport = RecordingTransport()
        let coordinator = CloudSyncCoordinator(transport: transport)
        let denied = makeRecord(dataClass: .httpAuthSecret)

        do {
            try await coordinator.upload([denied])
            XCTFail("Expected boundary rejection")
        } catch let error as SyncBoundaryError {
            XCTAssertEqual(error, .dataClassDenied(.httpAuthSecret))
        }
        let deniedSavedCount = await transport.savedCount
        XCTAssertEqual(deniedSavedCount, 0)
    }

    func testCoordinatorUploadsEncryptedAllowedRecord() async throws {
        let transport = RecordingTransport()
        let coordinator = CloudSyncCoordinator(transport: transport)
        let allowed = makeRecord(dataClass: .workspace)

        try await coordinator.prepare()
        try await coordinator.upload([allowed])

        let isPrepared = await transport.prepared
        let allowedSavedCount = await transport.savedCount
        XCTAssertTrue(isPrepared)
        XCTAssertEqual(allowedSavedCount, 1)
    }

    func testCoordinatorRejectsDeniedInboundRecord() async throws {
        let denied = makeRecord(dataClass: .httpAuthSecret)
        let transport = RecordingTransport(seedRecords: [denied])
        let coordinator = CloudSyncCoordinator(transport: transport)

        do {
            _ = try await coordinator.fetch(recordIDs: [denied.recordID])
            XCTFail("Expected inbound boundary rejection")
        } catch let error as SyncBoundaryError {
            XCTAssertEqual(error, .dataClassDenied(.httpAuthSecret))
        }
    }

    func testUserDeletionIsPersistedAsValidatedTombstoneWrite() async throws {
        let transport = RecordingTransport()
        let coordinator = CloudSyncCoordinator(transport: transport)
        let device = DeviceID()
        let entityID = UUID()
        let deletedAt = HybridLogicalClock(
            physicalMilliseconds: 20,
            nodeID: device
        )
        let record = SyncRecord(
            entityID: entityID,
            dataClass: .tombstone,
            modifiedAt: deletedAt,
            originatingDevice: device,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 5, count: 12),
                ciphertextAndTag: Data(repeating: 6, count: 16)
            ),
            tombstone: .init(
                entityID: entityID,
                deletedAt: deletedAt,
                deletedBy: device,
                originalParentID: UUID(),
                originalOrderKey: try OrderKey(
                    components: [10],
                    tieBreaker: device
                ),
                purgeAfterMilliseconds: 21
            )
        )

        try await coordinator.writeTombstone(record)

        let saved = await transport.allSaved
        XCTAssertEqual(saved, [record])
    }

    func testNonTombstoneCannotEnterDeletionAPI() async {
        let transport = RecordingTransport()
        let coordinator = CloudSyncCoordinator(transport: transport)
        let live = makeRecord(dataClass: .treeNode)

        do {
            try await coordinator.writeTombstone(live)
            XCTFail("Expected tombstone-only rejection")
        } catch let error as SyncBoundaryError {
            XCTAssertEqual(error, .invalidTombstone)
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }

    private func makeRecord(dataClass: SyncDataClass) -> SyncRecord {
        let device = DeviceID()
        return .init(
            entityID: UUID(),
            dataClass: dataClass,
            modifiedAt: .init(physicalMilliseconds: 10, nodeID: device),
            originatingDevice: device,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 3, count: 12),
                ciphertextAndTag: Data(repeating: 4, count: 32)
            )
        )
    }
}

private actor RecordingTransport: CloudRecordTransport {
    private(set) var prepared = false
    private(set) var saved: [SyncRecord]

    init(seedRecords: [SyncRecord] = []) {
        self.saved = seedRecords
    }

    var savedCount: Int { saved.count }
    var allSaved: [SyncRecord] { saved }

    func ensureCustomZone() async throws {
        prepared = true
    }

    func save(_ records: [SyncRecord]) async throws {
        saved.append(contentsOf: records)
    }

    func fetch(recordIDs: [UUID]) async throws -> [SyncRecord] {
        saved.filter { recordIDs.contains($0.recordID) }
    }

}
