import CryptoKit
import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Conformance preparation against the one C++/Swift contract resource. No
/// writer, CloudKit account, native Chromium runtime or network is constructed.
final class SharedTabFrozenContractTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()

    func testCanonicalFixtureHashAndCapabilityV2Bytes() throws {
        let (data, fixture) = try contract()
        XCTAssertEqual(SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined(),
                       "f640a7223c8bcb894625c2fc3041b2c561116c6879333b01ebe3a0b9b72f6777")
        for payload in try capabilityPayloads(fixture) {
            let capability = try decodeCapability(payload)
            XCTAssertEqual(capability.id, SharedTabContract.capabilityID(for: capability.deviceID))
            XCTAssertEqual(try codec.encode(capability), try canonical(payload))
            XCTAssertEqual(capability.version.schemaVersion, 2)
            XCTAssertThrowsError(try codec.decodeCapability(envelope(payload, .deviceCapability),
                                                            plaintext: canonical(payload), knownDevices: [:]))
            var invalid = payload
            invalid["readable_models"] = [3, 2, 2]
            XCTAssertThrowsError(try decodeCapability(invalid))
            invalid = payload
            invalid["id"] = UUID().uuidString.lowercased()
            XCTAssertThrowsError(try decodeCapability(invalid))
            invalid = payload
            invalid["writable_models"] = [true]
            XCTAssertThrowsError(try decodeCapability(invalid))
        }
    }

    func testCanonicalPromotionPreservesTimeButNotSyntheticCreator() throws {
        let (_, fixture) = try contract()
        let legacy = try dictionary(fixture, "legacy")
        let beforePayload = try dictionary(legacy, "tree_v2")
        let afterPayload = try dictionary(legacy, "tree_v3_promoted")
        let before = try decodeNode(beforePayload)
        let after = try decodeNode(afterPayload)
        let prepared = try SharedTabCreationProvenance.preparePromotion(before)
        XCTAssertEqual(prepared, after)
        XCTAssertTrue(SharedTabCreationProvenance.sameTime(before.createdAt, after.createdAt))
        XCTAssertFalse(after.creationProvenanceKnown)
        XCTAssertEqual(after.version.fieldVersions["created_at"], SharedTabContract.bottom)
        XCTAssertEqual(after.version.fieldVersions["is_temporary"], SharedTabContract.bottom)
        XCTAssertNil(before.version.fieldVersions["is_temporary"])
        XCTAssertNil(MobileSharedTabProjection(after)?.originDevice)
        let restored = try JSONDecoder().decode(TreeNode.self, from: JSONEncoder().encode(after))
        XCTAssertEqual(restored, after)
        XCTAssertThrowsError(try codec.encode(after), "Readiness does not activate a v3 writer.")

        var invalid = afterPayload
        invalid["is_temporary"] = true
        XCTAssertThrowsError(try decodeNode(invalid), "Bottom can encode only false/unlinked.")
    }

    func testLocalProvenanceEvidencePersistsWithoutChangingReplicatedEquality() throws {
        let (_, fixture) = try contract()
        let legacy = try dictionary(fixture, "legacy")
        let remote = try decodeNode(dictionary(legacy, "tree_v2"))
        var locallyObserved = remote
        locallyObserved.creationProvenanceClock = remote.version.fieldVersions["created_at"]
        XCTAssertEqual(locallyObserved, remote)
        XCTAssertEqual(Set([locallyObserved, remote]).count, 1)
        let restored = try JSONDecoder().decode(TreeNode.self, from: JSONEncoder().encode(locallyObserved))
        XCTAssertTrue(restored.creationProvenanceKnown)
        let promoted = try SharedTabCreationProvenance.preparePromotion(restored)
        XCTAssertTrue(promoted.creationProvenanceKnown)
        XCTAssertEqual(promoted.version.fieldVersions["created_at"], remote.version.fieldVersions["created_at"])
        XCTAssertEqual(try codec.encode(locallyObserved), try codec.encode(remote))
    }

    func testCanonicalMixedMergeMatchesBothDirectionsAndSurvivesRestart() throws {
        let (_, fixture) = try contract()
        let cases = try XCTUnwrap(fixture["merge_cases"] as? [[String: Any]])
        for entry in cases {
            let lhs = try decodeNode(dictionary(entry, "left"))
            let rhs = try decodeNode(dictionary(entry, "right"))
            let expected = try dictionary(entry, "expected")
            let merged = try CompanionFieldMerge.merge(lhs, rhs)
            XCTAssertEqual(merged, try CompanionFieldMerge.merge(rhs, lhs))
            XCTAssertEqual(merged.title, expected["title"] as? String)
            XCTAssertEqual(merged.isTemporary, expected["is_temporary"] as? Bool)
            XCTAssertEqual(merged.targetKind?.rawValue, expected["target_kind"] as? Int)
            XCTAssertEqual(merged.version.schemaVersion, 3)
            XCTAssertEqual(merged.version.modifiedAt.logicalCounter,
                           (expected["version_logical"] as? NSNumber)?.uint32Value)
            XCTAssertEqual(try codec.timeString(merged.version.modifiedAt), expected["version_physical"] as? String)
            XCTAssertEqual(merged.version.modifiedBy.rawValue.uuidString.lowercased(),
                           expected["version_device"] as? String)
            let clocks = try dictionary(expected, "field_versions")
            for (name, raw) in clocks {
                let clock = try XCTUnwrap(merged.version.fieldVersions[name])
                let wanted = try XCTUnwrap(raw as? [String: Any])
                XCTAssertEqual(try codec.timeString(clock), wanted["physical"] as? String)
                XCTAssertEqual(clock.nodeID.rawValue.uuidString.lowercased(), wanted["device"] as? String)
            }
            let restarted = try JSONDecoder().decode(TreeNode.self, from: JSONEncoder().encode(merged))
            XCTAssertEqual(try CompanionFieldMerge.merge(restarted, rhs), merged)
            XCTAssertThrowsError(try codec.encode(merged))
        }
    }

    func testCanonicalTargetMatrixAndLocalOnlyProjection() throws {
        let (_, fixture) = try contract()
        let cases = try XCTUnwrap(fixture["target_cases"] as? [[String: Any]])
        let legacy = try dictionary(fixture, "legacy")
        let basePage = try dictionary(legacy, "tree_v3_promoted")
        let presence = try dictionary(fixture, "presence")
        let basePresence = try dictionary(presence, "linked_v3")
        for entry in cases {
            let name = try XCTUnwrap(entry["name"] as? String)
            let isPage = entry["entity"] as? String == "page"
            var payload = isPage ? basePage : basePresence
            payload["target_kind"] = entry["target_kind"]
            payload["url"] = entry["url"]
            payload["local_scheme"] = entry["local_scheme"]
            if isPage {
                payload["is_temporary"] = entry["is_temporary"]
                var fields = try dictionary(payload, "field_versions")
                fields["is_temporary"] = fields["title"]
                payload["field_versions"] = fields
            } else {
                payload.removeValue(forKey: "tree_node_id")
            }
            if entry["valid"] as? Bool == false {
                if isPage { XCTAssertThrowsError(try decodeNode(payload), name) }
                else { XCTAssertThrowsError(try decodePresence(payload), name) }
                continue
            }
            let node = try decodeNode(payload)
            let row = try XCTUnwrap(MobileSharedTabProjection(node), name)
            XCTAssertEqual(row.id, node.id)
            if row.target.kind == .localOnly {
                XCTAssertFalse(row.canActivateHere)
                XCTAssertEqual(row.target.url, "")
                XCTAssertEqual(row.localRuntimeURL(preservingOwnedTarget: "file:///local/only"), "file:///local/only")
                XCTAssertNil(row.localRuntimeURL(preservingOwnedTarget: nil))
                XCTAssertNil(node.url)
            }
        }
    }

    func testPresenceNeedsMatchingKnownPageAndRejectsNullLink() throws {
        let (_, fixture) = try contract()
        let presence = try dictionary(fixture, "presence")
        var payload = try dictionary(presence, "linked_v3")
        payload["target_kind"] = 2
        payload["url"] = ""
        payload["local_scheme"] = "file"
        let value = try decodePresence(payload)
        XCTAssertThrowsError(try codec.validatePresenceTarget(value, pages: [:]))
        let legacy = try dictionary(fixture, "legacy")
        var pagePayload = try dictionary(legacy, "tree_v3_promoted")
        pagePayload["target_kind"] = 2
        pagePayload["url"] = ""
        pagePayload["local_scheme"] = "file"
        let page = try decodeNode(pagePayload)
        XCTAssertNoThrow(try codec.validatePresenceTarget(value, pages: [page.id: page]))
        payload["tree_node_id"] = NSNull()
        XCTAssertThrowsError(try decodePresence(payload))
    }

    func testFrozenReadinessMatrixNeverIgnoresOfflineOrNewPeers() throws {
        let (_, fixture) = try contract()
        let capabilities = try capabilityPayloads(fixture).map(decodeCapability)
        let byDevice = Dictionary(uniqueKeysWithValues: capabilities.map { ($0.deviceID, $0) })
        let cases = try XCTUnwrap(fixture["gate_cases"] as? [[String: Any]])
        for entry in cases {
            var peers: [SharedTabCapabilityReadiness.Peer] = []
            var declarations: [DeviceCapabilityRecord] = []
            for raw in try XCTUnwrap(entry["peers"] as? [[String: Any]]) {
                let device = DeviceID(rawValue: try SharedTabWireReadPolicy.strictUUID(raw, key: "id"))
                peers.append(.init(deviceID: device, explicitlyRetired: raw["retired"] as? Bool ?? false))
                if raw["capability"] as? String == "missing" { continue }
                var capability = try XCTUnwrap(byDevice[device])
                if raw["capability"] as? String == "reader_only" { capability.writableModels = [2] }
                declarations.append(capability)
            }
            let ready = SharedTabCapabilityReadiness.evaluate(
                localDevice: try XCTUnwrap(peers.first?.deviceID), peers: peers, capabilities: declarations,
                globalSyncEnabled: true, normalProfile: true,
                initialFetchComplete: entry["initial_fetch_complete"] as? Bool ?? false,
                localDeviceAcknowledged: entry["local_announcements_acknowledged"] as? Bool ?? false,
                localCapabilityAcknowledged: entry["local_announcements_acknowledged"] as? Bool ?? false
            )
            XCTAssertEqual(ready.isReady, entry["allowed"] as? Bool, entry["name"] as? String ?? "gate")
        }
        XCTAssertEqual(SharedTabWireReadPolicy.defaultWriteVersion, 2)
    }

    func testCapabilitySnapshotDecodeIsCompatibleAndCannotEnrollDevice() async throws {
        let (_, fixture) = try contract()
        let payloads = try capabilityPayloads(fixture)
        let capability = try decodeCapability(XCTUnwrap(payloads.first))
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let rejected = try await repository.mergeImportedBatch([.init(token: 1, value: .deviceCapability(capability))])
        guard case .some(.rejected) = rejected.first?.disposition else { return XCTFail("Capability cannot enroll its Device.") }
        _ = try await repository.upsert(device(capability.deviceID, version: capability.version))
        _ = try await repository.mergeImportedBatch([.init(token: 2, value: .deviceCapability(capability))])
        let snapshot = try await repository.currentSnapshot()
        let restored = try JSONDecoder().decode(CompanionSnapshot.self, from: JSONEncoder().encode(snapshot))
        XCTAssertEqual(restored.deviceCapabilities, [capability])
        var old = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(snapshot)) as? [String: Any])
        old.removeValue(forKey: "deviceCapabilities")
        let legacy = try JSONDecoder().decode(CompanionSnapshot.self, from: canonical(old))
        XCTAssertTrue(legacy.deviceCapabilities.isEmpty)
        XCTAssertEqual(legacy.devices, snapshot.devices)
    }

    private func contract() throws -> (Data, [String: Any]) {
        let url = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "shared_tab_wire_v3_contract", withExtension: "json"))
        let data = try Data(contentsOf: url)
        return (data, try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any]))
    }

    private func capabilityPayloads(_ fixture: [String: Any]) throws -> [[String: Any]] {
        try XCTUnwrap(dictionary(fixture, "capability")["records"] as? [[String: Any]])
    }

    private func dictionary(_ value: [String: Any], _ key: String) throws -> [String: Any] {
        try XCTUnwrap(value[key] as? [String: Any])
    }

    private func canonical(_ value: [String: Any]) throws -> Data {
        try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .withoutEscapingSlashes])
    }

    private func envelope(_ payload: [String: Any], _ dataClass: SyncDataClass) throws -> SyncRecord {
        let version = try codec.version(payload)
        let id = try SharedTabWireReadPolicy.strictUUID(payload, key: "id")
        return SyncRecord(recordID: id, entityID: id, schemaVersion: version.schemaVersion, dataClass: dataClass,
                          modifiedAt: version.modifiedAt, originatingDevice: version.modifiedBy,
                          encryptedValue: EncryptedValue(keyVersion: 1, nonce: Data(repeating: 0, count: 12),
                                                          ciphertextAndTag: Data(repeating: 0, count: 16)))
    }

    private func device(_ id: DeviceID, version: SyncVersion) -> Device {
        Device(deviceID: id, name: "Fixture", kind: .mac, lastSeenAt: version.modifiedAt, version: version)
    }

    private func decodeCapability(_ payload: [String: Any]) throws -> DeviceCapabilityRecord {
        let id = DeviceID(rawValue: try SharedTabWireReadPolicy.strictUUID(payload, key: "device_id"))
        return try codec.decodeCapability(envelope(payload, .deviceCapability), plaintext: canonical(payload),
                                          knownDevices: [id: device(id, version: codec.version(payload))])
    }

    private func decodeNode(_ payload: [String: Any]) throws -> TreeNode {
        try codec.decodeTreeNode(envelope(payload, .treeNode), plaintext: canonical(payload))
    }

    private func decodePresence(_ payload: [String: Any]) throws -> RemoteTab {
        let id = DeviceID(rawValue: try SharedTabWireReadPolicy.strictUUID(payload, key: "device_id"))
        return try codec.decodeRemoteTab(envelope(payload, .deviceTab), plaintext: canonical(payload),
                                         devices: [id: device(id, version: codec.version(payload))], workspaces: [:])
    }
}
