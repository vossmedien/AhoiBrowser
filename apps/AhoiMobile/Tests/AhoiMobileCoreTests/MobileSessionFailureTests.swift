import Foundation
import XCTest
@testable import AhoiMobileCore

final class MobileSessionFailureTests: XCTestCase {
    func testFileStoreRejectsCorruptUnsupportedAndOversizedSessions() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiMobileSessionFailureTests-\(UUID())",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let fileURL = directory.appendingPathComponent("session.json")
        let store = FileMobileBrowserSessionStore(fileURL: fileURL)

        try Data("not-json".utf8).write(to: fileURL, options: .atomic)
        await assertLoadError(store, equals: .invalidSnapshot)

        let incompatible = MobileBrowserSessionSnapshot(
            schemaVersion: MobileBrowserSessionSnapshot.currentSchemaVersion + 1,
            tabs: [],
            selectedTabID: nil
        )
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .millisecondsSince1970
        try encoder.encode(incompatible).write(to: fileURL, options: .atomic)
        await assertLoadError(store, equals: .unsupportedSchema)

        FileManager.default.createFile(atPath: fileURL.path, contents: nil)
        let handle = try FileHandle(forWritingTo: fileURL)
        try handle.truncate(atOffset: 64 * 1_024 * 1_024 + 1)
        try handle.close()
        await assertLoadError(store, equals: .invalidSnapshot)
    }

    @MainActor
    func testControllerHidesRestoreAndSaveImplementationDetails() async {
        let secret = "/private/customer/session.json"
        let restoreBrowser = MobileBrowserController(
            store: FailingMobileSessionStore(failLoadWith: secret)
        )
        await restoreBrowser.load()

        XCTAssertEqual(
            restoreBrowser.lastError,
            MobileBrowserSessionFailurePresentation.restoreMessage
        )
        XCTAssertFalse(restoreBrowser.lastError?.contains(secret) == true)
        XCTAssertEqual(restoreBrowser.normalTabs.count, 1)

        let saveBrowser = MobileBrowserController(
            store: FailingMobileSessionStore(failSaveWith: secret)
        )
        await saveBrowser.load()
        _ = saveBrowser.createTab(url: URL(string: "https://example.com"))
        for _ in 0..<100 where saveBrowser.lastError == nil {
            try? await Task.sleep(for: .milliseconds(10))
        }

        XCTAssertEqual(
            saveBrowser.lastError,
            MobileBrowserSessionFailurePresentation.saveMessage
        )
        XCTAssertFalse(saveBrowser.lastError?.contains(secret) == true)
        XCTAssertEqual(saveBrowser.normalTabs.count, 2)

        saveBrowser.dismissError()
        await saveBrowser.flushSession()
        XCTAssertEqual(
            saveBrowser.lastError,
            MobileBrowserSessionFailurePresentation.saveMessage
        )
        XCTAssertFalse(saveBrowser.lastError?.contains(secret) == true)
    }

    private func assertLoadError(
        _ store: FileMobileBrowserSessionStore,
        equals expected: MobileBrowserSessionStoreError
    ) async {
        do {
            _ = try await store.load()
            XCTFail("Expected \(expected)")
        } catch {
            XCTAssertEqual(error as? MobileBrowserSessionStoreError, expected)
        }
    }
}

private actor FailingMobileSessionStore: MobileBrowserSessionStoring {
    private let loadError: NSError?
    private let saveError: NSError?

    init(failLoadWith detail: String) {
        loadError = NSError(
            domain: "AhoiMobileSessionTests",
            code: 1,
            userInfo: [NSLocalizedDescriptionKey: detail]
        )
        saveError = nil
    }

    init(failSaveWith detail: String) {
        loadError = nil
        saveError = NSError(
            domain: "AhoiMobileSessionTests",
            code: 2,
            userInfo: [NSLocalizedDescriptionKey: detail]
        )
    }

    func load() async throws -> MobileBrowserSessionSnapshot {
        if let loadError { throw loadError }
        return .empty
    }

    func save(_ snapshot: MobileBrowserSessionSnapshot) async throws {
        if let saveError { throw saveError }
    }
}
