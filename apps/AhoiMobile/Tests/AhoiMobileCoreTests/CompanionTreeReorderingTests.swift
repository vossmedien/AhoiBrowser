import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionTreeReorderingTests: XCTestCase {
    private static func nativeFolder(
        workspace: WorkspaceID, key: String, device: DeviceID = DeviceID()
    ) throws -> TreeNode {
        let version = SyncVersion(
            modifiedAt: HybridLogicalClock(physicalMilliseconds: 100, nodeID: device),
            modifiedBy: device)
        return try TreeNode(
            treeNodeID: TreeNodeID(), workspaceID: workspace, kind: .folder,
            title: key, orderKey: OrderKey(components: [10], tieBreaker: device),
            wireSortKey: key, version: version.normalized(for: CompanionFieldMerge.treeNodeFields))
    }

    func testAppendAndReorderUseOpaqueNativeKeysInsteadOfTheirNumericAdapters() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let workspace = try await repository.createWorkspace(name: "Desktop")
        let first = try Self.nativeFolder(workspace: workspace.id, key: "A")
        let last = try Self.nativeFolder(workspace: workspace.id, key: "Z")
        _ = try await repository.upsert(first)
        _ = try await repository.upsert(last)

        let added = try await repository.createTreeNode(
            workspaceID: workspace.id, kind: .folder, title: "Appended")
        XCTAssertTrue(CompanionTreePosition.less(last.syncSortKey, added.syncSortKey))
        var snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleTreeNodes.map(\.id), [first.id, last.id, added.id])

        let moved = try await repository.reorderTreeNode(added.id, before: last.id)
        XCTAssertTrue(CompanionTreePosition.less(first.syncSortKey, moved.syncSortKey))
        XCTAssertTrue(CompanionTreePosition.less(moved.syncSortKey, last.syncSortKey))
        snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleTreeNodes.map(\.id), [first.id, added.id, last.id])
        let restored = try JSONDecoder().decode(
            CompanionSnapshot.self, from: JSONEncoder().encode(snapshot))
        XCTAssertEqual(restored.visibleTreeNodes.map(\.id), [first.id, added.id, last.id])
    }

    func testMoveFlatMergeAndSaveAppendAfterNativeTargetRoots() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let source = try await repository.createWorkspace(name: "Source")
        let target = try await repository.createWorkspace(name: "Target")
        let kept = try Self.nativeFolder(workspace: target.id, key: "Z")
        let moved = try Self.nativeFolder(workspace: source.id, key: "A")
        let merged = try Self.nativeFolder(workspace: source.id, key: "B")
        for node in [kept, moved, merged] { _ = try await repository.upsert(node) }

        _ = try await repository.moveTreeNode(moved.id, to: target.id, parentID: nil)
        _ = try await repository.mergeWorkspace(source.id, into: target.id, intoFolder: false)
        let saved = try await repository.saveBrowserPage(
            MobileTabRecord(title: "Saved", url: "https://example.test/saved"),
            workspaceID: target.id)
        let snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleTreeNodes.filter { $0.workspaceID == target.id }.map(\.id),
                       [kept.id, moved.id, merged.id, saved.id])
    }

    func testOpaqueUnicodeBoundsUseWireByteOrderWithoutInvalidUTF8() throws {
        let workspace = WorkspaceID()
        let lower = try Self.nativeFolder(workspace: workspace, key: "e\u{301}")
        let upper = try Self.nativeFolder(workspace: workspace, key: "é")
        // Swift String equality normalizes these spellings, the wire does not.
        XCTAssertEqual(lower.syncSortKey, upper.syncSortKey)
        XCTAssertTrue(CompanionTreePosition.less(lower.syncSortKey, upper.syncSortKey))
        let position = try CompanionTreePosition.between(lower, upper, device: DeviceID())
        let key = try XCTUnwrap(position.wireSortKey)
        XCTAssertTrue(CompanionTreePosition.less(lower.syncSortKey, key))
        XCTAssertTrue(CompanionTreePosition.less(key, upper.syncSortKey))
        XCTAssertNotNil(String(data: Data(key.utf8), encoding: .utf8))
    }

    func testWorkspaceDisplayOrderUsesOpaqueUTF8BeforeIDTieBreak() {
        let device = DeviceID()
        let version = SyncVersion(
            modifiedAt: HybridLogicalClock(physicalMilliseconds: 100, nodeID: device),
            modifiedBy: device)
        let upper = Workspace(
            workspaceID: WorkspaceID(rawValue: UUID(uuidString:
                "10000000-0000-4000-8000-000000000001")!),
            name: "Composed", sortKey: String(Unicode.Scalar(0xe9)!), version: version)
        let lower = Workspace(
            workspaceID: WorkspaceID(rawValue: UUID(uuidString:
                "20000000-0000-4000-8000-000000000002")!),
            name: "Decomposed", sortKey: "e" + String(Unicode.Scalar(0x301)!),
            version: version)
        XCTAssertEqual(upper.sortKey, lower.sortKey)
        XCTAssertTrue(CompanionTreePosition.less(lower.sortKey, upper.sortKey))
        var snapshot = CompanionSnapshot(workspaces: [upper, lower])
        XCTAssertEqual(snapshot.visibleWorkspaces.map(\.id), [lower.id, upper.id])
        snapshot.workspaces.reverse()
        XCTAssertEqual(snapshot.visibleWorkspaces.map(\.id), [lower.id, upper.id])
    }

    func testConcurrentCanonicalTieBreakersStillHaveAnInsertablePosition() throws {
        let workspace = WorkspaceID()
        let a = DeviceID(rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000001")!)
        let b = DeviceID(rawValue: UUID(uuidString: "20000000-0000-4000-8000-000000000002")!)
        var lower = try Self.nativeFolder(workspace: workspace, key: "unused", device: a)
        var upper = try Self.nativeFolder(workspace: workspace, key: "unused", device: b)
        lower.wireSortKey = nil
        upper.wireSortKey = nil
        let position = try CompanionTreePosition.between(lower, upper, device: DeviceID())
        let key = try XCTUnwrap(position.wireSortKey)
        XCTAssertTrue(CompanionTreePosition.less(lower.syncSortKey, key))
        XCTAssertTrue(CompanionTreePosition.less(key, upper.syncSortKey))
    }

    func testEqualLocationClockRejectsDifferentUTF8SortKeyBytes() throws {
        let lower = "e" + String(Unicode.Scalar(0x301)!)
        let upper = String(Unicode.Scalar(0xe9)!)
        let existing = try Self.nativeFolder(workspace: WorkspaceID(), key: lower)
        var incoming = existing
        incoming.wireSortKey = upper
        XCTAssertEqual(lower, upper)  // Text equality is not opaque wire equality.
        XCTAssertNotEqual(Array(lower.utf8), Array(upper.utf8))
        for (old, new) in [(existing, incoming), (incoming, existing)] {
            XCTAssertThrowsError(try CompanionFieldMerge.merge(old, new)) { error in
                XCTAssertEqual(error as? CompanionFieldMergeError, .equalClockConflict("location"))
            }
        }
    }

    func testLocalUTF8LocationChangeReceivesANewClockAndSurvivesMerge() throws {
        let existing = try Self.nativeFolder(
            workspace: WorkspaceID(), key: "e" + String(Unicode.Scalar(0x301)!))
        var candidate = existing
        candidate.wireSortKey = String(Unicode.Scalar(0xe9)!)
        let clock = try existing.version.modifiedAt.ticking(
            at: existing.version.modifiedAt.physicalMilliseconds + 1)
        candidate.version = SyncVersion(modifiedAt: clock, modifiedBy: clock.nodeID)
        let changed = CompanionFieldMerge.stampLocal(previous: existing, candidate: candidate)
        XCTAssertEqual(changed.version.fieldVersions["location"], clock)
        for (old, new) in [(existing, changed), (changed, existing)] {
            let merged = try CompanionFieldMerge.merge(old, new)
            XCTAssertEqual(Array(merged.syncSortKey.utf8), Array(changed.syncSortKey.utf8))
            XCTAssertEqual(merged.version.fieldVersions["location"], clock)
        }
    }

    func testEqualWorkspaceOrderClockRejectsDifferentUTF8Bytes() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        var existing = try await repository.createWorkspace(name: "Unicode order")
        existing.sortKey = "e" + String(Unicode.Scalar(0x301)!)
        var incoming = existing
        incoming.sortKey = String(Unicode.Scalar(0xe9)!)
        for (old, new) in [(existing, incoming), (incoming, existing)] {
            XCTAssertThrowsError(try CompanionFieldMerge.merge(old, new)) { error in
                XCTAssertEqual(error as? CompanionFieldMergeError, .equalClockConflict("sort_key"))
            }
        }
    }

    func testLocalUTF8WorkspaceOrderChangeReceivesANewClock() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        var existing = try await repository.createWorkspace(name: "Unicode order")
        existing.sortKey = "e" + String(Unicode.Scalar(0x301)!)
        var candidate = existing
        candidate.sortKey = String(Unicode.Scalar(0xe9)!)
        let clock = try existing.version.modifiedAt.ticking(
            at: existing.version.modifiedAt.physicalMilliseconds + 1)
        candidate.version = SyncVersion(modifiedAt: clock, modifiedBy: clock.nodeID)
        let changed = CompanionFieldMerge.stampLocal(previous: existing, candidate: candidate)
        XCTAssertEqual(changed.version.fieldVersions["sort_key"], clock)
        for (old, new) in [(existing, changed), (changed, existing)] {
            let merged = try CompanionFieldMerge.merge(old, new)
            XCTAssertEqual(Array(merged.sortKey.utf8), Array(changed.sortKey.utf8))
            XCTAssertEqual(merged.version.fieldVersions["sort_key"], clock)
        }
    }

    func testSiblingReorderPersistsFractionalOrderWithoutChangingParent() async throws {
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: DeviceID(
                rawValue: UUID(uuidString: "81000000-0000-4000-8000-000000000001")!
            )
        )
        let workspace = try await repository.createWorkspace(name: "Project")
        let folder = try await repository.createTreeNode(
            workspaceID: workspace.id,
            kind: .folder,
            title: "Folder"
        )
        let first = try await repository.createTreeNode(
            workspaceID: workspace.id,
            parentID: folder.id,
            kind: .savedPage,
            title: "First",
            url: "https://example.test/first"
        )
        let second = try await repository.createTreeNode(
            workspaceID: workspace.id,
            parentID: folder.id,
            kind: .savedPage,
            title: "Second",
            url: "https://example.test/second"
        )
        let third = try await repository.createTreeNode(
            workspaceID: workspace.id,
            parentID: folder.id,
            kind: .savedPage,
            title: "Third",
            url: "https://example.test/third"
        )

        let moved = try await repository.reorderTreeNode(third.id, before: first.id)
        let snapshot = try await repository.currentSnapshot()
        let children = snapshot.visibleTreeNodes.filter { $0.parentID == folder.id }

        XCTAssertEqual(children.map(\.id), [third.id, first.id, second.id])
        XCTAssertEqual(moved.parentID, folder.id)
        XCTAssertEqual(moved.workspaceID, workspace.id)
        XCTAssertNil(moved.wireSortKey)
    }

    func testSiblingReorderRejectsSuccessorFromAnotherParentAndIsIdempotent() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let workspace = try await repository.createWorkspace(name: "Project")
        let first = try await repository.createTreeNode(
            workspaceID: workspace.id,
            kind: .folder,
            title: "First"
        )
        let second = try await repository.createTreeNode(
            workspaceID: workspace.id,
            kind: .folder,
            title: "Second"
        )
        let child = try await repository.createTreeNode(
            workspaceID: workspace.id,
            parentID: first.id,
            kind: .savedPage,
            title: "Child",
            url: "https://example.test/child"
        )

        let unchanged = try await repository.reorderTreeNode(first.id, before: second.id)
        XCTAssertEqual(unchanged, first)
        await XCTAssertThrowsErrorAsync {
            _ = try await repository.reorderTreeNode(second.id, before: child.id)
        } verify: { error in
            XCTAssertEqual(error as? LocalCompanionStoreError, .notFound)
        }
    }
}

private func XCTAssertThrowsErrorAsync(
    _ expression: () async throws -> Void,
    verify: (Error) -> Void
) async {
    do {
        try await expression()
        XCTFail("Expected an error")
    } catch {
        verify(error)
    }
}
