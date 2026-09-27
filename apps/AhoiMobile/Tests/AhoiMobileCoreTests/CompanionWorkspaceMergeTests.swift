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

        // Crest 084: the tombstone names the target, on the wire as well.
        XCTAssertTrue(receipt.source.isDeleted)
        XCTAssertEqual(receipt.source.mergedInto, target.id)
        let payload = try DesktopWirePayloadCodec().encode(receipt.source)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: payload) as? [String: Any])
        XCTAssertEqual(json["merged_into"] as? String,
                       target.id.rawValue.uuidString.lowercased())

        let undone = try await repository.undoWorkspaceMerge(receipt)
        XCTAssertFalse(undone.workspace.isDeleted)
        XCTAssertNil(undone.workspace.mergedInto)
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

    func testMergeTargetIsPartOfEqualClockConflictDetection() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let other = try await repository.createWorkspace(name: "Other")
        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false)
        let deleted = receipt.source

        XCTAssertEqual(try CompanionFieldMerge.merge(deleted, deleted), deleted)
        let conflictingTargets: [WorkspaceID?] = [nil, other.id]
        for conflictingTarget in conflictingTargets {
            var conflict = deleted
            conflict.mergedInto = conflictingTarget
            for (old, new) in [(deleted, conflict), (conflict, deleted)] {
                XCTAssertThrowsError(try CompanionFieldMerge.merge(old, new)) { error in
                    XCTAssertEqual(error as? CompanionFieldMergeError,
                                   .equalClockConflict("tombstone"))
                }
            }
        }
    }

    func testNewerMergeTargetWinsWithTheTombstoneFieldGroup() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let other = try await repository.createWorkspace(name: "Other")
        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false)
        let old = receipt.source
        var candidate = old
        let next = try old.version.modifiedAt.ticking(
            at: old.version.modifiedAt.physicalMilliseconds + 1)
        candidate.version = SyncVersion(modifiedAt: next, modifiedBy: next.nodeID)
        candidate.mergedInto = other.id
        let updated = CompanionFieldMerge.stampLocal(previous: old, candidate: candidate)

        XCTAssertEqual(updated.version.fieldVersions["tombstone"], next)
        for (lhs, rhs) in [(old, updated), (updated, old)] {
            let merged = try CompanionFieldMerge.merge(lhs, rhs)
            XCTAssertTrue(merged.isDeleted)
            XCTAssertEqual(merged.mergedInto, other.id)
            XCTAssertEqual(merged.version.fieldVersions["tombstone"], next)
        }
    }

    func testMergeTargetWireRoundTripAndInvalidDestinations() async throws {
        let repository = makeRepository()
        let source = try await repository.createWorkspace(name: "Research")
        let target = try await repository.createWorkspace(name: "Work")
        let receipt = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false)
        let codec = DesktopWirePayloadCodec()
        let deleted = receipt.source
        let payload = try codec.encode(deleted)
        let decoded = try codec.decodeWorkspace(
            workspaceEnvelope(deleted), plaintext: payload)
        XCTAssertEqual(decoded.mergedInto, target.id)
        XCTAssertEqual(try codec.encode(decoded), payload)

        var legacy = deleted
        legacy.mergedInto = nil
        XCTAssertNil(try codec.decodeWorkspace(
            workspaceEnvelope(legacy), plaintext: codec.encode(legacy)).mergedInto)

        for var invalid in [source, deleted] {
            invalid.mergedInto = invalid.isDeleted ? invalid.id : target.id
            XCTAssertThrowsError(try codec.encode(invalid)) { error in
                XCTAssertEqual(error as? DesktopWirePayloadCodecError, .malformedPayload)
            }
        }

        for (workspace, invalidTarget) in [
            (source, target.id.rawValue.uuidString.lowercased()),
            (deleted, deleted.id.rawValue.uuidString.lowercased()),
            (deleted, "not-a-uuid"),
        ] {
            var value = try XCTUnwrap(JSONSerialization.jsonObject(
                with: codec.encode(workspace)) as? [String: Any])
            value["merged_into"] = invalidTarget
            let corrupt = try JSONSerialization.data(withJSONObject: value)
            XCTAssertThrowsError(try codec.decodeWorkspace(
                workspaceEnvelope(workspace), plaintext: corrupt)) { error in
                    XCTAssertEqual(error as? DesktopWirePayloadCodecError, .malformedPayload)
                }
        }
    }

    private func workspaceEnvelope(_ workspace: Workspace) -> SyncRecord {
        SyncRecord(
            recordID: workspace.id.rawValue,
            entityID: workspace.id.rawValue,
            schemaVersion: workspace.version.schemaVersion,
            dataClass: .workspace,
            modifiedAt: workspace.version.modifiedAt,
            originatingDevice: workspace.version.modifiedBy,
            encryptedValue: .init(keyVersion: 1, nonce: Data(repeating: 0, count: 12),
                                  ciphertextAndTag: Data(repeating: 0, count: 16)),
            tombstone: workspace.tombstone)
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

    func testLateOfflinePageFollowsMergeWithoutRewritingWireAuthority() async throws {
        let fixture = try await lateMergeFixture()
        let page = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Offline",
            url: "https://example.test/offline")
        let wire = try DesktopWirePayloadCodec().encode(page)
        let outcomes = try await fixture.local.mergeImportedBatch([
            .init(token: 0, value: .treeNode(page)),
        ])
        guard case .accepted(_, let reenqueue)? = outcomes.first?.disposition else {
            return XCTFail("Late page must be accepted by the real import batch")
        }
        XCTAssertFalse(reenqueue, "Projection must not author an echo repair record")
        let snapshot = try await fixture.local.currentSnapshot()
        let raw = try XCTUnwrap(snapshot.treeNodes.first { $0.id == page.id })
        XCTAssertEqual(raw, page)
        XCTAssertEqual(try DesktopWirePayloadCodec().encode(raw), wire)
        let presented = try XCTUnwrap(snapshot.presentationNode(page.id))
        XCTAssertEqual(presented.workspaceID, fixture.target.id)
        XCTAssertEqual(presented.version, page.version)
        XCTAssertNil(presented.parentID)
        let restored = try JSONDecoder().decode(
            CompanionSnapshot.self, from: JSONEncoder().encode(snapshot))
        XCTAssertEqual(restored.presentationNode(page.id), presented)
        XCTAssertEqual(restored.treeNodes, snapshot.treeNodes)
    }

    func testLatePageWaitsForMergeTargetAndFollowsChainsWithoutCycles() async throws {
        let fixture = try await lateMergeFixture()
        let page = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Offline",
            url: "https://example.test/offline")
        let third = try await fixture.local.createWorkspace(name: "Third")
        _ = try await fixture.local.mergeWorkspace(
            fixture.target.id, into: third.id, intoFolder: false)
        var snapshot = try await fixture.local.currentSnapshot()
        snapshot.treeNodes.append(page)
        XCTAssertEqual(snapshot.presentationNode(page.id)?.workspaceID, third.id)
        snapshot.workspaces.removeAll { $0.id == third.id }
        XCTAssertEqual(snapshot.presentationNode(page.id)?.workspaceID, fixture.source.id)
        snapshot.workspaces.append(third)
        XCTAssertEqual(snapshot.presentationNode(page.id)?.workspaceID, third.id)
        let middle = try XCTUnwrap(snapshot.workspaces.firstIndex {
            $0.id == fixture.target.id
        })
        snapshot.workspaces[middle].mergedInto = fixture.source.id
        XCTAssertNil(snapshot.liveWorkspaceDestination(fixture.source.id))
        XCTAssertEqual(snapshot.presentationNode(page.id)?.workspaceID, fixture.source.id)
        snapshot.workspaces[middle].mergedInto = nil // Ordinary deletion is not a merge.
        XCTAssertEqual(snapshot.presentationNode(page.id)?.workspaceID, fixture.source.id)
        XCTAssertEqual(snapshot.treeNodes.first { $0.id == page.id }, page)
    }

    func testLateFolderKeepsItsChildrenAndAnUnknownParentDoesNotHideAPage() async throws {
        let fixture = try await lateMergeFixture()
        let folder = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .folder, title: "Offline folder")
        let child = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, parentID: folder.id, kind: .savedPage,
            title: "Offline child", url: "https://example.test/child")
        var orphan = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Missing parent",
            url: "https://example.test/orphan")
        orphan.parentID = TreeNodeID()
        _ = try await fixture.local.mergeImportedBatch([
            .init(token: 0, value: .treeNode(child)),
            .init(token: 1, value: .treeNode(orphan)),
            .init(token: 2, value: .treeNode(folder)),
        ])
        let snapshot = try await fixture.local.currentSnapshot()
        XCTAssertEqual(snapshot.presentationNode(folder.id)?.workspaceID, fixture.target.id)
        XCTAssertEqual(snapshot.presentationNode(child.id)?.workspaceID, fixture.target.id)
        XCTAssertEqual(snapshot.presentationNode(child.id)?.parentID, folder.id)
        XCTAssertNil(snapshot.presentationNode(orphan.id)?.parentID)
        XCTAssertEqual(snapshot.treeNodes.first { $0.id == orphan.id }, orphan)

        // A deliberate edit can use a projected parent; it is a real new write.
        let added = try await fixture.local.createTreeNode(
            workspaceID: fixture.target.id, parentID: folder.id, kind: .savedPage,
            title: "New child", url: "https://example.test/new")
        XCTAssertEqual(added.workspaceID, fixture.target.id)
        XCTAssertEqual(added.parentID, folder.id)
        let deleted = try await fixture.local.deleteWorkspace(fixture.target.id)
        XCTAssertEqual(Set(deleted.nodes.map(\.id)), [folder.id, child.id, orphan.id, added.id])
        XCTAssertTrue(deleted.nodes.allSatisfy(\.isDeleted))
    }

    func testUndoRestoresLatePageWithoutACompetingLocationMutation() async throws {
        let fixture = try await lateMergeFixture()
        let page = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Offline",
            url: "https://example.test/offline")
        _ = try await fixture.local.upsert(page)
        let before = try await fixture.local.currentSnapshot()
        XCTAssertEqual(before.presentationNode(page.id)?.workspaceID, fixture.target.id)
        _ = try await fixture.local.undoWorkspaceMerge(fixture.receipt)
        let after = try await fixture.local.currentSnapshot()
        XCTAssertEqual(after.presentationNode(page.id)?.workspaceID, fixture.source.id)
        XCTAssertEqual(after.treeNodes, before.treeNodes)
    }

    func testReorderAndFlatMergeMaterializeThePresentedLocation() async throws {
        let fixture = try await lateMergeFixture()
        var page = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Offline",
            url: "https://example.test/offline")
        page.parentID = TreeNodeID() // Displayed as a root after its Workspace merge.
        _ = try await fixture.local.upsert(page)
        let kept = try await fixture.local.createTreeNode(
            workspaceID: fixture.target.id, kind: .savedPage, title: "Kept",
            url: "https://example.test/kept")
        let snapshot = try await fixture.local.currentSnapshot()
        let order = snapshot.visibleTreeNodes.filter { $0.workspaceID == fixture.target.id }
        // Pick the opposite order so this is a real explicit reorder.
        let reordered = try await fixture.local.reorderTreeNode(
            page.id, before: order.first?.id == page.id ? nil : kept.id)
        XCTAssertEqual(reordered.workspaceID, fixture.target.id)
        XCTAssertNil(reordered.parentID)
        XCTAssertNotEqual(reordered.version.fieldVersions["location"],
                          page.version.fieldVersions["location"])
        let third = try await fixture.local.createWorkspace(name: "Third")
        _ = try await fixture.local.mergeWorkspace(
            fixture.target.id, into: third.id, intoFolder: false)
        let after = try await fixture.local.currentSnapshot()
        XCTAssertEqual(after.presentationNode(page.id)?.workspaceID, third.id)
        XCTAssertNil(after.presentationNode(page.id)?.parentID)
    }

    func testRenameIntentUsesProjectedBindingButKeepsRawLocationClock() async throws {
        let fixture = try await lateMergeFixture()
        let page = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Offline",
            url: "https://example.test/offline")
        _ = try await fixture.local.upsert(page)
        let tab = MobileTabRecord(
            workspaceID: fixture.target.id, treeNodeID: page.id,
            title: page.title, url: page.url, isSaved: true,
            presenceID: TabID(), sharedTarget: try SharedTabURLGroup.of(page),
            sharedBindingState: .current)
        let result = try await fixture.local.applyLocalSharedIntent(
            tab: tab, intent: .rename("Renamed"), mutationID: UUID(),
            sessionID: DeviceSessionID(), deviceName: "Test", deviceKind: .iPhone)
        let raw = try XCTUnwrap(result.outbound.nodes.first)
        XCTAssertEqual(raw.workspaceID, fixture.source.id)
        XCTAssertEqual(raw.version.fieldVersions["location"], page.version.fieldVersions["location"])
        XCTAssertEqual(raw.title, "Renamed")
        XCTAssertEqual(result.bindings[tab.id]?.workspaceID, fixture.target.id)
        let capture = try await fixture.local.captureLocalMobileTabs(
            [tab], sessionID: result.outbound.sessions[0].id,
            deviceName: "Test", deviceKind: .iPhone)
        XCTAssertEqual(capture.bindings[tab.id]?.workspaceID, fixture.target.id)
        XCTAssertTrue(capture.outbound.nodes.isEmpty)
    }

    @MainActor
    func testPassiveMergeProjectionKeepsWebPageURLAndSelection() async throws {
        let fixture = try await lateMergeFixture()
        let page = try await fixture.offline.createTreeNode(
            workspaceID: fixture.source.id, kind: .savedPage, title: "Offline",
            url: "https://example.test/offline")
        _ = try await fixture.local.upsert(page)
        let browser = MobileBrowserController(store: InMemoryMobileBrowserSessionStore())
        let id = browser.createTab(workspaceID: fixture.source.id)
        XCTAssertTrue(browser.bindTab(id, to: page))
        let runtime = try XCTUnwrap(browser.page(for: id, createIfBlank: true))
        let index = try XCTUnwrap(browser.tabs.firstIndex { $0.id == id })
        browser.tabs[index].url = "https://example.test/local-unsaved"
        let selected = browser.createTab()
        let snapshot = try await fixture.local.currentSnapshot()

        browser.reconcileSharedTabs(snapshot: snapshot)

        XCTAssertEqual(browser.selectedTabID, selected)
        XCTAssertEqual(browser.tabs[index].workspaceID, fixture.target.id)
        XCTAssertEqual(browser.tabs[index].url, "https://example.test/local-unsaved")
        XCTAssertTrue(browser.pages[id] === runtime)
        XCTAssertEqual(browser.tabs.count, 2)
        browser.close(id)
    }

    private func lateMergeFixture() async throws -> (
        local: LocalFirstRepository, offline: LocalFirstRepository,
        source: Workspace, target: Workspace, receipt: CompanionWorkspaceMergeReceipt
    ) {
        let local = makeRepository()
        let source = try await local.createWorkspace(name: "Source")
        let target = try await local.createWorkspace(name: "Target")
        let offline = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: DeviceID())
        let beforeMerge = try await local.currentSnapshot()
        try await offline.replace(beforeMerge)
        let receipt = try await local.mergeWorkspace(source.id, into: target.id, intoFolder: false)
        return (local, offline, source, target, receipt)
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
