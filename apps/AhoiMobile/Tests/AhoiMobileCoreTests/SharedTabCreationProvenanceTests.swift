import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class SharedTabCreationProvenanceTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()
    private let creator = DeviceID(rawValue: UUID(uuidString: "90000000-0000-4000-8000-0000000000a0")!)
    private let editor = DeviceID(rawValue: UUID(uuidString: "90000000-0000-4000-8000-0000000000b0")!)

    func testNewerLegacyCreationClockPreservesOriginalEvidenceInBothMergeDirections() throws {
        let original = try localNode()
        let rewrite = try legacyRewrite(original, milliseconds: 5_000)
        for (old, new) in [(original, rewrite), (rewrite, original)] {
            let merged = try CompanionFieldMerge.merge(old, new)
            XCTAssertEqual(merged, rewrite, "Local evidence does not change replicated equality.")
            XCTAssertEqual(Set([merged, rewrite]).count, 1)
            XCTAssertEqual(merged.version, rewrite.version, "No synthetic merge-clock bump.")
            XCTAssertEqual(merged.version.fieldVersions["created_at"], rewrite.version.fieldVersions["created_at"])
            XCTAssertEqual(merged.creationProvenanceClock, original.creationProvenanceClock)
            XCTAssertEqual(MobileSharedTabProjection(merged)?.originDevice, creator)
            XCTAssertEqual(try codec.encode(merged), try codec.encode(rewrite))
            XCTAssertFalse(String(decoding: try codec.encode(merged), as: UTF8.self).contains("creationProvenance"))

            let restarted = try JSONDecoder().decode(TreeNode.self, from: JSONEncoder().encode(merged))
            let later = try legacyRewrite(restarted, milliseconds: 7_000)
            let mergedAgain = try CompanionFieldMerge.merge(restarted, later)
            XCTAssertEqual(mergedAgain.creationProvenanceClock, original.creationProvenanceClock)
            let promoted = try assertOriginalPromotion(mergedAgain, original: original)
            for (lhs, rhs) in [(promoted, later), (later, promoted)] {
                let retained = try CompanionFieldMerge.merge(lhs, rhs)
                XCTAssertEqual(retained.version.fieldVersions["created_at"], original.creationProvenanceClock)
                XCTAssertEqual(MobileSharedTabProjection(retained)?.originDevice, creator)
            }
        }
    }

    func testFileRestartAndReplayDoNotLoseEvidenceOrReenqueueAnEcho() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiCreationEvidence-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let original = try localNode()
        let rewrite = try legacyRewrite(original, milliseconds: 5_000)
        for (index, pair) in [(original, rewrite), (rewrite, original)].enumerated() {
            let store = FileCompanionStore(fileURL: directory.appendingPathComponent("\(index).json"))
            try await store.save(CompanionSnapshot(treeNodes: [pair.0]))
            let repository = LocalFirstRepository(store: store, localDeviceID: creator)
            let first = try await repository.mergeImportedBatch([.init(token: 1, value: .treeNode(pair.1))])
            guard case .some(.accepted) = first.first?.disposition else { return XCTFail("Merge rejected") }
            let replay = try await repository.mergeImportedBatch([.init(token: 2, value: .treeNode(rewrite))])
            try assertNoReenqueue(replay)
            let restarted = LocalFirstRepository(store: store, localDeviceID: creator)
            let snapshot = try await restarted.currentSnapshot()
            let restored = try XCTUnwrap(snapshot.treeNodes.first)
            XCTAssertEqual(restored.creationProvenanceClock, original.creationProvenanceClock)
            _ = try assertOriginalPromotion(restored, original: original)
            let replayAfterRestart = try await restarted.mergeImportedBatch([.init(token: 3, value: .treeNode(rewrite))])
            try assertNoReenqueue(replayAfterRestart)
        }
    }

    func testEvidenceOnlyChangeIsPersistedWithoutBecomingReplicatedState() async throws {
        let original = try localNode()
        let rewrite = try legacyRewrite(original, milliseconds: 5_000)
        var observed = rewrite
        observed.creationProvenanceClock = original.creationProvenanceClock
        let store = InMemoryCompanionStore(snapshot: CompanionSnapshot(treeNodes: [rewrite]))
        let repository = LocalFirstRepository(store: store, localDeviceID: creator)
        let outcome = try await repository.mergeImportedBatch([.init(token: 1, value: .treeNode(observed))])
        try assertNoReenqueue(outcome)
        let restarted = LocalFirstRepository(store: store, localDeviceID: creator)
        let snapshot = try await restarted.currentSnapshot()
        XCTAssertEqual(snapshot.treeNodes.first?.creationProvenanceClock, original.creationProvenanceClock)
        XCTAssertEqual(snapshot.treeNodes, [rewrite])
    }

    func testLegacyBooleanSnapshotUpgradesOnlyExplicitEvidence() throws {
        let original = try localNode()
        var payload = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(original)) as? [String: Any])
        payload.removeValue(forKey: "creationProvenanceClock")
        payload["creationProvenanceKnown"] = true
        let restored = try JSONDecoder().decode(TreeNode.self, from: JSONSerialization.data(withJSONObject: payload))
        XCTAssertEqual(restored.creationProvenanceClock, original.creationProvenanceClock)
        let rewrite = try legacyRewrite(restored, milliseconds: 5_000)
        _ = try assertOriginalPromotion(CompanionFieldMerge.merge(restored, rewrite), original: original)

        for known in [false, nil] as [Bool?] {
            payload["creationProvenanceKnown"] = known
            let unknown = try JSONDecoder().decode(TreeNode.self, from: JSONSerialization.data(withJSONObject: payload))
            let merged = try CompanionFieldMerge.merge(unknown, rewrite)
            XCTAssertNil(merged.creationProvenanceClock)
            let promoted = try SharedTabCreationProvenance.preparePromotion(merged)
            XCTAssertEqual(promoted.version.fieldVersions["created_at"], SharedTabContract.bottom)
            XCTAssertNil(MobileSharedTabProjection(promoted)?.originDevice)
        }
    }

    func testLocalEditAfterLegacyRewriteKeepsIndependentCreationEvidence() throws {
        let original = try localNode()
        let rewrite = try legacyRewrite(original, milliseconds: 5_000)
        let merged = try CompanionFieldMerge.merge(original, rewrite)
        var candidate = merged
        candidate.title = "Local edit after import"
        let next = HybridLogicalClock(physicalMilliseconds: 9_000, nodeID: creator)
        candidate.version = SyncVersion(modifiedAt: next, modifiedBy: creator)
        let stamped = CompanionFieldMerge.stampLocal(previous: merged, candidate: candidate)
        XCTAssertEqual(stamped.creationProvenanceClock, original.creationProvenanceClock)
        XCTAssertEqual(stamped.version.fieldVersions["created_at"], rewrite.version.fieldVersions["created_at"])
        _ = try assertOriginalPromotion(stamped, original: original)
    }

    private func localNode() throws -> TreeNode {
        let clock = HybridLogicalClock(physicalMilliseconds: 1_000, nodeID: creator)
        return try TreeNode(
            treeNodeID: TreeNodeID(), workspaceID: WorkspaceID(), kind: .savedPage,
            title: "Original", url: "https://example.test/page",
            orderKey: OrderKey.between(nil, nil, tieBreaker: creator), creationProvenanceKnown: true,
            createdAt: clock, version: SyncVersion(modifiedAt: clock, modifiedBy: creator)
                .normalized(for: CompanionFieldMerge.treeNodeFields)
        )
    }

    /// Exercise the real v2 codec: creation time stays immutable, but an older
    /// client rewrites its creation field clock with the later editor clock.
    private func legacyRewrite(_ node: TreeNode, milliseconds: UInt64) throws -> TreeNode {
        var payload = try XCTUnwrap(JSONSerialization.jsonObject(with: codec.encode(node)) as? [String: Any])
        let clock = HybridLogicalClock(physicalMilliseconds: milliseconds, logicalCounter: 3, nodeID: editor)
        let physical = try codec.timeString(clock)
        let device = editor.rawValue.uuidString.lowercased()
        payload["title"] = "Legacy edit \(milliseconds)"
        payload["modified_at"] = physical
        payload["version_physical"] = physical
        payload["version_logical"] = clock.logicalCounter
        payload["version_device"] = device
        var fields = try XCTUnwrap(payload["field_versions"] as? [String: Any])
        for name in ["title", "modified_at", "created_at"] {
            fields[name] = ["physical": physical, "logical": clock.logicalCounter, "device": device]
        }
        payload["field_versions"] = fields
        let version = try codec.version(payload)
        let envelope = SyncRecord(
            recordID: node.id.rawValue, entityID: node.id.rawValue, schemaVersion: 2, dataClass: .treeNode,
            modifiedAt: version.modifiedAt, originatingDevice: version.modifiedBy,
            encryptedValue: EncryptedValue(keyVersion: 1, nonce: Data(repeating: 0, count: 12),
                                           ciphertextAndTag: Data(repeating: 0, count: 16))
        )
        return try codec.decodeTreeNode(envelope, plaintext: JSONSerialization.data(withJSONObject: payload))
    }

    private func assertOriginalPromotion(_ node: TreeNode, original: TreeNode) throws -> TreeNode {
        let promoted = try SharedTabCreationProvenance.preparePromotion(node)
        XCTAssertEqual(promoted.version.fieldVersions["created_at"], original.creationProvenanceClock)
        XCTAssertEqual(promoted.creationProvenanceClock, original.creationProvenanceClock)
        XCTAssertEqual(promoted.createdAt, original.createdAt, "Neither creation time nor actor/counter is replaced.")
        XCTAssertEqual(MobileSharedTabProjection(promoted)?.originDevice, creator)
        XCTAssertEqual(SharedTabWireReadPolicy.defaultWriteVersion, 2)
        XCTAssertThrowsError(try codec.encode(promoted), "Preparation cannot enable v3 writes.")
        return promoted
    }

    private func assertNoReenqueue(_ outcomes: [CompanionImportMergeOutcome]) throws {
        let outcome = try XCTUnwrap(outcomes.first)
        guard case .accepted(_, let shouldReenqueue) = outcome.disposition else { return XCTFail("Merge rejected") }
        XCTAssertFalse(shouldReenqueue, "Local evidence must not generate a wire echo.")
    }
}
