import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class SharedTabFieldReadMergeTests: XCTestCase {
    func testLaterLegacyUpdateCannotClearTemporaryStatusInEitherMergeDirection() throws {
        let old = try node(version: version(2, at: 100))
        var shared = old
        shared.isTemporary = true
        shared.targetKind = .web
        shared.version = version(3, at: 200, field: "is_temporary")
        var laterLegacy = old
        laterLegacy.title = "Later legacy title"
        laterLegacy.version = version(2, at: 300)
        let first = try CompanionFieldMerge.merge(shared, laterLegacy)
        let reversed = try CompanionFieldMerge.merge(laterLegacy, shared)
        XCTAssertEqual(first, reversed)
        XCTAssertTrue(first.isTemporary)
        XCTAssertEqual(first.title, laterLegacy.title)
        XCTAssertEqual(first.version.schemaVersion, 3)
        XCTAssertEqual(first.version.fieldVersions["is_temporary"], shared.version.fieldVersions["is_temporary"])
        XCTAssertThrowsError(try DesktopWirePayloadCodec().encode(first))
    }

    func testLaterLegacyUpdateCannotRemoveBindingOrAuthorAnUnlinkedWrite() throws {
        let nodeID = TreeNodeID()
        let device = DeviceID()
        let clock = HybridLogicalClock(physicalMilliseconds: 100, nodeID: device)
        let original = try RemoteTab(tabID: TabID(), deviceID: device, deviceKind: .mac,
                                     deviceName: "Mac", sessionID: DeviceSessionID(),
                                     title: "Original", url: "https://example.test",
                                     lastActiveAt: clock, version: version(2, at: 100))
        var shared = original
        shared.treeNodeID = nodeID
        shared.targetKind = .web
        shared.version = version(3, at: 200, field: "tree_node_id")
        var legacy = original
        legacy.title = "New title"
        legacy.version = version(2, at: 300)
        let merged = try CompanionReadModelFieldMerge.merge(shared, legacy)
        let reversed = try CompanionReadModelFieldMerge.merge(legacy, shared)
        XCTAssertEqual(merged, reversed)
        XCTAssertEqual(merged.treeNodeID, nodeID)
        XCTAssertEqual(merged.version.fieldVersions["tree_node_id"], shared.version.fieldVersions["tree_node_id"])
        XCTAssertNil(legacy.version.fieldVersions["tree_node_id"])
        XCTAssertEqual(merged.version.schemaVersion, 3)
        let persisted = try JSONDecoder().decode(RemoteTab.self, from: JSONEncoder().encode(merged))
        XCTAssertEqual(persisted, merged)
    }

    func testLegacyLocalMutationPreservesExistingVersionedField() throws {
        var old = try node(version: version(3, at: 100, field: "is_temporary"))
        old.isTemporary = true
        var candidate = old
        candidate.title = "Edited locally"
        candidate.isTemporary = false
        candidate.version = version(2, at: 200)
        let stamped = CompanionFieldMerge.stampLocal(previous: old, candidate: candidate)
        XCTAssertTrue(stamped.isTemporary)
        XCTAssertEqual(stamped.version.schemaVersion, 3)
        XCTAssertEqual(stamped.version.fieldVersions["is_temporary"], old.version.fieldVersions["is_temporary"])
        XCTAssertThrowsError(try DesktopWirePayloadCodec().encode(stamped))
    }

    func testLegacyNewFieldsRemainUnstampedAndInboxIsDeterministic() throws {
        let legacy = try node(version: version(1, at: 100))
        let merged = try CompanionFieldMerge.merge(legacy, legacy)
        XCTAssertFalse(merged.isTemporary)
        XCTAssertNil(merged.version.fieldVersions["is_temporary"])
        let inbox = MobileSharedTabIdentity.inbox
        XCTAssertEqual(inbox.id.rawValue.uuidString.lowercased(), "83699047-edf8-580d-948d-9c37acc35cb6")
        XCTAssertEqual(inbox.name, "Inbox")
        XCTAssertEqual(inbox.sortKey, "0")
        XCTAssertEqual(inbox.createdAt.physicalMilliseconds, 0)
        XCTAssertEqual(inbox.version.modifiedBy, MobileSharedTabIdentity.systemActor)
        XCTAssertEqual(SyncVersion(modifiedAt: inbox.createdAt, modifiedBy: inbox.version.modifiedBy).schemaVersion, 2)
    }

    private let writer = DeviceID(rawValue: UUID(uuidString: "20000000-0000-4000-8000-000000000001")!)

    private func version(_ schema: UInt32, at time: UInt64, field: String? = nil) -> SyncVersion {
        let clock = HybridLogicalClock(physicalMilliseconds: time, nodeID: writer)
        var fields = field.map { [$0: clock] } ?? [:]
        if schema == 3, field == "is_temporary" { fields["created_at"] = SharedTabContract.bottom }
        return SyncVersion(schemaVersion: schema, modifiedAt: clock, modifiedBy: writer,
                           fieldVersions: fields)
    }

    private func node(version: SyncVersion) throws -> TreeNode {
        let created = HybridLogicalClock(physicalMilliseconds: 1, nodeID: writer)
        return try TreeNode(treeNodeID: TreeNodeID(), workspaceID: WorkspaceID(), kind: .savedPage,
                            title: "Page", url: "https://example.test",
                            orderKey: OrderKey.between(nil, nil, tieBreaker: writer),
                            targetKind: version.schemaVersion == 3 ? .web : nil,
                            createdAt: created, version: version)
    }
}
