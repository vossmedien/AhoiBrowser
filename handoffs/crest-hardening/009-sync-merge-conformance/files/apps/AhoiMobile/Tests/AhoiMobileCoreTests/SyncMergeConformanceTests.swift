import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Runs the shared format-3 merge conformance vectors
/// (`fixtures/sync-conformance/merge_v3.json`) against the Companion's field
/// merge. Chromium runs the same vectors in `sync_merge_conformance_unittest.cc`.
/// Swift has no decision enum, so a vector passes when the merged model equals
/// the decoded expected payload, or when both sides reject the input.
final class SyncMergeConformanceTests: XCTestCase {
    private struct Vector: Decodable {
        let name: String
        let entityType: Int
        let dataClass: String
        let existing: JSONValue
        let incoming: JSONValue
        let expect: Expectation
    }

    private struct Expectation: Decodable {
        let decision: String
        let merged: JSONValue?
    }

    private struct Document: Decodable {
        let schemaVersion: Int
        let cases: [Vector]
    }

    /// Keeps the raw payload so it can be re-serialized for the codec.
    private struct JSONValue: Decodable {
        let data: Data
        init(from decoder: Decoder) throws {
            let object = try AnyJSON(from: decoder).value
            data = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        }
    }

    private struct AnyJSON: Decodable {
        let value: Any
        init(from decoder: Decoder) throws {
            let container = try decoder.singleValueContainer()
            if container.decodeNil() { value = NSNull() }
            else if let bool = try? container.decode(Bool.self) { value = bool }
            else if let int = try? container.decode(Int64.self) { value = int }
            else if let double = try? container.decode(Double.self) { value = double }
            else if let string = try? container.decode(String.self) { value = string }
            else if let array = try? container.decode([AnyJSON].self) { value = array.map(\.value) }
            else { value = try container.decode([String: AnyJSON].self).mapValues(\.value) }
        }
    }

    private static let covered: Set<Int> = [1, 2, 5, 14]

    private func vectorsURL() -> URL {
        // Tests run on the Mac host, so the repository file is readable directly.
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }
        return url.appendingPathComponent("fixtures/sync-conformance/merge_v3.json")
    }

    private func envelope(_ vector: Vector, _ payload: JSONValue) throws -> SyncRecord {
        try UnifiedSyncFixture.envelope(UnifiedSyncFixture.Sample(
            name: vector.name, entity_type: vector.entityType,
            data_class: vector.dataClass,
            payload: String(decoding: payload.data, as: UTF8.self)))
    }

    private func devices() throws -> [DeviceID: Device] {
        let (_, document) = try UnifiedSyncFixture.load()
        let codec = DesktopWirePayloadCodec()
        var result: [DeviceID: Device] = [:]
        for sample in document.records where sample.data_class == "device" {
            let device = try codec.decodeDevice(try UnifiedSyncFixture.envelope(sample),
                                                plaintext: sample.data)
            result[device.id] = device
        }
        return result
    }

    /// Returns the merged model and the decoded expectation, both type-erased.
    private func run(_ vector: Vector, merged expected: JSONValue?) throws -> (AnyHashable, AnyHashable?) {
        let codec = DesktopWirePayloadCodec()
        let old = try envelope(vector, vector.existing)
        let new = try envelope(vector, vector.incoming)
        switch vector.entityType {
        case 1:
            let result = try CompanionFieldMerge.merge(
                try codec.decodeWorkspace(old, plaintext: vector.existing.data),
                try codec.decodeWorkspace(new, plaintext: vector.incoming.data))
            let want = try expected.map { try codec.decodeWorkspace(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want)
        case 2:
            let result = try CompanionFieldMerge.merge(
                try codec.decodeTreeNode(old, plaintext: vector.existing.data),
                try codec.decodeTreeNode(new, plaintext: vector.incoming.data))
            let want = try expected.map { try codec.decodeTreeNode(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want)
        case 5:
            let known = try devices()
            let result = try CompanionReadModelFieldMerge.merge(
                try codec.decodeSession(old, plaintext: vector.existing.data, devices: known),
                try codec.decodeSession(new, plaintext: vector.incoming.data, devices: known))
            let want = try expected.map {
                try codec.decodeSession(try envelope(vector, $0), plaintext: $0.data, devices: known)
            }
            return (result, want)
        case 14:
            let result = try CompanionWorkspaceStructureMerge.merge(
                try codec.decodeArchiveEntry(old, plaintext: vector.existing.data),
                try codec.decodeArchiveEntry(new, plaintext: vector.incoming.data))
            let want = try expected.map { try codec.decodeArchiveEntry(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want)
        default:
            throw XCTSkip("entity \(vector.entityType) has no Companion field merge")
        }
    }

    func testSharedMergeVectors() throws {
        let document = try JSONDecoder().decode(Document.self, from: Data(contentsOf: vectorsURL()))
        XCTAssertEqual(document.schemaVersion, 1)
        var executed = 0
        for vector in document.cases where Self.covered.contains(vector.entityType) {
            executed += 1
            let expected = vector.expect.decision == "invalid" ? nil : vector.expect.merged
            do {
                let (merged, want) = try run(vector, merged: expected)
                if vector.expect.decision == "invalid" {
                    XCTFail("\(vector.name): expected rejection, Swift merged \(merged)")
                } else {
                    XCTAssertEqual(merged, want, vector.name)
                }
            } catch let skip as XCTSkip {
                throw skip
            } catch {
                if vector.expect.decision != "invalid" {
                    XCTFail("\(vector.name): Swift rejected (\(error)); expected \(vector.expect.decision)")
                }
            }
        }
        XCTAssertGreaterThan(executed, 0)
    }

    /// Records which contract entities still lack a Companion field merge, so
    /// adding one without joining the conformance run fails loudly.
    func testUncoveredEntitiesAreExplicit() throws {
        let document = try JSONDecoder().decode(Document.self, from: Data(contentsOf: vectorsURL()))
        let present = Set(document.cases.map(\.entityType))
        XCTAssertEqual(present.subtracting(Self.covered), [6, 7, 8])
    }
}
