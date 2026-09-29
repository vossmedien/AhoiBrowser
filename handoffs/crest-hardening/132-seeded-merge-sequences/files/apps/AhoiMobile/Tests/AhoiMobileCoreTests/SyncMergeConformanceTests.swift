import Foundation
import CryptoKit
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
        let inputValid: Bool?
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

    private struct SequenceDocument: Decodable {
        let schemaVersion: Int
        let cases: [SequenceCase]
    }

    private struct SequenceCase: Decodable {
        let name: String
        let entityType: Int
        let dataClass: String
        let initial: JSONValue
        let steps: [SequenceStep]
        let expect: Expectation
    }

    private struct SequenceStep: Decodable {
        let name: String
        let incoming: JSONValue
        let inputValid: Bool
        let expect: Expectation
    }

    /// Keeps the raw payload so it can be re-serialized for the codec.
    private struct JSONValue: Decodable {
        let data: Data
        init(data: Data) { self.data = data }
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

    private static let covered: Set<Int> = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]

    private func vectorsURL(_ name: String = "merge_v3.json") -> URL {
        if name == "merge_v3.json",
           let path = ProcessInfo.processInfo.environment["AHOI_SYNC_CONFORMANCE_FIXTURE"] {
            return URL(fileURLWithPath: path)
        }
        if name == "merge_sequences_v3.json",
           let path = ProcessInfo.processInfo.environment["AHOI_SYNC_CONFORMANCE_SEQUENCE_FIXTURE"] {
            return URL(fileURLWithPath: path)
        }
        // Tests run on the Mac host, so the repository file is readable directly.
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }
        return url.appendingPathComponent("fixtures/sync-conformance/" + name)
    }

    /// A deleted payload arrives with envelope tombstone metadata, which the
    /// codec requires to match the payload clock; the vectors carry payloads
    /// only, so the runner supplies the metadata a sender would attach.
    private func envelope(_ vector: Vector, _ payload: JSONValue) throws -> SyncRecord {
        try envelope(name: vector.name, type: vector.entityType,
                     dataClass: vector.dataClass, payload: payload)
    }

    private func envelope(
        name: String, type: Int, dataClass: String, payload: JSONValue
    ) throws -> SyncRecord {
        let record = try UnifiedSyncFixture.envelope(UnifiedSyncFixture.Sample(
            name: name, entity_type: type, data_class: dataClass,
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

    private func workspaces() throws -> [WorkspaceID: Workspace] {
        let (_, document) = try UnifiedSyncFixture.load()
        let codec = DesktopWirePayloadCodec()
        var result: [WorkspaceID: Workspace] = [:]
        for sample in document.records where sample.data_class == "workspace" {
            let workspace = try codec.decodeWorkspace(
                try UnifiedSyncFixture.envelope(sample), plaintext: sample.data)
            result[workspace.id] = workspace
        }
        return result
    }

    private struct Rejection: Error {
        let stage: String
        let underlying: any Error
    }
    private enum ExportError: Error {
        case unsupportedEntity(Int), invalidConfiguration
    }

    /// Product validation errors carry their actual stage. Fixture I/O, expected
    /// decoding and output encoding errors remain harness failures, never invalid.
    private func mergeDecoded<T>(
        _ existing: @autoclosure () throws -> T,
        _ incoming: @autoclosure () throws -> T,
        using merge: (T, T) throws -> T
    ) throws -> T {
        let old: T
        let new: T
        do { old = try existing(); new = try incoming() }
        catch { throw Rejection(stage: "decode", underlying: error) }
        do { return try merge(old, new) }
        catch { throw Rejection(stage: "merge", underlying: error) }
    }

    /// Returns the merged model and the decoded expectation, both type-erased.
    private func run(_ vector: Vector, merged expected: JSONValue?) throws -> (Any, Any?, Data) {
        let codec = DesktopWirePayloadCodec()
        let old = try envelope(vector, vector.existing)
        let new = try envelope(vector, vector.incoming)
        switch vector.entityType {
        case 0:
            let result = try mergeDecoded(
                try codec.decodeDevice(old, plaintext: vector.existing.data),
                try codec.decodeDevice(new, plaintext: vector.incoming.data),
                using: CompanionReadModelFieldMerge.merge)
            let want = try expected.map { try codec.decodeDevice(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 1:
            let result = try mergeDecoded(
                try codec.decodeWorkspace(old, plaintext: vector.existing.data),
                try codec.decodeWorkspace(new, plaintext: vector.incoming.data),
                using: CompanionFieldMerge.merge)
            let want = try expected.map { try codec.decodeWorkspace(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 2:
            let result = try mergeDecoded(
                try codec.decodeTreeNode(old, plaintext: vector.existing.data),
                try codec.decodeTreeNode(new, plaintext: vector.incoming.data),
                using: CompanionFieldMerge.merge)
            let want = try expected.map { try codec.decodeTreeNode(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 3:
            let result = try mergeDecoded(
                try codec.decodeHistory(old, plaintext: vector.existing.data),
                try codec.decodeHistory(new, plaintext: vector.incoming.data),
                using: CompanionReadModelFieldMerge.merge)
            let want = try expected.map { try codec.decodeHistory(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 4:
            let knownDevices = try devices()
            let knownWorkspaces = try workspaces()
            let result = try mergeDecoded(
                try codec.decodeRemoteTab(old, plaintext: vector.existing.data,
                                          devices: knownDevices, workspaces: knownWorkspaces),
                try codec.decodeRemoteTab(new, plaintext: vector.incoming.data,
                                          devices: knownDevices, workspaces: knownWorkspaces),
                using: CompanionReadModelFieldMerge.merge)
            let want = try expected.map {
                try codec.decodeRemoteTab(try envelope(vector, $0), plaintext: $0.data,
                                          devices: knownDevices, workspaces: knownWorkspaces)
            }
            return (result, want, try codec.encode(result))
        case 5:
            let known = try devices()
            let result = try mergeDecoded(
                try codec.decodeSession(old, plaintext: vector.existing.data, devices: known),
                try codec.decodeSession(new, plaintext: vector.incoming.data, devices: known),
                using: CompanionReadModelFieldMerge.merge)
            let want = try expected.map {
                try codec.decodeSession(try envelope(vector, $0), plaintext: $0.data, devices: known)
            }
            return (result, want, try codec.encode(result))
        case 14:
            let result = try mergeDecoded(
                try codec.decodeArchiveEntry(old, plaintext: vector.existing.data),
                try codec.decodeArchiveEntry(new, plaintext: vector.incoming.data),
                using: CompanionWorkspaceStructureMerge.merge)
            let want = try expected.map { try codec.decodeArchiveEntry(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 6:
            let result = try mergeDecoded(
                try codec.decodeRemoteCommand(old, plaintext: vector.existing.data),
                try codec.decodeRemoteCommand(new, plaintext: vector.incoming.data),
                using: CompanionProductFieldMerge.merge)
            let want = try expected.map { try codec.decodeRemoteCommand(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 7:
            let result = try mergeDecoded(
                try codec.decodeAppearance(old, plaintext: vector.existing.data),
                try codec.decodeAppearance(new, plaintext: vector.incoming.data),
                using: CompanionProductFieldMerge.merge)
            let want = try expected.map { try codec.decodeAppearance(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 8:
            let result = try mergeDecoded(
                try codec.decodePermittedSetting(old, plaintext: vector.existing.data),
                try codec.decodePermittedSetting(new, plaintext: vector.incoming.data),
                using: CompanionProductFieldMerge.merge)
            let want = try expected.map { try codec.decodePermittedSetting(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 9:
            let result = try mergeDecoded(
                try codec.decodeExtensionInventory(old, plaintext: vector.existing.data),
                try codec.decodeExtensionInventory(new, plaintext: vector.incoming.data),
                using: CompanionProductFieldMerge.merge)
            let want = try expected.map {
                try codec.decodeExtensionInventory(try envelope(vector, $0), plaintext: $0.data)
            }
            return (result, want, try codec.encode(result))
        case 10:
            let result = try mergeDecoded(
                try codec.decodeDeveloperAsset(old, plaintext: vector.existing.data),
                try codec.decodeDeveloperAsset(new, plaintext: vector.incoming.data),
                using: CompanionProductFieldMerge.merge)
            let want = try expected.map {
                try codec.decodeDeveloperAsset(try envelope(vector, $0), plaintext: $0.data)
            }
            return (result, want, try codec.encode(result))
        case 11:
            let result = try mergeDecoded(
                try codec.decodeBookmark(old, plaintext: vector.existing.data),
                try codec.decodeBookmark(new, plaintext: vector.incoming.data),
                using: CompanionBookmarkFieldMerge.merge)
            let want = try expected.map { try codec.decodeBookmark(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        case 12:
            let known = try devices()
            let result = try mergeDecoded(
                try codec.decodeCapability(old, plaintext: vector.existing.data, knownDevices: known),
                try codec.decodeCapability(new, plaintext: vector.incoming.data, knownDevices: known),
                using: CompanionCapabilityDomain.merge)
            let want = try expected.map {
                try codec.decodeCapability(try envelope(vector, $0), plaintext: $0.data,
                                           knownDevices: known)
            }
            return (result, want, try codec.encode(result))
        case 13:
            let result = try mergeDecoded(
                try codec.decodeSplitGroup(old, plaintext: vector.existing.data),
                try codec.decodeSplitGroup(new, plaintext: vector.incoming.data),
                using: CompanionWorkspaceStructureMerge.merge)
            let want = try expected.map { try codec.decodeSplitGroup(try envelope(vector, $0), plaintext: $0.data) }
            return (result, want, try codec.encode(result))
        default:
            throw ExportError.unsupportedEntity(vector.entityType)
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
        // Data reflects its storage pointer; compare its bytes instead.
        if let data = value as? Data { return [path: data.base64EncodedString()] }
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
        try checkVectors("merge_v3.json")
    }

    func testUTF8SortKeyVectors() throws {
        try checkVectors("merge_utf8_sort_keys_v3.json")
    }

    func testInventoryAssetVectors() throws {
        try checkVectors("merge_inventory_asset_v3.json")
    }

    func testRemainingEntityVectors() throws {
        try checkVectors("merge_remaining_entities_v3.json")
    }

    private func canonicalWireJSON(_ data: Data) throws -> Data {
        let object = try JSONSerialization.jsonObject(with: data)
        return try JSONSerialization.data(
            withJSONObject: object, options: [.sortedKeys, .withoutEscapingSlashes])
    }

    func testSeededMergeSequences() throws {
        if let path = ProcessInfo.processInfo.environment["AHOI_SYNC_CONFORMANCE_SEQUENCE_FIXTURE"],
           !path.hasPrefix("/") { throw ExportError.invalidConfiguration }
        let fixture = vectorsURL("merge_sequences_v3.json")
        let bytes = try Data(contentsOf: fixture)
        let document = try JSONDecoder().decode(SequenceDocument.self, from: bytes)
        XCTAssertEqual(document.schemaVersion, 1)
        var outputs: [[String: Any]] = []
        for sequence in document.cases {
            XCTAssertTrue(Self.covered.contains(sequence.entityType))
            XCTAssertFalse(sequence.steps.isEmpty)
            var current = sequence.initial
            for step in sequence.steps {
                let pair = Vector(
                    name: sequence.name + "." + step.name,
                    entityType: sequence.entityType, dataClass: sequence.dataClass,
                    inputValid: step.inputValid, existing: current,
                    incoming: step.incoming, expect: step.expect)
                do {
                    let (merged, want, actualBytes) = try run(
                        pair, merged: step.expect.merged)
                    if step.expect.decision == "invalid" {
                        XCTFail("\(pair.name): expected rejection, merged \(merged)")
                    } else {
                        let diff = Self.wireFields(merged).symmetricDifferenceDescription(
                            Self.wireFields(want as Any))
                        if !diff.isEmpty { XCTFail("\(pair.name): \(diff)") }
                    }
                    // The next operation consumes the actual product codec
                    // output, never the expected intermediate fixture.
                    current = JSONValue(data: actualBytes)
                } catch let rejection as Rejection {
                    if step.expect.decision != "invalid" ||
                        (step.inputValid && rejection.stage == "decode") {
                        XCTFail("\(pair.name): unexpected \(rejection.stage) rejection")
                    }
                    // Invalid incoming records are quarantined; the actual
                    // accepted state remains the input to the next step.
                }
            }
            guard let expected = sequence.expect.merged else {
                XCTFail("\(sequence.name): final expected payload missing")
                continue
            }
            if try canonicalWireJSON(current.data) != canonicalWireJSON(expected.data) {
                XCTFail("\(sequence.name): final canonical wire payload differs")
            }
            outputs.append([
                "name": sequence.name, "entityType": sequence.entityType,
                "outcome": "accepted", "rejectionStage": NSNull(),
                "payload": try JSONSerialization.jsonObject(with: current.data),
            ])
        }
        XCTAssertFalse(outputs.isEmpty)
        try exportResults(outputs, fixture: fixture, bytes: bytes)
    }

    private func exportResults(_ results: [[String: Any]], fixture: URL, bytes: Data) throws {
        let env = ProcessInfo.processInfo.environment
        guard let directory = env["AHOI_SYNC_CONFORMANCE_OUTPUT_DIR"] else { return }
        guard directory.hasPrefix("/"),
              let runID = env["AHOI_SYNC_CONFORMANCE_RUN_ID"],
              !runID.isEmpty, runID.utf8.count <= 120,
              runID.utf8.allSatisfy({
                  (65...90).contains($0) || (97...122).contains($0) ||
                  (48...57).contains($0) || $0 == 46 || $0 == 95 || $0 == 45
              }) else { throw ExportError.invalidConfiguration }
        let document: [String: Any] = [
            "schemaVersion": 1, "kind": "ahoi-sync-merge-output",
            "implementation": "swift", "runId": runID, "complete": true,
            "fixtureName": fixture.lastPathComponent,
            "fixtureSha256": SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined(),
            "cases": results,
        ]
        let data = try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys])
        let path = URL(fileURLWithPath: directory, isDirectory: true)
            .appendingPathComponent("swift-" + fixture.lastPathComponent)
        // Existing files (including interrupted output) are never reused.
        try data.write(to: path, options: [.withoutOverwriting])
    }

    private func checkVectors(_ name: String) throws {
        if name == "merge_v3.json",
           let override = ProcessInfo.processInfo.environment["AHOI_SYNC_CONFORMANCE_FIXTURE"],
           !override.hasPrefix("/") { throw ExportError.invalidConfiguration }
        let fixture = vectorsURL(name)
        let bytes = try Data(contentsOf: fixture)
        let document = try JSONDecoder().decode(Document.self, from: bytes)
        XCTAssertEqual(document.schemaVersion, 1)
        XCTAssertEqual(Set(document.cases.map(\.entityType)).subtracting(Self.covered), [])
        var results: [[String: Any]] = []
        for vector in document.cases {
            let expected = vector.expect.decision == "invalid" ? nil : vector.expect.merged
            var output: [String: Any] = [
                "name": vector.name, "entityType": vector.entityType,
                "outcome": "error", "payload": NSNull(), "rejectionStage": NSNull(),
            ]
            do {
                let (merged, want, actualBytes) = try run(vector, merged: expected)
                // Actual product codec bytes, not reflection or expected payload.
                output["payload"] = try JSONSerialization.jsonObject(with: actualBytes)
                output["outcome"] = "accepted"
                if vector.expect.decision == "invalid" {
                    XCTFail("\(vector.name): expected rejection, Swift merged \(merged)")
                } else {
                    let diff = Self.wireFields(merged).symmetricDifferenceDescription(
                        Self.wireFields(want as Any))
                    if !diff.isEmpty {
                        XCTFail("\(vector.name): merged differs from expectation: \(diff)")
                    }
                }
            } catch let rejection as Rejection {
                output["outcome"] = "invalid"
                output["rejectionStage"] = rejection.stage
                if vector.inputValid == true && rejection.stage == "decode" {
                    XCTFail("\(vector.name): valid inputs must reach merge validation")
                }
                if vector.expect.decision != "invalid" {
                    XCTFail("\(vector.name): Swift rejected (\(rejection.underlying)); expected \(vector.expect.decision)")
                }
            } catch {
                // Unexpected harness/encoding errors cannot satisfy invalid vectors.
                XCTFail("\(vector.name): harness or output encoding failed: \(error)")
            }
            results.append(output)
        }
        XCTAssertFalse(results.isEmpty)
        try exportResults(results, fixture: fixture, bytes: bytes)
    }

    /// Every contract entity with shared vectors has a Companion field merge
    /// in the conformance run, so a new entity without one fails loudly.
    func testEveryVectorEntityIsCovered() throws {
        let document = try JSONDecoder().decode(Document.self, from: Data(contentsOf: vectorsURL()))
        let present = Set(document.cases.map(\.entityType))
        XCTAssertEqual(present.subtracting(Self.covered), [])
    }

    private struct ProjectionDocument: Decodable {
        let schemaVersion: Int
        let modelVersion: Int
        let arrayOrders: [String]
        let cases: [ProjectionCase]
    }

    private struct ProjectionCase: Decodable {
        let name: String
        let frames: [ProjectionFrame]
    }

    private struct ProjectionFrame: Decodable {
        let name: String
        let workspaces: [JSONValue]
        let nodes: [JSONValue]
        let unchangedNodeIdsFromPreviousFrame: [String]
        let expect: ProjectionExpectation
    }

    private struct ProjectionExpectation: Decodable {
        struct Route: Decodable {
            let workspaceId: String
            let classification: String
            let targetWorkspaceId: String?
        }
        struct Node: Decodable {
            let id: String
            let workspaceId: String
            let parentId: String?
        }
        struct Siblings: Decodable {
            let workspaceId: String
            let parentId: String?
            let nodeIds: [String]
        }
        let workspaceRoutes: [Route]
        let effectiveNodes: [Node]
        let siblingOrder: [Siblings]
        let unconstrainedNodeIds: [String]
        let notLiveNodeIds: [String]
    }

    private func arrayOrders<T>(_ input: [T]) -> [[T]] {
        [input, Array(input.reversed()),
         input.count > 1 ? Array(input.dropFirst()) + [input[0]] : input]
    }

    func testSharedWorkspaceMergeProjectionFrames() throws {
        let url = vectorsURL().deletingLastPathComponent()
            .appendingPathComponent("workspace_merge_projection_v3.json")
        let document = try JSONDecoder().decode(ProjectionDocument.self, from: Data(contentsOf: url))
        XCTAssertEqual(document.schemaVersion, 1)
        XCTAssertEqual(document.modelVersion, 3)
        XCTAssertEqual(document.arrayOrders, ["as-written", "reversed", "rotate-left"])
        let codec = DesktopWirePayloadCodec()
        var executions = 0
        for testCase in document.cases {
            var previousNodes: [String: Data] = [:]
            for frame in testCase.frames {
                let label = testCase.name + "/" + frame.name
                let workspaces = try frame.workspaces.map { payload in
                    try codec.decodeWorkspace(envelope(name: label, type: 1,
                        dataClass: SyncDataClass.workspace.rawValue, payload: payload),
                        plaintext: payload.data)
                }
                let nodes = try frame.nodes.map { payload in
                    try codec.decodeTreeNode(envelope(name: label, type: 2,
                        dataClass: SyncDataClass.treeNode.rawValue, payload: payload),
                        plaintext: payload.data)
                }
                let nodeBytes = try Dictionary(uniqueKeysWithValues: nodes.map {
                    ($0.id.rawValue.uuidString.lowercased(), try codec.encode($0))
                })
                for id in frame.unchangedNodeIdsFromPreviousFrame {
                    XCTAssertNotNil(previousNodes[id], label + " previous " + id)
                    XCTAssertEqual(previousNodes[id], nodeBytes[id], label + " unchanged " + id)
                }
                for (workspaceOrder, orderedWorkspaces) in arrayOrders(workspaces).enumerated() {
                    for (nodeOrder, orderedNodes) in arrayOrders(nodes).enumerated() {
                        let context = label + " workspaces=\(workspaceOrder) nodes=\(nodeOrder)"
                        let beforeWorkspaces = try orderedWorkspaces.map { try codec.encode($0) }
                        let beforeNodes = try orderedNodes.map { try codec.encode($0) }
                        let snapshot = CompanionSnapshot(workspaces: orderedWorkspaces, treeNodes: orderedNodes)
                        let view = snapshot.treeNodesForPresentation
                        for route in frame.expect.workspaceRoutes {
                            let id = WorkspaceID(rawValue: try XCTUnwrap(UUID(uuidString: route.workspaceId)))
                            XCTAssertEqual(snapshot.liveWorkspaceDestination(id)?.rawValue.uuidString.lowercased(),
                                           route.targetWorkspaceId, context + " route " + route.classification)
                        }
                        for expected in frame.expect.effectiveNodes {
                            let node = try XCTUnwrap(view.first {
                                $0.id.rawValue.uuidString.lowercased() == expected.id
                            }, context + " node " + expected.id)
                            XCTAssertFalse(node.isDeleted, context)
                            XCTAssertEqual(node.workspaceID.rawValue.uuidString.lowercased(),
                                           expected.workspaceId, context)
                            XCTAssertEqual(node.parentID?.rawValue.uuidString.lowercased(), expected.parentId, context)
                        }
                        for siblings in frame.expect.siblingOrder {
                            let constrained = Set(siblings.nodeIds)
                            let actual = view.filter {
                                !$0.isDeleted && constrained.contains($0.id.rawValue.uuidString.lowercased()) &&
                                    $0.workspaceID.rawValue.uuidString.lowercased() == siblings.workspaceId &&
                                    $0.parentID?.rawValue.uuidString.lowercased() == siblings.parentId
                            }.sorted(by: CompanionTreePosition.precedes).map { $0.id.rawValue.uuidString.lowercased() }
                            XCTAssertEqual(actual, siblings.nodeIds, context + " sibling order")
                        }
                        for id in frame.expect.notLiveNodeIds {
                            XCTAssertFalse(view.contains {
                                $0.id.rawValue.uuidString.lowercased() == id && !$0.isDeleted
                            }, context + " tombstoned " + id)
                        }
                        let constrainedIDs = Set(orderedNodes.filter { !$0.isDeleted }.map {
                            $0.id.rawValue.uuidString.lowercased()
                        }).subtracting(frame.expect.unconstrainedNodeIds)
                        XCTAssertEqual(Set(frame.expect.effectiveNodes.map(\.id)), constrainedIDs, context)
                        XCTAssertEqual(snapshot.treeNodesForPresentation, view, context + " repeated")
                        let restored = try JSONDecoder().decode(
                            CompanionSnapshot.self, from: JSONEncoder().encode(snapshot))
                        XCTAssertEqual(restored.treeNodesForPresentation, view, context + " reload")
                        XCTAssertEqual(try snapshot.workspaces.map { try codec.encode($0) }, beforeWorkspaces, context)
                        XCTAssertEqual(try snapshot.treeNodes.map { try codec.encode($0) }, beforeNodes, context)
                        XCTAssertEqual(snapshot.workspaces.map(\.version), orderedWorkspaces.map(\.version), context)
                        XCTAssertEqual(snapshot.treeNodes.map(\.version), orderedNodes.map(\.version), context)
                        XCTAssertEqual(try restored.treeNodes.map { try codec.encode($0) }, beforeNodes, context)
                        XCTAssertEqual(try restored.workspaces.map { try codec.encode($0) }, beforeWorkspaces, context)
                        executions += 1
                    }
                }
                previousNodes = nodeBytes
            }
        }
        let frames = document.cases.reduce(0) { $0 + $1.frames.count }
        XCTAssertGreaterThan(executions, 0)
        XCTAssertEqual(executions, frames * 9)
        print("Workspace projection conformance: \(document.cases.count) cases, \(frames) frames, \(executions) projections")
    }
}

private extension Dictionary where Key == String, Value == String {
    func symmetricDifferenceDescription(_ other: [String: String]) -> String {
        Set(keys).union(other.keys).sorted().compactMap { key in
            if let left = self[key], let right = other[key],
               left.utf8.elementsEqual(right.utf8) { return nil }
            return "\(key): swift=\(self[key] ?? "-") expected=\(other[key] ?? "-")"
        }.joined(separator: "; ")
    }
}
