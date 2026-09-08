import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Both language suites consume this exact repository resource, not a Swift
/// translation of its records. Envelopes are codec probes, not real encryption.
enum UnifiedSyncFixture {
    static let sha256 = "f1886032c54931f8dfd4180c5ff150698f85576ac70e52e3523f95291c3d8d00"

    struct Document: Decodable {
        let model_version: Int
        let device_id: String
        let records: [Sample]
    }

    struct Sample: Decodable {
        let name: String
        let entity_type: Int
        let data_class: String
        let payload: String

        var data: Data { Data(payload.utf8) }
    }

    static func load() throws -> (Data, Document) {
        let url = try XCTUnwrap(Bundle(for: UnifiedSyncWireContractTests.self)
            .url(forResource: "sync_wire_v3", withExtension: "json"))
        let data = try Data(contentsOf: url)
        return (data, try JSONDecoder().decode(Document.self, from: data))
    }

    static func object(_ data: Data) throws -> [String: Any] {
        try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    static func canonical(_ object: [String: Any]) throws -> Data {
        try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys, .withoutEscapingSlashes])
    }

    static func envelope(_ sample: Sample) throws -> SyncRecord {
        let value = try object(sample.data)
        let clock = try SharedTabWireReadPolicy.parseClock(
            value, physicalKey: "version_physical", logicalKey: "version_logical", deviceKey: "version_device"
        )
        let identity = try SharedTabWireReadPolicy.strictUUID(value, key: "id")
        return SyncRecord(
            recordID: identity, entityID: identity,
            schemaVersion: SharedSyncFormat.currentVersion,
            dataClass: try XCTUnwrap(SyncDataClass(rawValue: sample.data_class)),
            modifiedAt: clock, originatingDevice: clock.nodeID,
            encryptedValue: .init(keyVersion: 1, nonce: Data(repeating: 0xA1, count: 12),
                                  ciphertextAndTag: Data(repeating: 0xB2, count: 32))
        )
    }
}
