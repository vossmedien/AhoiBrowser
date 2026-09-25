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

    /// A deleted payload arrives with envelope tombstone metadata, which the
    /// codec requires to match the payload clock; the vectors carry payloads
    /// only, so the runner supplies the metadata a sender would attach.
    private func envelope(_ vector: Vector, _ payload: JSONValue) throws -> SyncRecord {
        let record = try UnifiedSyncFixture.envelope(UnifiedSyncFixture.Sample(
            name: vector.name, entity_type: vector.entityType,
            data_class: vector.dataClass,
            payload: String(decoding: payload.data, as: UTF8.self)))
        let object = try JSONSerialization.jsonObject(with: payload.data) as? [String: Any]
        guard object?["tombstone"] as? Bool == true else { return record }
        return SyncRecord(
            recordID: record.recordID, entityID: record.entityID,
            schemaVersion: record.schemaVersion, dataClass: record.dataClass,
            modifiedAt: record.modifiedAt, originatingDevice: record.originatingDevice,
            orderKey: record.orderKey, encryptedValue: record.encryptedValue,
            tombstone: Tombstone(
                entityID: record.entityID, deletedAt: record.modifiedAt,
                deletedBy: record.originatingDevice, originalParentID: nil,
                originalOrderKey: nil,
                purgeAfterMilliseconds: record.modifiedAt.physicalMilliseconds + 2_592_000_000))
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

    /// Flattens a model into sorted `path = value` leaves so a mismatch names
    /// the differing fields instead of printing two unordered dumps.
    private static func flatten(_ value: Any, path: String = "") -> [String: String] {
        let mirror = Mirror(reflecting: value)
        if mirror.displayStyle == .optional {
            guard let child = mirror.children.first else { return [path: "nil"] }
            return flatten(child.value, path: path)
        }
        if mirror.displayStyle == .dictionary {
            var result: [String: String] = [:]
            for pair in mirror.children {
                let entry = Array(Mirror(reflecting: pair.value).children)
                guard entry.count == 2 else { continue }
                result.merge(flatten(entry[1].value, path: "\(path)[\(entry[0].value)]")) { a, _ in a }
            }
            return result.isEmpty ? [path: "[:]"] : result
        }
        if type(of: value) == AnyHashable.self, let erased = value as? AnyHashable {
            return flatten(erased.base, path: path)
        }
        guard !mirror.children.isEmpty,
              mirror.displayStyle == .struct || mirror.displayStyle == .class ||
              mirror.displayStyle == .enum || mirror.displayStyle == .tuple ||
              mirror.displayStyle == .collection || mirror.displayStyle == .set else {
            return [path: String(describing: value)]
        }
        var result: [String: String] = [:]
        for (index, child) in mirror.children.enumerated() {
            let key = mirror.displayStyle == .set ? "\(child.value)" : (child.label ?? "\(index)")
            result.merge(flatten(child.value, path: path.isEmpty ? key : "\(path).\(key)")) { a, _ in a }
        }
        return result
    }

    /// Leaves that are not part of the wire payload and therefore not pinned
    /// by the vectors: envelope tombstone metadata (the runner synthesizes it
    /// from the payload clock, so the expected side always carries the merged
    /// record clock) and the local `OrderKey` tie-breaker the codec derives
    /// from the opaque `sort_key`, whose wire bytes stay in `wireSortKey`.
    private static let receiverLocalPrefixes = [
        "tombstone.deletedAt", "tombstone.deletedBy", "tombstone.purgeAfterMilliseconds",
    ]

    private static func wireFields(_ value: Any) -> [String: String] {
        let leaves = flatten(value)
        let opaqueSortKey = leaves["wireSortKey"].map { $0 != "nil" } ?? false
        return leaves.filter { key, _ in
            !receiverLocalPrefixes.contains { key.hasPrefix($0) } &&
                !(opaqueSortKey && key.hasPrefix("orderKey.tieBreaker"))
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
                    let diff = Self.wireFields(merged).symmetricDifferenceDescription(
                        Self.wireFields(want as Any))
                    if !diff.isEmpty {
                        XCTFail("\(vector.name): merged differs from expectation: \(diff)")
                    }
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

private extension Dictionary where Key == String, Value == String {
    func symmetricDifferenceDescription(_ other: [String: String]) -> String {
        Set(keys).union(other.keys).sorted().compactMap { key in
            self[key] == other[key] ? nil : "\(key): swift=\(self[key] ?? "-") expected=\(other[key] ?? "-")"
        }.joined(separator: "; ")
    }
}
