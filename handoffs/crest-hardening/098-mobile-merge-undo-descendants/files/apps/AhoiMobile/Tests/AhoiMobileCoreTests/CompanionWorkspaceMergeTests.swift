import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// ADR 0012, WS-MERGE-07: merging Workspaces on mobile.
final class CompanionWorkspaceMergeTests: XCTestCase {
    private func makeRepository() -> LocalFirstRepository {
        LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: DeviceID(
                rawValue: UUID(uuidString: "70000000-0000-4000-8000-000000000086")!
            )
        )
    }

    func testMergeIntoFolderKeepsOrderAndUndoRestoresTheSource() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let kept = try await repository.createTreeNode(
            workspaceID: target.id, kind: .savedPage, title: "Mail",
            url: "https://example.test/mail")
        let folder = try await repository.createTreeNode(
            workspaceID: source.id, kind: .folder, title: "Papers")
        let nested = try await repository.createTreeNode(
            workspaceID: source.id, parentID: folder.id, kind: .savedPage,
            title: "Paper", url: "https://example.test/paper")
        let loose = try await repository.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "Notes",
            url: "https://example.test/notes")

        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: true)

        let folderID = try XCTUnwrap(receipt.folderID)
        XCTAssertEqual(receipt.nodes.last?.id, folderID)
        var snapshot = try await repository.currentSnapshot()
        XCTAssertFalse(snapshot.visibleWorkspaces.contains { $0.id == source.id })
        let mergeFolder = try XCTUnwrap(snapshot.visibleTreeNodes.first { $0.id == folderID })
        XCTAssertEqual(mergeFolder.title, "Research")
        XCTAssertEqual(mergeFolder.workspaceID, target.id)
        XCTAssertNil(mergeFolder.parentID)
        XCTAssertGreaterThan(mergeFolder.syncSortKey, kept.syncSortKey)
        let children = snapshot.visibleTreeNodes
            .filter { $0.parentID == folderID }
            .sorted { $0.syncSortKey < $1.syncSortKey }
        XCTAssertEqual(children.map(\.id), [folder.id, loose.id])
        let movedNested = try XCTUnwrap(snapshot.visibleTreeNodes.first { $0.id == nested.id })
        XCTAssertEqual(movedNested.workspaceID, target.id)
        XCTAssertEqual(movedNested.parentID, folder.id)

        let undone = try await repository.undoWorkspaceMerge(receipt)
        XCTAssertFalse(undone.workspace.isDeleted)
        XCTAssertGreaterThan(undone.workspace.version, receipt.source.version)
        snapshot = try await repository.currentSnapshot()
        XCTAssertTrue(snapshot.visibleWorkspaces.contains { $0.id == source.id })
        XCTAssertFalse(snapshot.visibleTreeNodes.contains { $0.id == folderID })
        for node in [folder, nested, loose] {
            let restored = try XCTUnwrap(snapshot.visibleTreeNodes.first { $0.id == node.id })
            XCTAssertEqual(restored.workspaceID, source.id)
            XCTAssertEqual(restored.parentID, node.parentID)
            XCTAssertEqual(restored.orderKey, node.orderKey)
        }
    }

    func testFlatMergeAppendsAfterTheTargetInSourceOrder() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let kept = try await repository.createTreeNode(
            workspaceID: target.id, kind: .savedPage, title: "Mail",
            url: "https://example.test/mail")
        let first = try await repository.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "First",
            url: "https://example.test/1")
        let second = try await repository.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "Second",
            url: "https://example.test/2")

        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false)

        XCTAssertNil(receipt.folderID)
        let roots = try await repository.currentSnapshot().visibleTreeNodes
            .filter { $0.workspaceID == target.id && $0.parentID == nil }
            .sorted { $0.syncSortKey < $1.syncSortKey }
        XCTAssertEqual(roots.map(\.id), [kept.id, first.id, second.id])
    }

    func testUndoIsRefusedOnceAMergedRecordChanged() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let page = try await repository.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "Page",
            url: "https://example.test/")
        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: true)
        _ = try await repository.updateTreeNode(page.id, title: "Renamed")

        do {
            _ = try await repository.undoWorkspaceMerge(receipt)
            XCTFail("Expected an outdated undo")
        } catch {
            XCTAssertEqual(error as? LocalCompanionStoreError, .mergeUndoOutdated)
        }
        let snapshot = try await repository.currentSnapshot()
        XCTAssertFalse(snapshot.visibleWorkspaces.contains { $0.id == source.id })
    }

    func testUndoRefusesANewChildOfTheMergeFolderWithoutChangingState() async throws {
        try await assertUndoRefusesUnrecordedChild(intoFolder: true, useMergeFolder: true)
    }

    func testUndoRefusesANewChildOfAMovedFolderWithoutChangingState() async throws {
        for intoFolder in [true, false] {
            try await assertUndoRefusesUnrecordedChild(
                intoFolder: intoFolder, useMergeFolder: false)
        }
    }

    func testUndoRefusesAnExistingPageMovedUnderTheMergeFolder() async throws {
        try await assertUndoRefusesUnrecordedChild(
            intoFolder: true, useMergeFolder: true, moveExisting: true)
    }

    private func assertUndoRefusesUnrecordedChild(
        intoFolder: Bool,
        useMergeFolder: Bool,
        moveExisting: Bool = false
    ) async throws {
        let store = InMemoryCompanionStore()
        let device = DeviceID()
        let repository = LocalFirstRepository(store: store, localDeviceID: device)
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let movedFolder = try await repository.createTreeNode(
            workspaceID: source.id, kind: .folder, title: "Papers")
        let existing = try await repository.createTreeNode(
            workspaceID: target.id, kind: .savedPage, title: "Existing",
            url: "https://example.test/existing")
        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: intoFolder)
        let parentID = useMergeFolder ? try XCTUnwrap(receipt.folderID) : movedFolder.id
        let added: TreeNode
        if moveExisting {
            added = try await repository.moveTreeNode(
                existing.id, to: target.id, parentID: parentID).node
        } else {
            added = try await repository.createTreeNode(
                workspaceID: target.id, parentID: parentID, kind: .savedPage,
                title: "Added after merge", url: "https://example.test/new")
        }
        let before = try await repository.currentSnapshot()
        let parentBefore = try XCTUnwrap(before.treeNodes.first { $0.id == parentID })
        // Creating/moving a child does not invalidate today's per-record guard.
        XCTAssertEqual(parentBefore.version,
                       receipt.nodes.first { $0.id == parentID }?.version)
        XCTAssertFalse(receipt.nodes.contains { $0.id == added.id })

        do {
            _ = try await repository.undoWorkspaceMerge(receipt)
            XCTFail("Expected refusal before orphaning an unrecorded child")
        } catch {
            XCTAssertEqual(error as? LocalCompanionStoreError, .mergeUndoOutdated)
        }
        let after = try await repository.currentSnapshot()
        XCTAssertEqual(after, before)
        let reopened = LocalFirstRepository(store: store, localDeviceID: device)
        let persisted = try await reopened.currentSnapshot()
        XCTAssertEqual(persisted, before)
    }

    func testUndoAllowsAnUnrelatedPageAddedAtTheTargetRoot() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let moved = try await repository.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "Moved",
            url: "https://example.test/moved")
        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: true)
        let unrelated = try await repository.createTreeNode(
            workspaceID: target.id, kind: .savedPage, title: "Unrelated",
            url: "https://example.test/unrelated")

        _ = try await repository.undoWorkspaceMerge(receipt)

        let after = try await repository.currentSnapshot()
        XCTAssertEqual(after.treeNodes.first { $0.id == unrelated.id }, unrelated)
        XCTAssertEqual(after.visibleTreeNodes.first { $0.id == moved.id }?.workspaceID,
                       source.id)
        XCTAssertTrue(after.visibleWorkspaces.contains { $0.id == source.id })
    }

    func testMergeRejectsTheSameOrAMissingWorkspace() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        for target in [source.id, WorkspaceID()] {
            do {
                _ = try await repository.mergeWorkspace(source.id, into: target, intoFolder: true)
                XCTFail("Expected a refusal")
            } catch {
                XCTAssertEqual(error as? LocalCompanionStoreError, .notFound)
            }
        }
    }

    @MainActor
    func testModelMovesOpenTabsAndUndoMovesThemBack() async throws {
        let repository = makeRepository()
        let model = CompanionAppModel(repository: repository)
        await model.load()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        try await model.refreshLocalState()
        let browser = MobileBrowserController()
        model.connectWorkspaceMerges(to: browser)
        let moved = browser.createTab(workspaceID: source.id)
        let privateTab = browser.createTab(workspaceID: source.id, mode: .privateBrowsing)
        let other = browser.createTab(workspaceID: target.id)

        XCTAssertFalse(model.canMergeWorkspace(source.id, into: source.id))
        await model.mergeWorkspace(source.id, into: target.id)

        XCTAssertNil(model.loadError)
        XCTAssertNotNil(model.pendingWorkspaceMergeUndo)
        XCTAssertFalse(model.snapshot.visibleWorkspaces.contains { $0.id == source.id })
        XCTAssertEqual(browser.tabs.first { $0.id == moved }?.workspaceID, target.id)
        XCTAssertEqual(browser.tabs.first { $0.id == other }?.workspaceID, target.id)
        XCTAssertNotEqual(browser.tabs.first { $0.id == privateTab }?.workspaceID, target.id)

        await model.undoWorkspaceMerge()

        XCTAssertNil(model.pendingWorkspaceMergeUndo)
        XCTAssertTrue(model.snapshot.visibleWorkspaces.contains { $0.id == source.id })
        XCTAssertEqual(browser.tabs.first { $0.id == moved }?.workspaceID, source.id)
        XCTAssertEqual(browser.tabs.first { $0.id == other }?.workspaceID, target.id)
    }
}
