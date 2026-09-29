import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class CompanionBookmarkIntegrationTests: XCTestCase {
    private let windowsEpochMicroseconds: Int64 = 11_644_473_600_000_000

    func testLegacySnapshotWithoutBookmarksKeepsExistingDomains() throws {
        let device = deviceID(1)
        let version = makeVersion(at: 100, device: device)
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Legacy workspace",
            version: version
        )
        let node = try TreeNode(
            treeNodeID: TreeNodeID(),
            workspaceID: workspace.id,
            kind: .folder,
            title: "Legacy folder",
            orderKey: try OrderKey(components: [1], tieBreaker: device),
            version: version
        )
        let current = CompanionSnapshot(
            workspaces: [workspace],
            treeNodes: [node],
            bookmarks: [try makeBookmark()]
        )
        var object = try XCTUnwrap(
            JSONSerialization.jsonObject(with: JSONEncoder().encode(current)) as? [String: Any]
        )
        XCTAssertNotNil(object.removeValue(forKey: "bookmarks"))
        let legacyData = try JSONSerialization.data(
            withJSONObject: object,
            options: [.sortedKeys]
        )

        let decoded = try JSONDecoder().decode(CompanionSnapshot.self, from: legacyData)

        XCTAssertEqual(decoded.workspaces, [workspace])
        XCTAssertEqual(decoded.treeNodes, [node])
        XCTAssertTrue(decoded.bookmarks.isEmpty)
        XCTAssertTrue(decoded.visibleBookmarks.isEmpty)
    }

    func testBookmarkDomainPersistsNativeURLWithoutReclassifyingTreeNodes() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiBookmarkPersistence-\(UUID().uuidString)",
            isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let fileURL = directory.appendingPathComponent("snapshot.json")
        let device = deviceID(2)
        let first = LocalFirstRepository(
            store: FileCompanionStore(fileURL: fileURL),
            localDeviceID: device
        )
        let workspace = try await first.createWorkspace(name: "Separate workspace")
        let page = try await first.createTreeNode(
            workspaceID: workspace.id,
            kind: .savedPage,
            title: "Workspace page",
            url: "https://example.test/workspace"
        )
        let bookmark = try await first.createBookmark(
            kind: .url,
            rootKind: .other,
            parentID: nil,
            title: "Native settings",
            url: "chrome://settings/"
        ).bookmark

        let second = LocalFirstRepository(
            store: FileCompanionStore(fileURL: fileURL),
            localDeviceID: device
        )
        let restored = try await second.currentSnapshot()

        XCTAssertEqual(restored.treeNodes.map(\.id), [page.id])
        XCTAssertEqual(restored.bookmarks.map(\.id), [bookmark.id])
        XCTAssertEqual(restored.visibleBookmarks.first?.url, "chrome://settings/")
        XCTAssertFalse(restored.treeNodes.contains { $0.title == bookmark.title })
    }

    func testLocalCRUDMovesReordersAndDeletesFolderDescendants() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let folder = try await repository.createBookmark(
            kind: .folder,
            rootKind: .bar,
            parentID: nil,
            title: "Folder",
            url: ""
        ).bookmark
        let first = try await repository.createBookmark(
            kind: .url,
            rootKind: nil,
            parentID: folder.id,
            title: "First",
            url: "https://example.test/first"
        ).bookmark
        let second = try await repository.createBookmark(
            kind: .url,
            rootKind: nil,
            parentID: folder.id,
            title: "Second",
            url: "https://example.test/second"
        ).bookmark
        _ = try await repository.updateBookmark(
            first.id,
            title: "First edited",
            url: "chrome://bookmarks/"
        )
        _ = try await repository.reorderBookmark(second.id, before: first.id)

        var snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(
            snapshot.visibleBookmarks.filter { $0.parentID == folder.id }.map(\.id),
            [second.id, first.id]
        )
        XCTAssertEqual(
            snapshot.visibleBookmarks.first { $0.id == first.id }?.url,
            "chrome://bookmarks/"
        )

        _ = try await repository.moveBookmark(folder.id, rootKind: .mobile, parentID: nil)
        let destination = try await repository.createBookmark(
            kind: .folder,
            rootKind: .other,
            parentID: nil,
            title: "Destination",
            url: ""
        ).bookmark
        _ = try await repository.moveBookmark(first.id, rootKind: nil, parentID: destination.id)
        snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.bookmarks.first { $0.id == folder.id }?.rootKind, .mobile)
        XCTAssertEqual(snapshot.bookmarks.first { $0.id == first.id }?.parentID, destination.id)

        let firstDeletion = try await repository.deleteBookmark(destination.id)
        XCTAssertEqual(Set(firstDeletion.map(\.id)), Set([destination.id, first.id]))
        let secondDeletion = try await repository.deleteBookmark(folder.id)
        XCTAssertEqual(Set(secondDeletion.map(\.id)), Set([folder.id, second.id]))
        snapshot = try await repository.currentSnapshot()
        XCTAssertTrue(snapshot.visibleBookmarks.isEmpty)
        XCTAssertEqual(snapshot.bookmarks.count, 4)
        XCTAssertTrue(snapshot.bookmarks.allSatisfy(\.isDeleted))
    }

    func testInvalidKnownParentAndCycleLeaveSnapshotUnchanged() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let page = try makeBookmark(title: "Known URL parent")
        _ = try await repository.upsert(page)
        let child = try makeBookmark(
            id: bookmarkID(2),
            rootKind: nil,
            parentID: page.id,
            title: "Invalid child"
        )
        let beforeInvalidParent = try await repository.currentSnapshot()
        await XCTAssertThrowsErrorAsync {
            _ = try await repository.upsert(child)
        }
        let afterInvalidParent = try await repository.currentSnapshot()
        XCTAssertEqual(afterInvalidParent, beforeInvalidParent)

        let firstFolder = try makeBookmark(
            id: bookmarkID(3),
            kind: .folder,
            rootKind: .bar,
            title: "First folder",
            url: ""
        )
        let secondFolder = try makeBookmark(
            id: bookmarkID(4),
            kind: .folder,
            rootKind: nil,
            parentID: firstFolder.id,
            title: "Second folder",
            url: ""
        )
        _ = try await repository.upsert(firstFolder)
        _ = try await repository.upsert(secondFolder)
        var cyclic = firstFolder
        cyclic.rootKind = nil
        cyclic.parentID = secondFolder.id
        cyclic = try edit(cyclic, fields: ["location"], at: 500, device: deviceID(2))
        let beforeCycle = try await repository.currentSnapshot()
        await XCTAssertThrowsErrorAsync {
            _ = try await repository.upsert(cyclic)
        }
        let afterCycle = try await repository.currentSnapshot()
        XCTAssertEqual(afterCycle, beforeCycle)
    }

    func testChildBeforeParentIsDurableButOnlyVisibleAfterLiveFolderArrives() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let parentID = bookmarkID(10)
        let child = try makeBookmark(
            id: bookmarkID(11),
            rootKind: nil,
            parentID: parentID,
            title: "Early child"
        )
        _ = try await repository.upsert(child)
        var snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(snapshot.bookmarks.map(\.id), [child.id])
        XCTAssertTrue(snapshot.visibleBookmarks.isEmpty)

        let parent = try makeBookmark(
            id: parentID,
            kind: .folder,
            rootKind: .mobile,
            title: "Late parent",
            url: ""
        )
        _ = try await repository.upsert(parent)
        snapshot = try await repository.currentSnapshot()
        XCTAssertEqual(Set(snapshot.visibleBookmarks.map(\.id)), Set([parent.id, child.id]))
    }

    func testFieldMergeIsDisjointAtomicAndRejectsKindOrEqualClockConflicts() throws {
        let base = try makeBookmark()
        var renamed = base
        renamed.title = "Renamed"
        renamed = try edit(renamed, fields: ["title"], at: 200, device: deviceID(1))
        var redirected = base
        redirected.url = "https://example.test/redirected"
        redirected = try edit(redirected, fields: ["url"], at: 300, device: deviceID(2))
        var corrected = base
        corrected.createdAt = windowsEpochMicroseconds - 1
        corrected = try edit(
            corrected,
            fields: ["created_at"],
            at: 350,
            device: deviceID(3)
        )

        let firstMerge = try CompanionBookmarkFieldMerge.merge(renamed, redirected)
        XCTAssertEqual(
            firstMerge,
            try CompanionBookmarkFieldMerge.merge(redirected, renamed)
        )
        let merged = try CompanionBookmarkFieldMerge.merge(firstMerge, corrected)
        XCTAssertEqual(merged.title, "Renamed")
        XCTAssertEqual(merged.url, "https://example.test/redirected")
        XCTAssertEqual(merged.createdAt, windowsEpochMicroseconds - 1)

        var rooted = base
        rooted.rootKind = .other
        rooted.sortKey = "ROOT"
        rooted = try edit(rooted, fields: ["location"], at: 400, device: deviceID(1))
        let parentID = bookmarkID(20)
        var nested = base
        nested.rootKind = nil
        nested.parentID = parentID
        nested.sortKey = "NESTED"
        nested = try edit(nested, fields: ["location"], at: 500, device: deviceID(2))
        let located = try CompanionBookmarkFieldMerge.merge(rooted, nested)
        XCTAssertEqual(located, try CompanionBookmarkFieldMerge.merge(nested, rooted))
        XCTAssertNil(located.rootKind)
        XCTAssertEqual(located.parentID, parentID)
        XCTAssertEqual(located.sortKey, "NESTED")

        var changedKind = base
        changedKind.kind = .folder
        changedKind.url = ""
        changedKind = try edit(
            changedKind,
            fields: ["kind", "url"],
            at: 600,
            device: deviceID(3)
        )
        XCTAssertThrowsError(try CompanionBookmarkFieldMerge.merge(base, changedKind)) {
            XCTAssertEqual(
                $0 as? CompanionFieldMergeError,
                .immutableFieldConflict("kind")
            )
        }
        var equalClockConflict = base
        equalClockConflict.title = "Divergent title"
        XCTAssertThrowsError(try CompanionBookmarkFieldMerge.merge(base, equalClockConflict)) {
            XCTAssertEqual(
                $0 as? CompanionFieldMergeError,
                .equalClockConflict("title")
            )
        }
    }

    func testNewerUnrelatedTitleAndDelayedStaleLiveRecordCannotResurrectDeletion() async throws {
        let base = try makeBookmark(id: bookmarkID(30))
        let deleteClock = clock(300, device: deviceID(1))
        var deleted = base
        deleted.tombstone = Tombstone(
            entityID: base.id.rawValue,
            deletedAt: deleteClock,
            deletedBy: deleteClock.nodeID,
            originalParentID: nil,
            originalOrderKey: nil,
            purgeAfterMilliseconds: deleteClock.physicalMilliseconds + 1_000
        )
        deleted = try edit(deleted, fields: ["tombstone"], clock: deleteClock)
        var laterTitle = base
        laterTitle.title = "Newer unrelated title"
        laterTitle = try edit(laterTitle, fields: ["title"], at: 400, device: deviceID(2))

        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        _ = try await repository.upsert(deleted)
        _ = try await repository.upsert(laterTitle)
        _ = try await repository.upsert(base)
        let snapshot = try await repository.currentSnapshot()
        let retained = try XCTUnwrap(snapshot.bookmarks.first { $0.id == base.id })
        XCTAssertTrue(retained.isDeleted)
        XCTAssertEqual(retained.title, "Newer unrelated title")
        XCTAssertTrue(snapshot.visibleBookmarks.isEmpty)
    }

    private func makeBookmark(
        id: BookmarkID = BookmarkID(),
        kind: BookmarkKind = .url,
        rootKind: BookmarkRoot? = .bar,
        parentID: BookmarkID? = nil,
        title: String = "Base",
        url: String = "https://example.test/base"
    ) throws -> BookmarkRecord {
        let device = deviceID(1)
        let version = makeVersion(at: 100, device: device)
        return try BookmarkRecord(
            bookmarkID: id,
            kind: kind,
            rootKind: rootKind,
            parentID: parentID,
            sortKey: "M",
            title: title,
            url: url,
            createdAt: windowsEpochMicroseconds + 100,
            version: version
        )
    }

    private func edit(
        _ bookmark: BookmarkRecord,
        fields: Set<String>,
        at milliseconds: UInt64,
        device: DeviceID
    ) throws -> BookmarkRecord {
        try edit(bookmark, fields: fields, clock: clock(milliseconds, device: device))
    }

    private func edit(
        _ bookmark: BookmarkRecord,
        fields: Set<String>,
        clock: HybridLogicalClock
    ) throws -> BookmarkRecord {
        var edited = bookmark
        var fieldVersions = bookmark.version.normalized(
            for: BookmarkRecord.syncFields
        ).fieldVersions
        for field in fields { fieldVersions[field] = clock }
        edited.version = SyncVersion(
            modifiedAt: clock,
            modifiedBy: clock.nodeID,
            fieldVersions: fieldVersions
        )
        try edited.validate()
        return edited
    }

    private func makeVersion(at milliseconds: UInt64, device: DeviceID) -> SyncVersion {
        let stamp = clock(milliseconds, device: device)
        return SyncVersion(
            modifiedAt: stamp,
            modifiedBy: device,
            fieldVersions: Dictionary(uniqueKeysWithValues:
                BookmarkRecord.syncFields.map { ($0, stamp) }
            )
        )
    }

    private func clock(_ milliseconds: UInt64, device: DeviceID) -> HybridLogicalClock {
        HybridLogicalClock(physicalMilliseconds: milliseconds, nodeID: device)
    }

    private func deviceID(_ suffix: UInt32) -> DeviceID {
        DeviceID(rawValue: fixedUUID(prefix: "10000000", suffix: suffix))
    }

    private func bookmarkID(_ suffix: UInt32) -> BookmarkID {
        BookmarkID(rawValue: fixedUUID(prefix: "90000000", suffix: suffix))
    }

    private func fixedUUID(prefix: String, suffix: UInt32) -> UUID {
        UUID(uuidString: String(format: "%@-0000-4000-8000-%012x", prefix, suffix))!
    }
}

private func XCTAssertThrowsErrorAsync<T>(
    _ expression: () async throws -> T,
    file: StaticString = #filePath,
    line: UInt = #line
) async {
    do {
        _ = try await expression()
        XCTFail("Expected expression to throw", file: file, line: line)
    } catch {}
}
