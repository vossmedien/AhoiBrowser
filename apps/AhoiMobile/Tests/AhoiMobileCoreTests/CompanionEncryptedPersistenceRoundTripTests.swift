import Foundation
import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionEncryptedPersistenceRoundTripTests: XCTestCase {
    func testProductionAESGCMPayloadPersistsAcrossRestartWithoutPlaintextLeak() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiEncryptedPersistenceTests-\(UUID().uuidString)",
            isDirectory: true
        )
        let recordsURL = directory.appendingPathComponent("records.json")
        defer { try? FileManager.default.removeItem(at: directory) }

        let marker = "AHOI-ENCRYPTED-PERSISTENCE-9F0A"
        let title = "\(marker) Secret Voyage"
        let url = "https://roundtrip.ahoibrowser.test/private/\(marker)"
        let deviceID = DeviceID(
            rawValue: UUID(uuidString: "71000000-0000-4000-8000-000000000001")!
        )
        let clock = HybridLogicalClock(
            physicalMilliseconds: 1_780_000_000_123,
            submillisecondMicroseconds: 456,
            logicalCounter: 7,
            nodeID: deviceID
        )
        let historyFieldNames: [String] = [
            "device_id", "url", "title", "last_visit", "visit_count",
            "transition", "tombstone",
        ]
        let version = SyncVersion(
            modifiedAt: clock,
            modifiedBy: deviceID,
            fieldVersions: Dictionary(uniqueKeysWithValues: historyFieldNames.map {
                ($0, clock)
            })
        )
        let original = try HistoryVisit(
            visitID: HistoryVisitID(
                rawValue: UUID(uuidString: "72000000-0000-4000-8000-000000000002")!
            ),
            deviceID: deviceID,
            title: title,
            url: url,
            visitedAt: clock,
            transition: "typed-\(marker)",
            visitCount: 3,
            version: version
        )

        let keyConfiguration = CompanionSyncKeyConfiguration(
            service: "app.ahoibrowser.tests.sync",
            account: "encrypted-persistence-roundtrip",
            keyVersion: 7
        )
        let keyData = Data(repeating: 0x42, count: 32)
        let writerCodec = CompanionPayloadCodec(sealer: KeychainCompanionPayloadSealer(
            configuration: keyConfiguration,
            keyLoader: { keyData }
        ))
        let wireCodec = DesktopWirePayloadCodec()
        let wirePayload = try wireCodec.encode(original)
        let record = try writerCodec.makeRecord(
            recordID: original.id.rawValue,
            entityID: original.id.rawValue,
            dataClass: .historyVisit,
            version: original.version,
            plaintext: wirePayload,
            tombstone: original.tombstone
        )

        XCTAssertEqual(record.encryptedValue.algorithm, .aes256GCM)
        XCTAssertEqual(record.encryptedValue.keyVersion, keyConfiguration.keyVersion)

        let writerStore = try FileSyncRecordStore(fileURL: recordsURL)
        try await writerStore.upsert(record)

        let rawFile = try Data(contentsOf: recordsURL)
        let rawText = String(decoding: rawFile, as: UTF8.self)
        XCTAssertFalse(rawText.contains(marker))
        XCTAssertFalse(rawText.contains(title))
        XCTAssertFalse(rawText.contains(url))
        XCTAssertFalse(rawText.contains("roundtrip.ahoibrowser.test"))

        let restartedStore = try FileSyncRecordStore(fileURL: recordsURL)
        let persistedRecord = try await restartedStore.record(for: record.recordID)
        let restoredRecord = try XCTUnwrap(persistedRecord)
        XCTAssertEqual(restoredRecord, record)

        let readerCodec = CompanionPayloadCodec(sealer: KeychainCompanionPayloadSealer(
            configuration: keyConfiguration,
            keyLoader: { keyData }
        ))
        let restoredWirePayload = try readerCodec.openData(restoredRecord)
        XCTAssertEqual(restoredWirePayload, wirePayload)

        let restored = try wireCodec.decodeHistory(
            restoredRecord,
            plaintext: restoredWirePayload
        )
        XCTAssertEqual(restored, original)
    }
}
