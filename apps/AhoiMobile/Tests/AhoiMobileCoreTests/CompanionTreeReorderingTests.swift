import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionTreeReorderingTests: XCTestCase {
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
