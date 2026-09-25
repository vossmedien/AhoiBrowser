import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Format-3 field merge of shared-tab state. There is no mixed v2/v3 matrix
/// (ADR 0009); a later edit of one field group never reverts another.
final class SharedTabFieldReadMergeTests: XCTestCase {
    func testLaterUnrelatedEditCannotClearTemporaryStatusInEitherMergeDirection() throws {
        let old = try node(version: version(at: 100, fields: CompanionFieldMerge.treeNodeFields))
        var shared = old
        shared.isTemporary = true
        shared.version = version(at: 200, fields: CompanionFieldMerge.treeNodeFields,
                                 base: old.version, changed: ["is_temporary"])
        var laterEdit = old
        laterEdit.title = "Later title"
        laterEdit.version = version(at: 300, fields: CompanionFieldMerge.treeNodeFields,
                                    base: old.version, changed: ["title"])
        let first = try CompanionFieldMerge.merge(shared, laterEdit)
        let reversed = try CompanionFieldMerge.merge(laterEdit, shared)
        XCTAssertEqual(first, reversed)
        XCTAssertTrue(first.isTemporary)
        XCTAssertEqual(first.title, laterEdit.title)
        XCTAssertEqual(first.version.schemaVersion, 3)
        XCTAssertEqual(first.version.fieldVersions["is_temporary"], shared.version.fieldVersions["is_temporary"])
        XCTAssertEqual(first.version.fieldVersions["title"], laterEdit.version.fieldVersions["title"])
        XCTAssertNoThrow(try DesktopWirePayloadCodec().encode(first))
    }

    func testLaterUnrelatedEditCannotRevertNewerPageBinding() throws {
        let fields = SharedTabWireReadPolicy.remoteTabBaseFields
        let device = writer
        let original = try RemoteTab(tabID: TabID(), deviceID: device, deviceKind: .mac,
                                     deviceName: "Mac", sessionID: DeviceSessionID(),
                                     treeNodeID: TreeNodeID(),
                                     title: "Original", url: "https://example.test", targetKind: .web,
                                     lastActiveAt: HybridLogicalClock(physicalMilliseconds: 100, nodeID: device),
                                     version: version(at: 100, fields: fields))
        let relinkedPage = TreeNodeID()
        var shared = original
        shared.treeNodeID = relinkedPage
        shared.version = version(at: 200, fields: fields, base: original.version, changed: ["tree_node_id"])
        var laterEdit = original
        laterEdit.title = "New title"
        laterEdit.version = version(at: 300, fields: fields, base: original.version, changed: ["title"])
        let merged = try CompanionReadModelFieldMerge.merge(shared, laterEdit)
        let reversed = try CompanionReadModelFieldMerge.merge(laterEdit, shared)
        XCTAssertEqual(merged, reversed)
        XCTAssertEqual(merged.treeNodeID, relinkedPage)
        XCTAssertEqual(merged.title, "New title")
        XCTAssertEqual(merged.version.fieldVersions["tree_node_id"], shared.version.fieldVersions["tree_node_id"])
        XCTAssertEqual(merged.version.schemaVersion, 3)
        let persisted = try JSONDecoder().decode(RemoteTab.self, from: JSONEncoder().encode(merged))
        XCTAssertEqual(persisted, merged)
    }

    func testLocalMutationStampsOnlyChangedFieldsAndPreservesExistingClocks() throws {
        var old = try node(version: version(at: 100, fields: CompanionFieldMerge.treeNodeFields))
        old.isTemporary = true
        var candidate = old
        candidate.title = "Edited locally"
        let editClock = HybridLogicalClock(physicalMilliseconds: 200, nodeID: writer)
        candidate.version = SyncVersion(modifiedAt: editClock, modifiedBy: writer)
        let stamped = CompanionFieldMerge.stampLocal(previous: old, candidate: candidate)
        XCTAssertTrue(stamped.isTemporary)
        XCTAssertEqual(stamped.version.schemaVersion, 3)
        XCTAssertEqual(stamped.version.fieldVersions["is_temporary"], old.version.fieldVersions["is_temporary"])
        XCTAssertEqual(stamped.version.fieldVersions["title"], editClock)
        XCTAssertEqual(Set(stamped.version.fieldVersions.keys), CompanionFieldMerge.treeNodeFields)
        XCTAssertNoThrow(try DesktopWirePayloadCodec().encode(stamped))
    }

    func testSelfMergeIsStableAndInboxIsDeterministic() throws {
        let value = try node(version: version(at: 100, fields: CompanionFieldMerge.treeNodeFields))
        let merged = try CompanionFieldMerge.merge(value, value)
        XCTAssertEqual(merged, value)
        XCTAssertFalse(merged.isTemporary)
        let inbox = MobileSharedTabIdentity.inbox
        XCTAssertEqual(inbox.id.rawValue.uuidString.lowercased(), "83699047-edf8-580d-948d-9c37acc35cb6")
        XCTAssertEqual(inbox.name, "Inbox")
        XCTAssertEqual(inbox.sortKey, "0")
        XCTAssertEqual(inbox.createdAt.physicalMilliseconds, 0)
        XCTAssertEqual(inbox.version.modifiedBy, MobileSharedTabIdentity.systemActor)
        XCTAssertEqual(SyncVersion(modifiedAt: inbox.createdAt, modifiedBy: inbox.version.modifiedBy).schemaVersion,
                       SharedSyncFormat.currentVersion)
    }

    private let writer = DeviceID(rawValue: UUID(uuidString: "20000000-0000-4000-8000-000000000001")!)

    /// A complete Format-3 field map: unchanged fields keep `base`, `changed`
    /// fields (or all fields without a base) carry the new clock.
    private func version(
        at time: UInt64, fields: Set<String>, base: SyncVersion? = nil, changed: Set<String> = []
    ) -> SyncVersion {
        let clock = HybridLogicalClock(physicalMilliseconds: time, nodeID: writer)
        var map: [String: HybridLogicalClock] = [:]
        for field in fields {
            map[field] = base.flatMap { changed.contains(field) ? nil : $0.fieldVersions[field] } ?? clock
        }
        return SyncVersion(modifiedAt: clock, modifiedBy: writer, fieldVersions: map)
    }

    private func node(version: SyncVersion) throws -> TreeNode {
        let created = HybridLogicalClock(physicalMilliseconds: 1, nodeID: writer)
        return try TreeNode(treeNodeID: TreeNodeID(), workspaceID: WorkspaceID(), kind: .savedPage,
                            title: "Page", url: "https://example.test",
                            orderKey: OrderKey.between(nil, nil, tieBreaker: writer),
                            targetKind: .web, createdAt: created, version: version)
    }
}
