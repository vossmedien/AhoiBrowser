import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class CompanionWorkspaceRetentionTests: XCTestCase {
    private enum SaveFailure: Error { case simulated }

    private actor FailingStore: LocalCompanionStore {
        private var value: CompanionSnapshot = .empty
        private var failNext = false

        func load() async throws -> CompanionSnapshot { value }
        func save(_ snapshot: CompanionSnapshot) async throws {
            if failNext {
                failNext = false
                throw SaveFailure.simulated
            }
            value = snapshot
        }
        func failNextSave() { failNext = true }
    }

    func testExpiryExactSourceAndAtomicRouteSurviveReload() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiWorkspaceCompaction-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let url = directory.appendingPathComponent("snapshot.json")
        let repository = LocalFirstRepository(store: FileCompanionStore(fileURL: url))
        let source = try await repository.createWorkspace(name: "é")
        let target = try await repository.createWorkspace(name: "Target")
        let offline = LocalFirstRepository(store: InMemoryCompanionStore(
            snapshot: try await repository.currentSnapshot()))
        let merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let late = try await offline.createTreeNode(
            workspaceID: source.id, kind: .savedPage, title: "Late",
            url: "https://example.test/late")
        _ = try await repository.upsert(late)
        let raw = try DesktopWirePayloadCodec().encode(late)
        let expiry = try XCTUnwrap(merged.tombstone?.purgeAfterMilliseconds)

        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: merged, nowMilliseconds: expiry - 1)
            XCTFail("An unexpired tombstone cannot be compacted")
        } catch {
            XCTAssertEqual(error as? CompanionWorkspaceCompactionError, .notExpiredTombstone)
        }
        var changed = merged
        changed.name = "e\u{301}" // Swift equality can fold this; wire bytes cannot.
        XCTAssertEqual(changed.name, merged.name)
        XCTAssertNotEqual(Data(changed.name.utf8), Data(merged.name.utf8))
        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: changed, nowMilliseconds: expiry)
            XCTFail("The domain record must match the upload candidate exactly")
        } catch {
            XCTAssertEqual(error as? CompanionWorkspaceCompactionError, .sourceChanged)
        }
        let first = try await repository.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: expiry)
        XCTAssertTrue(first)
        let restored = LocalFirstRepository(store: FileCompanionStore(fileURL: url))
        let snapshot = try await restored.currentSnapshot()
        XCTAssertFalse(snapshot.workspaces.contains { $0.id == source.id })
        XCTAssertEqual(snapshot.deletionWatermarks.count, 1)
        XCTAssertEqual(snapshot.deletionWatermarks[0].version, merged.version)
        XCTAssertEqual(snapshot.deletionWatermarks[0].mergedInto, target.id)
        XCTAssertEqual(snapshot.presentationNode(late.id)?.workspaceID, target.id)
        let persistedRaw = try DesktopWirePayloadCodec().encode(
            XCTUnwrap(snapshot.treeNodes.first { $0.id == late.id }))
        XCTAssertEqual(persistedRaw, raw)
        let repeated = try await restored.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: expiry)
        XCTAssertFalse(repeated)
    }

    func testFailedAtomicSaveRetainsPayloadUntilRetry() async throws {
        let store = FailingStore()
        let repository = LocalFirstRepository(store: store)
        let source = try await repository.createWorkspace(name: "Source")
        let target = try await repository.createWorkspace(name: "Target")
        let merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let expiry = try XCTUnwrap(merged.tombstone?.purgeAfterMilliseconds)
        await store.failNextSave()
        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: merged, nowMilliseconds: expiry)
            XCTFail("Injected store failure must stop compaction")
        } catch {
            XCTAssertTrue(error is SaveFailure)
        }
        let afterFailure = try await repository.currentSnapshot()
        XCTAssertEqual(afterFailure.workspaces.first { $0.id == source.id }, merged)
        XCTAssertTrue(afterFailure.deletionWatermarks.isEmpty)
        let persisted = try await store.load()
        XCTAssertEqual(persisted, afterFailure)
        let retried = try await repository.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: expiry)
        XCTAssertTrue(retried)
    }

    func testShortRemotePurgeAfterCannotReduceThirtyDayFloor() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let source = try await repository.createWorkspace(name: "Source")
        let target = try await repository.createWorkspace(name: "Target")
        var merged = try await repository.mergeWorkspace(
            source.id, into: target.id, intoFolder: false).source
        let original = try XCTUnwrap(merged.tombstone)
        merged.tombstone = Tombstone(
            entityID: original.entityID, deletedAt: original.deletedAt,
            deletedBy: original.deletedBy, originalParentID: original.originalParentID,
            originalOrderKey: original.originalOrderKey,
            purgeAfterMilliseconds: original.deletedAt.physicalMilliseconds + 1)
        var snapshot = try await repository.currentSnapshot()
        let index = try XCTUnwrap(snapshot.workspaces.firstIndex { $0.id == source.id })
        snapshot.workspaces[index] = merged
        try await repository.replace(snapshot)
        do {
            _ = try await repository.compactWorkspaceTombstone(
                matching: merged, nowMilliseconds: original.deletedAt.physicalMilliseconds + 1)
            XCTFail("A shortened remote purge hint cannot bypass 30 days")
        } catch {
            XCTAssertEqual(error as? CompanionWorkspaceCompactionError, .notExpiredTombstone)
        }
        let floor = original.deletedAt.physicalMilliseconds + 30 * 24 * 60 * 60 * 1_000
        let compacted = try await repository.compactWorkspaceTombstone(
            matching: merged, nowMilliseconds: floor)
        XCTAssertTrue(compacted)
    }
}
