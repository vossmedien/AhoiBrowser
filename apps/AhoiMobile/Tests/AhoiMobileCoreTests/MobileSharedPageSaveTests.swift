import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class MobileSharedPageSaveTests: XCTestCase {
    @MainActor
    func testSaveKeepsInitiatingIdentityAcrossSelectionChangeAndReentrantSave() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let workspace = try await repository.createWorkspace(name: "Shared")
        let first = MobileTabRecord(title: "First", url: "https://example.test/first")
        let second = MobileTabRecord(title: "Second", url: "https://example.test/second")
        let browser = MobileBrowserController()
        browser.tabs = [first, second]
        browser.selectedTabID = first.id
        var duplicateCommits = 0

        let saved = await browser.saveSharedPage(for: first.id) { captured, didCommit in
            browser.selectedTabID = second.id
            let duplicate = await browser.saveSharedPage(for: first.id) { _, _ in
                duplicateCommits += 1
                return nil
            }
            XCTAssertNil(duplicate)
            guard let node = try? await repository.saveBrowserPage(
                captured, workspaceID: workspace.id
            ) else { return nil }
            didCommit(node)
            // Model refresh/slow outbound queuing may expose this node before
            // saveSharedPage returns. It must already resolve to the first tab.
            XCTAssertEqual(browser.ensureSharedTab(node), first.id)
            return node
        }

        let nodeID = try XCTUnwrap(saved?.treeNodeID)
        XCTAssertEqual(saved?.id, first.id)
        XCTAssertEqual(browser.selectedTabID, second.id)
        XCTAssertEqual(browser.localTabID(for: nodeID), first.id)
        XCTAssertTrue(try XCTUnwrap(browser.tabs.first { $0.id == first.id }).isSaved)
        XCTAssertFalse(try XCTUnwrap(browser.tabs.first { $0.id == second.id }).isSaved)
        XCTAssertNil(browser.tabs.first { $0.id == second.id }?.treeNodeID)
        XCTAssertEqual(duplicateCommits, 0)
        let snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleTreeNodes.count, 1)
        XCTAssertEqual(snapshot.visibleTreeNodes.first?.url, first.url)
    }

    @MainActor
    func testClosedInitiatingTabDoesNotBindTheReplacementOrStealSelection() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let workspace = try await repository.createWorkspace(name: "Saved")
        let first = MobileTabRecord(title: "First", url: "https://example.test/first")
        let remaining = MobileTabRecord(title: "Remain")
        let browser = MobileBrowserController()
        browser.tabs = [first, remaining]
        browser.selectedTabID = first.id

        let result = await browser.saveSharedPage(for: first.id) { captured, didCommit in
            browser.close(first.id)
            guard let node = try? await repository.saveBrowserPage(
                captured, workspaceID: workspace.id
            ) else { return nil }
            didCommit(node)
            return node
        }

        XCTAssertNil(result)
        XCTAssertEqual(browser.selectedTabID, remaining.id)
        XCTAssertNil(browser.selectedTab?.treeNodeID)
        XCTAssertEqual(browser.selectedTab?.isSaved, false)
        let snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleTreeNodes.count, 1,
                       "The deliberate save survives closing its local runtime.")
        let node = try XCTUnwrap(snapshot.visibleTreeNodes.first)
        XCTAssertEqual(browser.recentlyClosedTab?.treeNodeID, node.id)
        browser.undoClose()
        XCTAssertEqual(browser.localTabID(for: node.id), first.id)
        XCTAssertEqual(browser.ensureSharedTab(node), first.id)
        XCTAssertEqual(browser.tabs.count, 2)
    }

    @MainActor
    func testFailedOrPrivateSaveCannotChangeSavedState() async {
        let normal = MobileTabRecord(url: "https://example.test")
        let privateTab = MobileTabRecord(url: "https://private.test", mode: .privateBrowsing)
        let browser = MobileBrowserController()
        browser.tabs = [normal, privateTab]
        var privateCommitWasCalled = false
        let privateResult = await browser.saveSharedPage(for: privateTab.id) { _, _ in
            privateCommitWasCalled = true
            return nil
        }
        let normalResult = await browser.saveSharedPage(for: normal.id) { _, _ in nil }
        XCTAssertNil(privateResult)
        XCTAssertNil(normalResult)
        XCTAssertFalse(privateCommitWasCalled)
        XCTAssertEqual(browser.tabs, [normal, privateTab])
        XCTAssertTrue(browser.sharedPageSavesInFlight.isEmpty)
    }

    func testRepeatedSaveUpdatesAndMovesOneNodeWithoutReplacingIdentity() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let firstWorkspace = try await repository.createWorkspace(name: "First")
        let secondWorkspace = try await repository.createWorkspace(name: "Second")
        var tab = MobileTabRecord(title: "Original", url: "https://example.test/original")
        let original = try await repository.saveBrowserPage(tab, workspaceID: firstWorkspace.id)
        tab.treeNodeID = original.id
        tab.customTitle = "User title"
        tab.url = "https://example.test/updated"
        let updated = try await repository.saveBrowserPage(tab, workspaceID: secondWorkspace.id)
        let snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(updated.id, original.id)
        XCTAssertEqual(updated.createdAt, original.createdAt)
        XCTAssertEqual(updated.workspaceID, secondWorkspace.id)
        XCTAssertEqual(updated.title, "User title")
        XCTAssertEqual(updated.url, tab.url)
        XCTAssertNil(updated.parentID)
        XCTAssertEqual(snapshot.visibleTreeNodes.count, 1)

        _ = try await repository.deleteTreeNode(original.id)
        do {
            _ = try await repository.saveBrowserPage(tab, workspaceID: secondWorkspace.id)
            XCTFail("Saving a stale binding must not resurrect or duplicate a deleted page.")
        } catch {
            XCTAssertEqual(error as? LocalCompanionStoreError, .notFound)
        }
        let deletedSnapshot = try await repository.currentSnapshot()
        XCTAssertTrue(deletedSnapshot.visibleTreeNodes.isEmpty)
        XCTAssertEqual(deletedSnapshot.treeNodes.count, 1)
    }
}
