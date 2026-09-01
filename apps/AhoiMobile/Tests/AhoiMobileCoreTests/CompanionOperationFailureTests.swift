import XCTest
@testable import AhoiMobileCore

final class CompanionOperationFailureTests: XCTestCase {
    @MainActor
    func testFailingStoreProjectsStableRecoveryWithoutSensitiveDetails() async throws {
        let secret = SensitiveStoreError(
            detail: "token-secret at /Users/person/Library/Ahoi/private.json"
        )
        let model = CompanionAppModel(
            repository: LocalFirstRepository(
                store: FailingCompanionStore(error: secret)
            )
        )

        await model.load()

        let message = try XCTUnwrap(model.loadError)
        XCTAssertFalse(message.contains("token-secret"))
        XCTAssertFalse(message.contains("/Users/person"))
        XCTAssertFalse(message.contains("private.store.failure"))
        model.dismissLoadError()
        XCTAssertNil(model.loadError)
    }

    @MainActor
    func testOutboundQueueFailureKeepsTheCommittedLocalMutationVisibleAndUnique() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let model = CompanionAppModel(repository: repository)
        await model.load()
        let secret = SensitiveStoreError(
            detail: "sync-token-secret at /Users/person/Library/Ahoi/outbox.json"
        )

        let workspace = await model.performLocalFirstMutation({
            try await repository.createWorkspace(name: "Offline Harbor")
        }, enqueue: { _ in
            throw secret
        })

        let committed = try XCTUnwrap(workspace)
        XCTAssertEqual(model.snapshot.visibleWorkspaces.map(\.id), [committed.id])
        XCTAssertTrue(model.localSnapshotReseedRequired)
        let message = try XCTUnwrap(model.loadError)
        XCTAssertEqual(
            message,
            CompanionOperationFailurePresentation.syncQueueMessage(for: secret)
        )
        XCTAssertFalse(message.contains("sync-token-secret"))
        XCTAssertFalse(message.contains("/Users/person"))

        await model.load()

        XCTAssertEqual(model.snapshot.visibleWorkspaces.map(\.id), [committed.id])
        XCTAssertEqual(
            model.searchResults.filter { $0.id == committed.id.rawValue }.count,
            1,
            "Reloading local state after a queue failure must not replay the mutation."
        )
    }
}

private actor FailingCompanionStore: LocalCompanionStore {
    let error: SensitiveStoreError

    init(error: SensitiveStoreError) {
        self.error = error
    }

    func load() async throws -> CompanionSnapshot {
        throw error
    }

    func save(_ snapshot: CompanionSnapshot) async throws {
        _ = snapshot
        throw error
    }
}

private struct SensitiveStoreError: Error, LocalizedError, Sendable {
    let detail: String

    var errorDescription: String? {
        "private.store.failure: \(detail)"
    }
}
