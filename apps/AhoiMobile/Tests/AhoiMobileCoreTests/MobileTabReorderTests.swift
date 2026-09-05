import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class MobileTabReorderTests: XCTestCase {
    @MainActor
    func testSharedIdentityBindingIsUniqueNormalOnlyAndSurvivesSessionEncoding() throws {
        let first = MobileTabRecord(title: "First")
        let second = MobileTabRecord(title: "Second")
        let privateTab = MobileTabRecord(mode: .privateBrowsing)
        let nodeID = TreeNodeID()
        let browser = MobileBrowserController()
        browser.tabs = [first, second, privateTab]

        XCTAssertTrue(browser.bindTab(first.id, to: nodeID))
        XCTAssertTrue(browser.bindTab(first.id, to: nodeID))
        XCTAssertEqual(browser.localTabID(for: nodeID), first.id)
        XCTAssertFalse(browser.bindTab(second.id, to: nodeID))
        XCTAssertFalse(browser.bindTab(privateTab.id, to: TreeNodeID()))
        XCTAssertFalse(browser.bindTab(first.id, to: TreeNodeID()))
        XCTAssertFalse(browser.bindTab(second.id, to: TreeNodeID(rawValue: second.id)))
        XCTAssertNotEqual(first.id, nodeID.rawValue)

        let encoded = try JSONEncoder().encode(MobileBrowserSessionSnapshot(
            tabs: browser.tabs, selectedTabID: first.id
        ))
        let restored = try JSONDecoder().decode(MobileBrowserSessionSnapshot.self, from: encoded)
        XCTAssertEqual(restored.tabs.first { $0.id == first.id }?.treeNodeID, nodeID)
        XCTAssertEqual(restored.tabs.count, 2)
        XCTAssertFalse(restored.tabs.contains { $0.id == privateTab.id })
    }

    func testLegacyLocalTabWithoutSharedIdentityStillDecodes() throws {
        let legacy = Data(#"{"id":"10000000-0000-4000-8000-000000000001","title":"Legacy","createdAt":0,"lastActiveAt":0,"isSaved":false,"mode":"normal"}"#.utf8)
        let decoded = try JSONDecoder().decode(MobileTabRecord.self, from: legacy)
        XCTAssertNil(decoded.treeNodeID)
        XCTAssertEqual(decoded.title, "Legacy")
    }

    @MainActor
    func testSharedReferencesAreLazyAndDeduplicateIdentityNotURL() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let workspace = try await repository.createWorkspace(name: "Shared")
        let first = try await repository.createTreeNode(
            workspaceID: workspace.id, kind: .savedPage,
            title: "First", url: "https://example.test/one"
        )
        let second = try await repository.createTreeNode(
            workspaceID: workspace.id, kind: .savedPage,
            title: "Same URL, different tab", url: first.url
        )
        let browser = MobileBrowserController()
        let selected = browser.createTab(mode: .privateBrowsing)
        let firstID = try XCTUnwrap(browser.ensureSharedTab(first))
        let secondID = try XCTUnwrap(browser.ensureSharedTab(second))

        XCTAssertEqual(browser.ensureSharedTab(first), firstID)
        XCTAssertNotEqual(firstID, secondID)
        XCTAssertEqual(browser.normalTabs.count, 2)
        XCTAssertEqual(browser.privateTabs.count, 1)
        XCTAssertEqual(browser.selectedTabID, selected)
        XCTAssertTrue(browser.pages.isEmpty, "Receiving a tab may not load a website.")
        XCTAssertTrue(browser.normalTabs.allSatisfy(\.isSaved))
        XCTAssertEqual(browser.normalTabs.map(\.treeNodeID), [first.id, second.id])

        let folder = try await repository.createTreeNode(
            workspaceID: workspace.id, kind: .folder, title: "Folder"
        )
        XCTAssertNil(browser.ensureSharedTab(folder))
        let deleted = try await repository.deleteTreeNode(first.id)
        let tombstone = try XCTUnwrap(deleted.first)
        XCTAssertNil(browser.ensureSharedTab(tombstone))
        var unsafe = second
        unsafe.url = "javascript:alert(1)"
        XCTAssertNil(browser.ensureSharedTab(unsafe))
        XCTAssertEqual(browser.normalTabs.count, 2)
    }

    func testSessionPreservesTabsWhileRejectingDuplicateOrCollidingBindings() {
        let nodeID = TreeNodeID()
        let first = MobileTabRecord(treeNodeID: nodeID, title: "First")
        let duplicate = MobileTabRecord(treeNodeID: nodeID, title: "Keep local work")
        let collision = MobileTabRecord(
            id: nodeID.rawValue, treeNodeID: nodeID, title: "Colliding record"
        )
        let restored = MobileBrowserSessionSnapshot(tabs: [first, duplicate, collision])

        XCTAssertEqual(restored.tabs.count, 3)
        XCTAssertEqual(restored.tabs.first?.treeNodeID, nodeID)
        XCTAssertNil(restored.tabs[1].treeNodeID)
        XCTAssertNil(restored.tabs[2].treeNodeID)
        XCTAssertEqual(restored.tabs[1].title, "Keep local work")
    }

    @MainActor
    func testPrivateTabsUseTheSameDeterministicReorderContract() throws {
        let browser = MobileBrowserController()
        let first = browser.createTab(
            url: try XCTUnwrap(URL(string: "https://private-one.example")),
            mode: .privateBrowsing
        )
        let second = browser.createTab(
            url: try XCTUnwrap(URL(string: "https://private-two.example")),
            mode: .privateBrowsing
        )
        let third = browser.createTab(
            url: try XCTUnwrap(URL(string: "https://private-three.example")),
            mode: .privateBrowsing
        )

        browser.reorderTabs(
            browser.privateTabs.map(\.id),
            fromOffsets: IndexSet(integer: 0),
            toOffset: 3
        )

        XCTAssertEqual(browser.privateTabs.map(\.id), [second, third, first])
    }
}
