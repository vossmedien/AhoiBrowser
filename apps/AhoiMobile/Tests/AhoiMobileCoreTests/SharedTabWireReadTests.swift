import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class SharedTabWireReadTests: XCTestCase {
    func testLegacyTreeAndPresenceBytesAndFieldMapsRemainUnchanged() throws {
        let codec = DesktopWirePayloadCodec()
        for schemaVersion: UInt32 in [1, 2] {
            let node = try makeNode(schemaVersion: schemaVersion)
            let nodePayload = try codec.encode(node)
            let nodeObject = try codec.object(from: nodePayload)
            XCTAssertNil(nodeObject["is_temporary"])
            if schemaVersion == 1 {
                XCTAssertNil(nodeObject["field_versions"])
            } else {
                XCTAssertEqual(
                    Set(try XCTUnwrap(
                        nodeObject["field_versions"] as? [String: Any]
                    ).keys),
                    SharedTabWireReadPolicy.treeNodeBaseFields
                )
            }
            let decodedNode = try codec.decodeTreeNode(
                envelope(
                    id: node.id.rawValue,
                    dataClass: .treeNode,
                    version: node.version
                ),
                plaintext: nodePayload
            )
            XCTAssertFalse(decodedNode.isTemporary)
            XCTAssertNil(decodedNode.version.fieldVersions["is_temporary"])
            XCTAssertEqual(try codec.encode(decodedNode), nodePayload)

            let tab = try makeTab(schemaVersion: schemaVersion)
            let tabPayload = try codec.encode(tab)
            let tabObject = try codec.object(from: tabPayload)
            XCTAssertNil(tabObject["tree_node_id"])
            if schemaVersion == 1 {
                XCTAssertNil(tabObject["field_versions"])
            } else {
                XCTAssertEqual(
                    Set(try XCTUnwrap(
                        tabObject["field_versions"] as? [String: Any]
                    ).keys),
                    SharedTabWireReadPolicy.remoteTabBaseFields
                )
            }
            let decodedTab = try decodeTab(
                tabPayload,
                version: tab.version,
                codec: codec
            )
            XCTAssertNil(decodedTab.treeNodeID)
            XCTAssertNil(decodedTab.version.fieldVersions["tree_node_id"])
            XCTAssertEqual(try codec.encode(decodedTab), tabPayload)
        }
    }

    func testVersionThreeReadsLinkedPresenceAndTemporaryEmptyPage() throws {
        let codec = DesktopWirePayloadCodec()
        let node = try makeNode()
        var nodeObject = try upgradedV3Object(
            from: codec.encode(node),
            newFieldClock: "is_temporary",
            codec: codec
        )
        nodeObject["is_temporary"] = true
        nodeObject["url"] = ""
        let version = v3Version()
        let decodedNode = try codec.decodeTreeNode(
            envelope(
                id: node.id.rawValue,
                dataClass: .treeNode,
                version: version
            ),
            plaintext: try data(nodeObject)
        )
        XCTAssertTrue(decodedNode.isTemporary)
        XCTAssertNil(decodedNode.url)
        XCTAssertNotNil(decodedNode.version.fieldVersions["is_temporary"])

        let tab = try makeTab()
        let linkedID = node.id
        var tabObject = try upgradedV3Object(
            from: codec.encode(tab),
            newFieldClock: "tree_node_id",
            codec: codec
        )
        tabObject["tree_node_id"] = linkedID.rawValue.uuidString.lowercased()
        let decodedTab = try decodeTab(
            try data(tabObject),
            version: version,
            codec: codec
        )
        XCTAssertEqual(decodedTab.treeNodeID, linkedID)
        XCTAssertNotNil(decodedTab.version.fieldVersions["tree_node_id"])

        tabObject.removeValue(forKey: "tree_node_id")
        let unlinked = try decodeTab(
            try data(tabObject),
            version: version,
            codec: codec
        )
        XCTAssertNil(unlinked.treeNodeID)
        XCTAssertNotNil(unlinked.version.fieldVersions["tree_node_id"])
    }

    func testVersionThreeRejectsUnknownVersionsAndMalformedNewFields() throws {
        let codec = DesktopWirePayloadCodec()
        let node = try makeNode()
        let version = v3Version()
        var baseline = try upgradedV3Object(
            from: codec.encode(node),
            newFieldClock: "is_temporary",
            codec: codec
        )
        baseline["is_temporary"] = false
        let record = envelope(
            id: node.id.rawValue,
            dataClass: .treeNode,
            version: version
        )

        for invalidValue: Any in [1, "false", NSNull()] {
            var invalid = baseline
            invalid["is_temporary"] = invalidValue
            XCTAssertThrowsError(try codec.decodeTreeNode(
                record,
                plaintext: data(invalid)
            ))
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
        invalid["model_version"] = 4
        invalid["version_model"] = 4
        XCTAssertThrowsError(try codec.decodeTreeNode(
            envelope(
                id: node.id.rawValue,
                dataClass: .treeNode,
                version: SyncVersion(
                    schemaVersion: 4,
                    modifiedAt: version.modifiedAt,
                    modifiedBy: version.modifiedBy
                )
            ),
            plaintext: data(invalid)
        )) { error in
            XCTAssertEqual(
                error as? SharedTabWirePreparationError,
                .unsupportedVersion
            )
        }

        var folder = baseline
        folder["node_kind"] = 0
        folder["is_temporary"] = true
        folder["url"] = ""
        XCTAssertThrowsError(try codec.decodeTreeNode(record, plaintext: data(folder)))
        var emptyPersistent = baseline
        emptyPersistent["url"] = ""
        XCTAssertThrowsError(try codec.decodeTreeNode(
            record,
            plaintext: data(emptyPersistent)
        ))
    }

    func testVersionThreeRejectsMalformedPresenceLinkAndIncognito() throws {
        let codec = DesktopWirePayloadCodec()
        let tab = try makeTab()
        let version = v3Version()
        var baseline = try upgradedV3Object(
            from: codec.encode(tab),
            newFieldClock: "tree_node_id",
            codec: codec
        )
        baseline["tree_node_id"] = nodeID.uuidString.lowercased()

        for invalidValue: Any in [NSNull(), 42, "not-a-uuid", zeroUUID.uuidString] {
            var invalid = baseline
            invalid["tree_node_id"] = invalidValue
            XCTAssertThrowsError(try decodeTab(
                data(invalid),
                version: version,
                codec: codec
            ))
        }
        var collision = baseline
        collision["tree_node_id"] = tab.id.rawValue.uuidString.lowercased()
        XCTAssertThrowsError(try decodeTab(
            data(collision),
            version: version,
            codec: codec
        ))
        var missingClock = baseline
        removeClock("tree_node_id", from: &missingClock)
        XCTAssertThrowsError(try decodeTab(
            data(missingClock),
            version: version,
            codec: codec
        ))
        var extraClock = baseline
        addClock("unknown", to: &extraClock)
        XCTAssertThrowsError(try decodeTab(
            data(extraClock),
            version: version,
            codec: codec
        ))
        var futureClock = baseline
        advanceClock("tree_node_id", in: &futureClock)
        XCTAssertThrowsError(try decodeTab(
            data(futureClock),
            version: version,
            codec: codec
        ))
        var incognito = baseline
        incognito["is_incognito"] = true
        XCTAssertThrowsError(try decodeTab(
            data(incognito),
            version: version,
            codec: codec
        )) { error in
            XCTAssertEqual(
                error as? CompanionModelError,
                .incognitoNotSyncable
            )
        }
    }

    func testLegacyPayloadCannotSmuggleVersionThreeFieldsOrClocks() throws {
        let codec = DesktopWirePayloadCodec()
        for schemaVersion: UInt32 in [1, 2] {
            let node = try makeNode(schemaVersion: schemaVersion)
            let nodeRecord = envelope(
                id: node.id.rawValue,
                dataClass: .treeNode,
                version: node.version
            )
            var nodeObject = try codec.object(from: codec.encode(node))
            nodeObject["is_temporary"] = false
            XCTAssertThrowsError(try codec.decodeTreeNode(
                nodeRecord,
                plaintext: data(nodeObject)
            )) { error in
                XCTAssertEqual(
                    error as? SharedTabWirePreparationError,
                    .writerNotActivated
                )
            }
            nodeObject = try codec.object(from: codec.encode(node))
            addClock("is_temporary", to: &nodeObject)
            XCTAssertThrowsError(try codec.decodeTreeNode(
                nodeRecord,
                plaintext: data(nodeObject)
            ))

            let tab = try makeTab(schemaVersion: schemaVersion)
            var tabObject = try codec.object(from: codec.encode(tab))
            tabObject["tree_node_id"] = nodeID.uuidString.lowercased()
            XCTAssertThrowsError(try decodeTab(
                data(tabObject),
                version: tab.version,
                codec: codec
            ))
            tabObject = try codec.object(from: codec.encode(tab))
            addClock("tree_node_id", to: &tabObject)
            XCTAssertThrowsError(try decodeTab(
                data(tabObject),
                version: tab.version,
                codec: codec
            ))
        }
    }

    func testEveryVersionThreeOrIncompatibleLegacyWriteRemainsGated() throws {
        let codec = DesktopWirePayloadCodec()
        let v3 = v3Version()
        let v3Node = try makeNode(version: v3)
        XCTAssertThrowsError(try codec.encode(v3Node)) { error in
            XCTAssertEqual(
                error as? SharedTabWirePreparationError,
                .writerNotActivated
            )
        }
        let v3Tab = try makeTab(version: v3)
        XCTAssertThrowsError(try codec.encode(v3Tab)) { error in
            XCTAssertEqual(
                error as? SharedTabWirePreparationError,
                .writerNotActivated
            )
        }

        let temporaryV2 = try makeNode(isTemporary: true, url: nil)
        XCTAssertThrowsError(try codec.encode(temporaryV2)) { error in
            XCTAssertEqual(
                error as? SharedTabWirePreparationError,
                .writerNotActivated
            )
        }
        let linkedV2 = try makeTab(treeNodeID: TreeNodeID(rawValue: nodeID))
        XCTAssertThrowsError(try codec.encode(linkedV2)) { error in
            XCTAssertEqual(
                error as? SharedTabWirePreparationError,
                .writerNotActivated
            )
        }
    }

    func testVersionThreeEnvelopeIdentityClassSchemaAndClockMustMatch() throws {
        let codec = DesktopWirePayloadCodec()
        let node = try makeNode()
        let version = v3Version()
        var object = try upgradedV3Object(
            from: codec.encode(node),
            newFieldClock: "is_temporary",
            codec: codec
        )
        object["is_temporary"] = false
        let payload = try data(object)
        let wrongClock = clock(1_001, device: writerID)
        let records = [
            envelope(
                id: UUID(uuidString: "90000000-0000-4000-8000-000000000099")!,
                dataClass: .treeNode,
                version: version
            ),
            envelope(id: node.id.rawValue, dataClass: .deviceTab, version: version),
            SyncRecord(
                recordID: node.id.rawValue,
                entityID: node.id.rawValue,
                schemaVersion: 2,
                dataClass: .treeNode,
                modifiedAt: version.modifiedAt,
                originatingDevice: version.modifiedBy,
                encryptedValue: encryptedValue
            ),
            SyncRecord(
                recordID: node.id.rawValue,
                entityID: node.id.rawValue,
                schemaVersion: 3,
                dataClass: .treeNode,
                modifiedAt: wrongClock,
                originatingDevice: writerID,
                encryptedValue: encryptedValue
            ),
        ]
        for record in records {
            XCTAssertThrowsError(try codec.decodeTreeNode(
                record,
                plaintext: payload
            ))
        }
    }

    private func makeNode(
        schemaVersion: UInt32 = 2,
        version: SyncVersion? = nil,
        isTemporary: Bool = false,
        url: String? = "https://example.test/shared"
    ) throws -> TreeNode {
        let resolvedVersion = version ?? makeVersion(
            schemaVersion: schemaVersion,
            fields: schemaVersion == 1 ? [] : SharedTabWireReadPolicy.treeNodeBaseFields
        )
        return try TreeNode(
            treeNodeID: TreeNodeID(rawValue: nodeID),
            workspaceID: WorkspaceID(rawValue: workspaceID),
            kind: .savedPage,
            title: "Shared",
            url: url,
            orderKey: try OrderKey(components: [10, 20], tieBreaker: writerID),
            isTemporary: isTemporary,
            version: resolvedVersion
        )
    }

    private func makeTab(
        schemaVersion: UInt32 = 2,
        version: SyncVersion? = nil,
        treeNodeID: TreeNodeID? = nil
    ) throws -> RemoteTab {
        let resolvedVersion = version ?? makeVersion(
            schemaVersion: schemaVersion,
            fields: schemaVersion == 1 ? [] : SharedTabWireReadPolicy.remoteTabBaseFields
        )
        return try RemoteTab(
            tabID: TabID(rawValue: tabID),
            deviceID: writerID,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: DeviceSessionID(rawValue: sessionID),
            workspaceID: WorkspaceID(rawValue: workspaceID),
            treeNodeID: treeNodeID,
            workspaceName: "Inbox",
            title: "Shared tab",
            url: "https://example.test/tab",
            lastActiveAt: resolvedVersion.modifiedAt,
            version: resolvedVersion
        )
    }

    private func decodeTab(
        _ payload: Data,
        version: SyncVersion,
        codec: DesktopWirePayloadCodec
    ) throws -> RemoteTab {
        try codec.decodeRemoteTab(
            envelope(id: tabID, dataClass: .deviceTab, version: version),
            plaintext: payload,
            devices: [
                writerID: Device(
                    deviceID: writerID,
                    name: "Mac",
                    kind: .mac,
                    lastSeenAt: version.modifiedAt,
                    version: version
                ),
            ],
            workspaces: [
                WorkspaceID(rawValue: workspaceID): Workspace(
                    workspaceID: WorkspaceID(rawValue: workspaceID),
                    name: "Inbox",
                    version: version
                ),
            ]
        )
    }

    private func makeVersion(
        schemaVersion: UInt32,
        fields: Set<String>
    ) -> SyncVersion {
        let value = clock(1_000, device: writerID)
        return SyncVersion(
            schemaVersion: schemaVersion,
            modifiedAt: value,
            modifiedBy: writerID,
            fieldVersions: Dictionary(uniqueKeysWithValues:
                fields.map { ($0, value) }
            )
        )
    }

    private func v3Version() -> SyncVersion {
        SyncVersion(
            schemaVersion: 3,
            modifiedAt: clock(1_000, device: writerID),
            modifiedBy: writerID
        )
    }

    private func upgradedV3Object(
        from payload: Data,
        newFieldClock: String,
        codec: DesktopWirePayloadCodec
    ) throws -> [String: Any] {
        var value = try codec.object(from: payload)
        value["model_version"] = 3
        value["version_model"] = 3
        addClock(newFieldClock, to: &value)
        return value
    }

    private func addClock(_ name: String, to value: inout [String: Any]) {
        var fields = value["field_versions"] as? [String: Any] ?? [:]
        fields[name] = fields["title"] ?? [
            "physical": value["version_physical"]!,
            "logical": value["version_logical"]!,
            "device": value["version_device"]!,
        ]
        value["field_versions"] = fields
    }

    private func removeClock(_ name: String, from value: inout [String: Any]) {
        var fields = value["field_versions"] as! [String: Any]
        fields.removeValue(forKey: name)
        value["field_versions"] = fields
    }

    private func advanceClock(_ name: String, in value: inout [String: Any]) {
        var fields = value["field_versions"] as! [String: Any]
        var field = fields[name] as! [String: Any]
        field["physical"] = "11644473601001000"
        fields[name] = field
        value["field_versions"] = fields
    }

    private func data(_ value: [String: Any]) throws -> Data {
        try JSONSerialization.data(
            withJSONObject: value,
            options: [.sortedKeys, .withoutEscapingSlashes]
        )
    }

    private func envelope(
        id: UUID,
        dataClass: SyncDataClass,
        version: SyncVersion
    ) -> SyncRecord {
        SyncRecord(
            recordID: id,
            entityID: id,
            schemaVersion: version.schemaVersion,
            dataClass: dataClass,
            modifiedAt: version.modifiedAt,
            originatingDevice: version.modifiedBy,
            encryptedValue: encryptedValue
        )
    }

    private var encryptedValue: EncryptedValue {
        EncryptedValue(
            keyVersion: 1,
            nonce: Data(repeating: 0, count: 12),
            ciphertextAndTag: Data(repeating: 0, count: 16)
        )
    }

    private func clock(
        _ milliseconds: UInt64,
        device: DeviceID
    ) -> HybridLogicalClock {
        HybridLogicalClock(
            physicalMilliseconds: milliseconds,
            logicalCounter: 2,
            nodeID: device
        )
    }

    private var writerID: DeviceID {
        DeviceID(rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000002")!)
    }
    private var nodeID: UUID {
        UUID(uuidString: "20000000-0000-4000-8000-000000000002")!
    }
    private var tabID: UUID {
        UUID(uuidString: "30000000-0000-4000-8000-000000000003")!
    }
    private var sessionID: UUID {
        UUID(uuidString: "40000000-0000-4000-8000-000000000004")!
    }
    private var workspaceID: UUID {
        UUID(uuidString: "83699047-edf8-580d-948d-9c37acc35cb6")!
    }
    private var zeroUUID: UUID {
        UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    }
}
