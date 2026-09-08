import CryptoKit
import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class UnifiedSyncWireContractTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()

    private struct Context {
        var devices: [DeviceID: Device] = [:]
        var workspaces: [WorkspaceID: Workspace] = [:]
        var pages: [TreeNodeID: TreeNode] = [:]
    }

    func testEveryEntityUsesTheSameGoldenBytes() throws {
        let (data, fixture) = try UnifiedSyncFixture.load()
        XCTAssertEqual(SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined(),
                       UnifiedSyncFixture.sha256)
        XCTAssertEqual(fixture.model_version, 3)
        XCTAssertEqual(fixture.records.count, 30)
        XCTAssertEqual(Set(fixture.records.map(\.entity_type)), Set(0...12))
        var context = Context()
        for sample in fixture.records {
            XCTAssertEqual(try reencode(sample, context: &context), sample.data, sample.name)
        }
    }

    func testEveryEntityRejectsOldAndUnknownVersions() throws {
        let (_, fixture) = try UnifiedSyncFixture.load()
        var context = Context()
        for sample in fixture.records {
            _ = try reencode(sample, context: &context)
            for version in [0, 1, 2, 4] {
                var value = try UnifiedSyncFixture.object(sample.data)
                value["model_version"] = version
                value["version_model"] = version
                let invalid = UnifiedSyncFixture.Sample(name: sample.name, entity_type: sample.entity_type,
                                                         data_class: sample.data_class,
                                                         payload: String(decoding: try UnifiedSyncFixture.canonical(value),
                                                                         as: UTF8.self))
                XCTAssertThrowsError(try reencode(invalid, context: &context), "\(sample.name):\(version)")
            }
        }
    }

    func testMissingIncomingFieldClockIsNotAuthoredFromTheTopClock() throws {
        let (_, fixture) = try UnifiedSyncFixture.load()
        var context = Context()
        for sample in fixture.records {
            _ = try reencode(sample, context: &context)
            var value = try UnifiedSyncFixture.object(sample.data)
            var fields = try XCTUnwrap(value["field_versions"] as? [String: Any])
            let key = try XCTUnwrap(fields.keys.sorted().first)
            fields.removeValue(forKey: key)
            value["field_versions"] = fields
            let invalid = UnifiedSyncFixture.Sample(name: sample.name, entity_type: sample.entity_type,
                                                     data_class: sample.data_class,
                                                     payload: String(decoding: try UnifiedSyncFixture.canonical(value),
                                                                     as: UTF8.self))
            XCTAssertThrowsError(try reencode(invalid, context: &context), sample.name)
        }
    }

    func testCreationAndSaveProvenanceSurviveNewerEditsAndRestart() throws {
        let (_, fixture) = try UnifiedSyncFixture.load()
        let sample = try XCTUnwrap(fixture.records.first { $0.name == "tree_saved_web" })
        let original = try codec.decodeTreeNode(UnifiedSyncFixture.envelope(sample), plaintext: sample.data)
        let creator = try XCTUnwrap(original.version.fieldVersions["created_at"])
        let saved = try XCTUnwrap(original.version.fieldVersions["is_temporary"])
        XCTAssertNotEqual(creator.nodeID, saved.nodeID)
        XCTAssertEqual(original.createdAt.physicalMilliseconds, 1_000)
        XCTAssertEqual(creator.submillisecondMicroseconds, 123)
        var edited = original
        edited.title = "An explicit later edit"
        let later = try original.version.modifiedAt.ticking(at: 5_000)
        edited.version = SyncVersion(modifiedAt: later, modifiedBy: later.nodeID)
        edited = CompanionFieldMerge.stampLocal(previous: original, candidate: edited)
        let forward = try CompanionFieldMerge.merge(original, edited)
        let backward = try CompanionFieldMerge.merge(edited, original)
        XCTAssertEqual(forward, backward)
        XCTAssertEqual(forward.version.schemaVersion, 3)
        XCTAssertEqual(forward.createdAt.physicalMilliseconds, 1_000)
        XCTAssertEqual(forward.creationProvenanceClock, creator)
        XCTAssertEqual(forward.version.fieldVersions["is_temporary"], saved)
        let restarted = try JSONDecoder().decode(TreeNode.self, from: JSONEncoder().encode(forward))
        XCTAssertEqual(restarted.creationProvenanceClock, creator)
        XCTAssertEqual(try codec.encode(restarted), try codec.encode(forward))
    }

    func testOtherDeviceKindRemainsMetadataNotARejectedClient() throws {
        let (_, fixture) = try UnifiedSyncFixture.load()
        let sample = try XCTUnwrap(fixture.records.first { $0.name == "device_mac" })
        var value = try UnifiedSyncFixture.object(sample.data)
        value["device_type"] = 3
        let device = try codec.decodeDevice(UnifiedSyncFixture.envelope(sample),
                                             plaintext: UnifiedSyncFixture.canonical(value))
        XCTAssertEqual(device.kind, .other)
        XCTAssertEqual(try codec.encode(device), try UnifiedSyncFixture.canonical(value))
    }

    private func reencode(_ sample: UnifiedSyncFixture.Sample, context: inout Context) throws -> Data {
        let record = try UnifiedSyncFixture.envelope(sample)
        let bytes = sample.data
        switch record.dataClass {
        case .device:
            let value = try codec.decodeDevice(record, plaintext: bytes)
            context.devices[value.id] = value
            return try codec.encode(value)
        case .workspace:
            let value = try codec.decodeWorkspace(record, plaintext: bytes)
            context.workspaces[value.id] = value
            return try codec.encode(value)
        case .treeNode:
            let value = try codec.decodeTreeNode(record, plaintext: bytes)
            context.pages[value.id] = value
            return try codec.encode(value)
        case .deviceTab:
            let value = try codec.decodeRemoteTab(record, plaintext: bytes,
                                                   devices: context.devices, workspaces: context.workspaces)
            try codec.validatePresenceTarget(value, pages: context.pages)
            return try codec.encode(value)
        case .deviceSession:
            return try codec.encode(codec.decodeSession(record, plaintext: bytes, devices: context.devices))
        case .historyVisit:
            return try codec.encode(codec.decodeHistory(record, plaintext: bytes))
        case .remoteCommand:
            // Shape/serialization only; the synthetic fixture signature never
            // passes an execution authorization path in this test.
            return try codec.encode(codec.decodeRemoteCommand(record, plaintext: bytes))
        case .appearance:
            return try codec.encode(codec.decodeAppearance(record, plaintext: bytes))
        case .permittedSetting:
            let value = try codec.decodePermittedSetting(record, plaintext: bytes)
            if sample.name.hasPrefix("extension_setup_") {
                let setup = try XCTUnwrap(CompanionExtensionSetup.decode(value))
                XCTAssertEqual(try setup.record(version: value.version), value)
            }
            if sample.name.hasPrefix("extension_storage_") {
                let setting = try XCTUnwrap(CompanionExtensionStorage.decode(value))
                XCTAssertEqual(try setting.record(version: value.version), value)
            }
            return try codec.encode(value)
        case .extensionInventory:
            return try codec.encode(codec.decodeExtensionInventory(record, plaintext: bytes))
        case .developerAsset:
            return try codec.encode(codec.decodeDeveloperAsset(record, plaintext: bytes))
        case .bookmark:
            return try codec.encode(codec.decodeBookmark(record, plaintext: bytes))
        case .deviceCapability:
            return try codec.encode(codec.decodeCapability(record, plaintext: bytes, knownDevices: context.devices))
        default:
            throw CompanionSyncBridgeError.unsupportedDataClass(record.dataClass)
        }
    }
}
