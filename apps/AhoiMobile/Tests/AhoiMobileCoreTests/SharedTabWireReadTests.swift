import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Shared-tab read/write checks against the one Format-3 golden resource
/// (ADR 0009). Old v1/v2 bytes are rejected, never upgraded or re-emitted.
final class SharedTabWireReadTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()

    func testLegacyTreeAndPresenceVersionsAreRejectedEvenWithVersionThreeFields() throws {
        let (node, nodeSample) = try goldenNode("tree_saved_web")
        let (tab, tabSample) = try goldenTab("presence_saved_web")
        for legacy: UInt32 in [1, 2] {
            var nodeObject = try UnifiedSyncFixture.object(nodeSample.data)
            relabel(&nodeObject, version: legacy)
            XCTAssertThrowsError(try codec.decodeTreeNode(
                envelope(nodeSample, schemaVersion: legacy), plaintext: data(nodeObject)
            )) { error in
                XCTAssertEqual(error as? SharedTabWirePreparationError, .unsupportedVersion)
            }
            nodeObject.removeValue(forKey: "is_temporary")
            removeClock("is_temporary", from: &nodeObject)
            XCTAssertThrowsError(try codec.decodeTreeNode(
                envelope(nodeSample, schemaVersion: legacy), plaintext: data(nodeObject)
            ))

            var tabObject = try UnifiedSyncFixture.object(tabSample.data)
            relabel(&tabObject, version: legacy)
            XCTAssertThrowsError(try decodeTab(data(tabObject), sample: tabSample, schemaVersion: legacy))
            tabObject.removeValue(forKey: "tree_node_id")
            removeClock("tree_node_id", from: &tabObject)
            XCTAssertThrowsError(try decodeTab(data(tabObject), sample: tabSample, schemaVersion: legacy))

            var legacyNode = node
            legacyNode.version = relabeled(node.version, legacy)
            XCTAssertThrowsError(try codec.encode(legacyNode))
            var legacyTab = tab
            legacyTab.version = relabeled(tab.version, legacy)
            XCTAssertThrowsError(try codec.encode(legacyTab))
        }
    }

    func testVersionThreeReadsLinkedPresenceAndTemporaryEmptyPage() throws {
        let (temporary, temporarySample) = try goldenNode("tree_temporary_new_tab")
        XCTAssertTrue(temporary.isTemporary)
        XCTAssertNil(temporary.url)
        XCTAssertEqual(temporary.targetKind, .newTab)
        XCTAssertNotNil(temporary.version.fieldVersions["is_temporary"])
        XCTAssertEqual(try codec.encode(temporary), temporarySample.data)

        let (page, _) = try goldenNode("tree_saved_web")
        let (tab, tabSample) = try goldenTab("presence_saved_web")
        XCTAssertEqual(tab.treeNodeID, page.id)
        XCTAssertNotNil(tab.version.fieldVersions["tree_node_id"])
        XCTAssertNoThrow(try codec.validatePresenceTarget(tab, pages: [page.id: page]))
        XCTAssertThrowsError(try codec.validatePresenceTarget(tab, pages: [:]))
        XCTAssertEqual(try codec.encode(tab), tabSample.data)

        // A published Presence must link its actual page (ADR 0009).
        var unlinked = try UnifiedSyncFixture.object(tabSample.data)
        unlinked.removeValue(forKey: "tree_node_id")
        XCTAssertThrowsError(try decodeTab(data(unlinked), sample: tabSample))
    }

    func testVersionThreeRejectsUnknownVersionsAndMalformedNewFields() throws {
        let (_, sample) = try goldenNode("tree_saved_web")
        let baseline = try UnifiedSyncFixture.object(sample.data)
        let record = try UnifiedSyncFixture.envelope(sample)
        XCTAssertNoThrow(try codec.decodeTreeNode(record, plaintext: data(baseline)))

        for invalidValue: Any in [1, "false", NSNull()] {
            var invalid = baseline
            invalid["is_temporary"] = invalidValue
            XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(invalid)))
        }
        var invalid = baseline
        invalid.removeValue(forKey: "is_temporary")
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(invalid)))
        invalid = baseline
        removeClock("is_temporary", from: &invalid)
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(invalid)))
        invalid = baseline
        addClock("future_field", to: &invalid)
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(invalid)))
        invalid = baseline
        advanceClock("is_temporary", in: &invalid)
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(invalid)))

        invalid = baseline
        relabel(&invalid, version: 4)
        XCTAssertThrowsError(try codec.decodeTreeNode(
            envelope(sample, schemaVersion: 4), plaintext: data(invalid)
        )) { error in
            XCTAssertEqual(error as? SharedTabWirePreparationError, .unsupportedVersion)
        }

        var folder = baseline
        folder["node_kind"] = 0
        folder["is_temporary"] = true
        folder["url"] = ""
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(folder)))
        var emptyPersistent = baseline
        emptyPersistent["url"] = ""
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(emptyPersistent)))
    }

    func testVersionThreeRejectsMalformedPresenceLinkAndIncognito() throws {
        let (tab, sample) = try goldenTab("presence_saved_web")
        let baseline = try UnifiedSyncFixture.object(sample.data)

        for invalidValue: Any in [NSNull(), 42, "not-a-uuid", zeroUUID.uuidString.lowercased(),
                                  tab.treeNodeID!.rawValue.uuidString] {
            var invalid = baseline
            invalid["tree_node_id"] = invalidValue
            XCTAssertThrowsError(try decodeTab(data(invalid), sample: sample), "\(invalidValue)")
        }
        var collision = baseline
        collision["tree_node_id"] = tab.id.rawValue.uuidString.lowercased()
        XCTAssertThrowsError(try decodeTab(data(collision), sample: sample))
        var missingClock = baseline
        removeClock("tree_node_id", from: &missingClock)
        XCTAssertThrowsError(try decodeTab(data(missingClock), sample: sample))
        var extraClock = baseline
        addClock("unknown", to: &extraClock)
        XCTAssertThrowsError(try decodeTab(data(extraClock), sample: sample))
        var futureClock = baseline
        advanceClock("tree_node_id", in: &futureClock)
        XCTAssertThrowsError(try decodeTab(data(futureClock), sample: sample))
        var incognito = baseline
        incognito["is_incognito"] = true
        XCTAssertThrowsError(try decodeTab(data(incognito), sample: sample)) { error in
            XCTAssertEqual(error as? CompanionModelError, .incognitoNotSyncable)
        }
    }

    func testVersionThreeWriterIsActiveButRejectsUnlinkedOrIncompleteValues() throws {
        XCTAssertEqual(SharedTabWireReadPolicy.defaultWriteVersion, 3)
        let (node, nodeSample) = try goldenNode("tree_saved_web")
        let (tab, tabSample) = try goldenTab("presence_saved_web")
        XCTAssertEqual(try codec.encode(node), nodeSample.data)
        XCTAssertEqual(try codec.encode(tab), tabSample.data)

        var unlinked = tab
        unlinked.treeNodeID = nil
        XCTAssertThrowsError(try codec.encode(unlinked)) { error in
            XCTAssertEqual(error as? SharedTabTargetError, .missingPageLink)
        }
        var selfLinked = tab
        selfLinked.treeNodeID = TreeNodeID(rawValue: tab.id.rawValue)
        XCTAssertThrowsError(try codec.encode(selfLinked))

        var partialNode = node
        partialNode.version.fieldVersions.removeValue(forKey: "is_temporary")
        XCTAssertThrowsError(try codec.encode(partialNode))
        var partialTab = tab
        partialTab.version.fieldVersions.removeValue(forKey: "tree_node_id")
        XCTAssertThrowsError(try codec.encode(partialTab))

        XCTAssertThrowsError(try RemoteTab(
            tabID: tab.tabID, deviceID: tab.deviceID, deviceKind: tab.deviceKind,
            deviceName: tab.deviceName, sessionID: tab.sessionID, workspaceID: tab.workspaceID,
            title: tab.title, url: tab.url, targetKind: .web,
            lastActiveAt: tab.lastActiveAt, version: tab.version
        )) { error in
            XCTAssertEqual(error as? SharedTabTargetError, .missingPageLink)
        }
    }

    func testVersionThreeEnvelopeIdentityClassSchemaAndClockMustMatch() throws {
        let (node, sample) = try goldenNode("tree_saved_web")
        let base = try UnifiedSyncFixture.envelope(sample)
        let wrongClock = try node.version.modifiedAt.ticking(at: node.version.modifiedAt.physicalMilliseconds + 1)
        let records = [
            SyncRecord(recordID: UUID(uuidString: "90000000-0000-4000-8000-000000000099")!,
                       entityID: UUID(uuidString: "90000000-0000-4000-8000-000000000099")!,
                       schemaVersion: 3, dataClass: .treeNode, modifiedAt: base.modifiedAt,
                       originatingDevice: base.originatingDevice, encryptedValue: base.encryptedValue),
            SyncRecord(recordID: base.recordID, entityID: base.entityID, schemaVersion: 3,
                       dataClass: .deviceTab, modifiedAt: base.modifiedAt,
                       originatingDevice: base.originatingDevice, encryptedValue: base.encryptedValue),
            try envelope(sample, schemaVersion: 2),
            SyncRecord(recordID: base.recordID, entityID: base.entityID, schemaVersion: 3,
                       dataClass: .treeNode, modifiedAt: wrongClock,
                       originatingDevice: wrongClock.nodeID, encryptedValue: base.encryptedValue),
        ]
        XCTAssertNoThrow(try codec.decodeTreeNode(base, plaintext: sample.data))
        for record in records {
            XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: sample.data))
        }
    }

    // MARK: - Golden helpers

    private func sample(_ name: String) throws -> UnifiedSyncFixture.Sample {
        let (_, fixture) = try UnifiedSyncFixture.load()
        return try XCTUnwrap(fixture.records.first { $0.name == name }, name)
    }

    private func goldenNode(_ name: String) throws -> (TreeNode, UnifiedSyncFixture.Sample) {
        let value = try sample(name)
        return (try codec.decodeTreeNode(UnifiedSyncFixture.envelope(value), plaintext: value.data), value)
    }

    private func goldenTab(_ name: String) throws -> (RemoteTab, UnifiedSyncFixture.Sample) {
        let value = try sample(name)
        return (try decodeTab(value.data, sample: value), value)
    }

    private func decodeTab(
        _ payload: Data, sample: UnifiedSyncFixture.Sample, schemaVersion: UInt32 = 3
    ) throws -> RemoteTab {
        let deviceSample = try self.sample("device_mac")
        let device = try codec.decodeDevice(UnifiedSyncFixture.envelope(deviceSample), plaintext: deviceSample.data)
        let workspaceSample = try self.sample("workspace")
        let workspace = try codec.decodeWorkspace(UnifiedSyncFixture.envelope(workspaceSample),
                                                  plaintext: workspaceSample.data)
        return try codec.decodeRemoteTab(
            envelope(sample, schemaVersion: schemaVersion), plaintext: payload,
            devices: [device.id: device], workspaces: [workspace.id: workspace]
        )
    }

    private func envelope(_ sample: UnifiedSyncFixture.Sample, schemaVersion: UInt32) throws -> SyncRecord {
        let base = try UnifiedSyncFixture.envelope(sample)
        return SyncRecord(recordID: base.recordID, entityID: base.entityID, schemaVersion: schemaVersion,
                          dataClass: base.dataClass, modifiedAt: base.modifiedAt,
                          originatingDevice: base.originatingDevice, encryptedValue: base.encryptedValue)
    }

    private func relabeled(_ version: SyncVersion, _ schemaVersion: UInt32) -> SyncVersion {
        SyncVersion(schemaVersion: schemaVersion, modifiedAt: version.modifiedAt,
                    modifiedBy: version.modifiedBy, fieldVersions: version.fieldVersions)
    }

    private func relabel(_ value: inout [String: Any], version: UInt32) {
        value["model_version"] = version
        value["version_model"] = version
    }

    private func addClock(_ name: String, to value: inout [String: Any]) {
        var fields = value["field_versions"] as? [String: Any] ?? [:]
        fields[name] = fields["title"]
        value["field_versions"] = fields
    }

    private func removeClock(_ name: String, from value: inout [String: Any]) {
        var fields = value["field_versions"] as! [String: Any]
        fields.removeValue(forKey: name)
        value["field_versions"] = fields
    }

    /// Moves one field clock past the enclosing record clock.
    private func advanceClock(_ name: String, in value: inout [String: Any]) {
        var fields = value["field_versions"] as! [String: Any]
        var field = fields[name] as! [String: Any]
        field["physical"] = "11644473609000000"
        fields[name] = field
        value["field_versions"] = fields
    }

    private func data(_ value: [String: Any]) throws -> Data {
        try UnifiedSyncFixture.canonical(value)
    }

    private var zeroUUID: UUID {
        UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    }
}
