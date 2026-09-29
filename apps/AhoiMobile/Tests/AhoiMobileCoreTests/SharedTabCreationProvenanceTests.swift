import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Format 3 has one immutable creation register. A later editor must not
/// become the creator, and no local side channel may manufacture an origin.
final class SharedTabCreationProvenanceTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()

    func testReframeUsesCreationRegisterWithoutChangingItsTimestampOrWire() throws {
        let node = try savedWebPage()
        let creator = try XCTUnwrap(node.version.fieldVersions["created_at"])
        let reframed = try SharedTabCreationProvenance.reframe(node)

        XCTAssertTrue(SharedTabCreationProvenance.sameTime(reframed.createdAt, node.createdAt))
        XCTAssertEqual(reframed.createdAt.nodeID, creator.nodeID)
        XCTAssertEqual(reframed.createdAt.logicalCounter, creator.logicalCounter)
        XCTAssertEqual(reframed.creationProvenanceClock, creator)
        XCTAssertEqual(MobileSharedTabProjection(reframed)?.originDevice, creator.nodeID)
        XCTAssertEqual(try codec.encode(reframed), try codec.encode(node))
    }

    func testLaterEditMergeAndRestartKeepOriginalCreationRegister() throws {
        let original = try savedWebPage()
        let creator = try XCTUnwrap(original.version.fieldVersions["created_at"])
        let saved = try XCTUnwrap(original.version.fieldVersions["is_temporary"])
        var edited = original
        edited.title = "Later editor"
        let later = try original.version.modifiedAt.ticking(at: 5_000)
        edited.version = SyncVersion(modifiedAt: later, modifiedBy: later.nodeID)
        edited = CompanionFieldMerge.stampLocal(previous: original, candidate: edited)

        for (old, new) in [(original, edited), (edited, original)] {
            let merged = try CompanionFieldMerge.merge(old, new)
            XCTAssertEqual(merged.version.fieldVersions["created_at"], creator)
            XCTAssertEqual(merged.version.fieldVersions["is_temporary"], saved)
            XCTAssertEqual(merged.createdAt, original.createdAt)
            XCTAssertEqual(MobileSharedTabProjection(merged)?.originDevice, creator.nodeID)
            let restored = try JSONDecoder().decode(TreeNode.self, from: JSONEncoder().encode(merged))
            XCTAssertEqual(restored.creationProvenanceClock, creator)
            XCTAssertEqual(try CompanionFieldMerge.merge(restored, original), merged)
            XCTAssertEqual(try codec.encode(restored), try codec.encode(merged))
        }
    }

    func testSystemBottomAndMissingRegisterNeverInventCreator() throws {
        var node = try savedWebPage()
        var fields = node.version.fieldVersions
        fields["created_at"] = SharedTabContract.bottom
        node.version = SyncVersion(schemaVersion: SharedSyncFormat.currentVersion,
                                   modifiedAt: node.version.modifiedAt,
                                   modifiedBy: node.version.modifiedBy,
                                   fieldVersions: fields)
        XCTAssertNil(node.creationProvenanceClock)
        XCTAssertNil(MobileSharedTabProjection(node)?.originDevice)
        let reframed = try SharedTabCreationProvenance.reframe(node)
        XCTAssertTrue(SharedTabCreationProvenance.sameTime(reframed.createdAt, node.createdAt))
        XCTAssertEqual(reframed.createdAt.nodeID, SharedTabContract.systemActor)

        fields.removeValue(forKey: "created_at")
        node.version = SyncVersion(schemaVersion: SharedSyncFormat.currentVersion,
                                   modifiedAt: node.version.modifiedAt,
                                   modifiedBy: node.version.modifiedBy,
                                   fieldVersions: fields)
        XCTAssertNil(node.creationProvenanceClock)
        XCTAssertThrowsError(try SharedTabCreationProvenance.reframe(node))
        XCTAssertThrowsError(try codec.encode(node))
    }

    private func savedWebPage() throws -> TreeNode {
        let (_, fixture) = try UnifiedSyncFixture.load()
        let sample = try XCTUnwrap(fixture.records.first { $0.name == "tree_saved_web" })
        return try codec.decodeTreeNode(UnifiedSyncFixture.envelope(sample), plaintext: sample.data)
    }
}
