import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class BookmarkTransportAuthorizationTests: XCTestCase {
    func testDefaultAndRevokedConsentBlockBookmarkIncludingDeletionButNotWorkspace() throws {
        let gate = BookmarkTransportAuthorization()
        XCTAssertThrowsError(try gate.authorize(record(.bookmark))) { error in
            XCTAssertEqual(error as? BookmarkTransportAuthorizationError, .categoryNotApproved)
        }
        XCTAssertNoThrow(try gate.authorize(record(.workspace)))
        XCTAssertTrue(gate.setApproved(true))
        XCTAssertFalse(gate.setApproved(true))
        XCTAssertNoThrow(try gate.authorize(record(.bookmark)))
        XCTAssertTrue(gate.setApproved(false))
        XCTAssertThrowsError(try gate.authorize(record(.bookmark, deleted: true)))
        XCTAssertNoThrow(try gate.authorize(record(.workspace)))
    }

    func testBookmarkApprovalNeverEnablesVersionThreeUpload() {
        let gate = BookmarkTransportAuthorization()
        gate.setApproved(true)
        for dataClass: SyncDataClass in [.bookmark, .treeNode, .deviceTab] {
            XCTAssertThrowsError(try gate.authorize(record(dataClass, schema: 3))) { error in
                XCTAssertEqual(error as? SharedTabWirePreparationError, .writerNotActivated)
            }
        }
    }

    func testTransportMutationEntrypointsRejectUnapprovedBookmarkBeforePersisting() async throws {
#if DEBUG
        let store = InMemorySyncRecordStore()
        let transport = CompanionSyncVisibleTestTransport(recordStore: store)
        let bookmark = record(.bookmark)
        for operation in 0..<3 {
            do {
                switch operation {
                case 0: try await transport.enqueue(bookmark)
                case 1:
                    try await transport.enqueueLocalSnapshot(
                        [bookmark], authorizedDeveloperAssetIDs: [], scanStartedAtMutationEpoch: 0
                    )
                default:
                    try await transport.commitImportedDomainResults(
                        records: [bookmark], authorizedDeveloperAssetIDs: [], revokedDeveloperAssetIDs: []
                    )
                }
                XCTFail("No outbound entry point may bypass the category approval.")
            } catch {
                XCTAssertEqual(error as? BookmarkTransportAuthorizationError, .categoryNotApproved)
            }
        }
        let records = try await store.allRecords()
        XCTAssertTrue(records.isEmpty)
        transport.setBookmarkCategoryApproved(true)
        try await transport.enqueue(bookmark)
        let enabledRecords = try await store.allRecords()
        XCTAssertEqual(enabledRecords, [bookmark])
        transport.setBookmarkCategoryApproved(false)
        let retained = try await store.allRecords()
        XCTAssertEqual(retained, [bookmark], "Withdrawing runtime permission never erases cached data.")
#endif
    }

    private func record(_ dataClass: SyncDataClass, schema: UInt32 = 2, deleted: Bool = false) -> SyncRecord {
        let id = UUID()
        let device = DeviceID()
        let clock = HybridLogicalClock(physicalMilliseconds: 100, nodeID: device)
        return SyncRecord(
            recordID: id, entityID: id, schemaVersion: schema, dataClass: dataClass,
            modifiedAt: clock, originatingDevice: device,
            encryptedValue: EncryptedValue(keyVersion: 1, nonce: Data(repeating: 0, count: 12),
                                            ciphertextAndTag: Data(repeating: 0, count: 16)),
            tombstone: deleted ? Tombstone(entityID: id, deletedAt: clock, deletedBy: device,
                                           originalParentID: nil, originalOrderKey: nil,
                                           purgeAfterMilliseconds: 3_000_000_000) : nil
        )
    }
}
