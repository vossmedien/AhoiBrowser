import Foundation
import XCTest
@testable import AhoiMobileCore

final class MobileDownloadRecoveryTests: XCTestCase {
    func testSameProcessRetryAcceptsOnlySafeNormalRequests() throws {
        let url = try XCTUnwrap(URL(string: "https://downloads.example/archive.zip"))
        var get = URLRequest(url: url)
        get.httpMethod = "GET"
        XCTAssertTrue(MobileDownloadCoordinator.isSameProcessRetryEligible(
            get,
            isPrivate: false
        ))
        XCTAssertFalse(MobileDownloadCoordinator.isSameProcessRetryEligible(
            get,
            isPrivate: true
        ))

        var head = URLRequest(url: url)
        head.httpMethod = " head "
        XCTAssertTrue(MobileDownloadCoordinator.isSameProcessRetryEligible(
            head,
            isPrivate: false
        ))

        var post = URLRequest(url: url)
        post.httpMethod = "POST"
        XCTAssertFalse(MobileDownloadCoordinator.isSameProcessRetryEligible(
            post,
            isPrivate: false
        ))

        var body = URLRequest(url: url)
        body.httpMethod = "GET"
        body.httpBody = Data("sensitive".utf8)
        XCTAssertFalse(MobileDownloadCoordinator.isSameProcessRetryEligible(
            body,
            isPrivate: false
        ))
    }

    func testConcurrentSameNameDestinationReservationsAreCollisionFree() throws {
        let fixture = try makeFixture()
        let first = MobileDownloadCoordinator.uniqueDestination(
            for: "report.pdf",
            directoryURL: fixture.downloadsURL,
            reservedPaths: []
        )
        let second = MobileDownloadCoordinator.uniqueDestination(
            for: "report.pdf",
            directoryURL: fixture.downloadsURL,
            reservedPaths: [first.standardizedFileURL.path]
        )

        XCTAssertEqual(first.lastPathComponent, "report.pdf")
        XCTAssertEqual(second.lastPathComponent, "report-2.pdf")
        XCTAssertNotEqual(first, second)
    }

    func testArchivePersistsOnlySanitizedNormalMetadata() throws {
        let fixture = try makeFixture()
        let secretError = "Authorization: Bearer header-secret; cookie-secret; body-secret"
        let normal = MobileDownloadRecord(
            sourceURL: try XCTUnwrap(URL(
                string: "https://user:password@downloads.example/private/report.pdf?token=query-secret"
            )),
            sourceOrigin: "https://user:password@downloads.example/private?token=origin-secret",
            suggestedFilename: "../../report.pdf",
            status: .downloading,
            errorMessage: secretError,
            bytesReceived: 512,
            totalBytesExpected: 1_024,
            progressFraction: 0.5,
            isPrivate: false
        )
        let privateRecord = MobileDownloadRecord(
            sourceURL: try XCTUnwrap(URL(string: "https://private.example/private-secret")),
            suggestedFilename: "private-secret.txt",
            status: .failed,
            errorMessage: "private-error-secret",
            isPrivate: true
        )

        try fixture.store.save(records: [normal, privateRecord])

        let archiveText = try String(contentsOf: fixture.recoveryURL, encoding: .utf8)
        XCTAssertTrue(archiveText.contains("downloads.example"))
        XCTAssertTrue(archiveText.contains("report.pdf"))
        for secret in [
            "user",
            "password",
            "/private",
            "query-secret",
            "origin-secret",
            "header-secret",
            "cookie-secret",
            "body-secret",
            "private.example",
            "private-secret",
            "private-error-secret",
        ] {
            XCTAssertFalse(archiveText.contains(secret), "Archive leaked \(secret)")
        }

        let restored = fixture.store.restoreRecords()
        XCTAssertEqual(restored.count, 1)
        XCTAssertEqual(restored[0].sourceOrigin, "https://downloads.example")
        XCTAssertEqual(restored[0].sourceURL.absoluteString, "https://downloads.example")
        XCTAssertEqual(restored[0].suggestedFilename, "report.pdf")
        XCTAssertEqual(restored[0].status, .failed)
        XCTAssertNil(restored[0].errorMessage)
        XCTAssertNil(restored[0].destinationURL)
        XCTAssertFalse(restored[0].isPrivate)
    }

    @MainActor
    func testInterruptedRestoreIsIdempotentAndNeverClaimsActiveOrCompleted() async throws {
        let fixture = try makeFixture()
        let active = MobileDownloadRecord(
            sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/archive.zip")),
            suggestedFilename: "archive.zip",
            status: .downloading,
            bytesReceived: 2_048,
            totalBytesExpected: 8_192,
            progressFraction: 0.25,
            isPrivate: false
        )
        try fixture.store.save(records: [active])

        let firstLaunch = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        await firstLaunch.loadRecoveryState()
        XCTAssertEqual(firstLaunch.downloads.map(\.status), [.failed])
        XCTAssertNil(firstLaunch.downloads.first?.progressFraction)
        await firstLaunch.flushRecoveryState()

        let secondLaunch = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        await secondLaunch.loadRecoveryState()
        XCTAssertEqual(secondLaunch.downloads, firstLaunch.downloads)
        XCTAssertEqual(secondLaunch.downloads.map(\.status), [.failed])
        XCTAssertFalse(secondLaunch.canRetry(active.id))
    }

    @MainActor
    func testClearRemovesRecoveryMetadataWithoutDeletingCompletedFile() async throws {
        let fixture = try makeFixture()
        try FileManager.default.createDirectory(
            at: fixture.downloadsURL,
            withIntermediateDirectories: true
        )
        let downloadedFile = fixture.downloadsURL.appendingPathComponent("report.pdf")
        try Data("complete".utf8).write(to: downloadedFile)
        let completed = MobileDownloadRecord(
            sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/report.pdf")),
            suggestedFilename: "report.pdf",
            destinationURL: downloadedFile,
            status: .completed,
            bytesReceived: 8,
            totalBytesExpected: 8,
            progressFraction: 1,
            isPrivate: false
        )
        try fixture.store.save(records: [completed])

        let coordinator = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        await coordinator.loadRecoveryState()
        XCTAssertEqual(coordinator.downloads.first?.status, .completed)
        coordinator.removeFinished(isPrivate: false)
        await coordinator.flushRecoveryState()

        XCTAssertTrue(coordinator.downloads.isEmpty)
        XCTAssertFalse(FileManager.default.fileExists(atPath: fixture.recoveryURL.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: downloadedFile.path))
    }

    func testCompletedRestoreRejectsMissingAndOutOfDirectoryDestinations() throws {
        let fixture = try makeFixture()
        let outsideURL = fixture.rootURL.appendingPathComponent("outside.pdf")
        try Data("outside".utf8).write(to: outsideURL)
        let missingInsideURL = fixture.downloadsURL.appendingPathComponent("missing.pdf")
        let records = [
            MobileDownloadRecord(
                sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/outside.pdf")),
                suggestedFilename: "outside.pdf",
                destinationURL: outsideURL,
                status: .completed,
                isPrivate: false
            ),
            MobileDownloadRecord(
                sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/missing.pdf")),
                suggestedFilename: "missing.pdf",
                destinationURL: missingInsideURL,
                status: .completed,
                isPrivate: false
            ),
        ]
        try fixture.store.save(records: records)

        let restored = fixture.store.restoreRecords()
        XCTAssertEqual(restored.count, 2)
        XCTAssertEqual(restored.map(\.status), [.failed, .failed])
        XCTAssertTrue(restored.allSatisfy { $0.destinationURL == nil })
    }

    func testHandWrittenTraversalArchiveCannotEscapeDownloadDirectory() throws {
        let fixture = try makeFixture()
        let outsideURL = fixture.rootURL.appendingPathComponent("outside.pdf")
        try Data("outside".utf8).write(to: outsideURL)
        try FileManager.default.createDirectory(
            at: fixture.recoveryURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        let id = UUID()
        let archive = """
        {"schemaVersion":1,"records":[{
          "id":"\(id.uuidString)",
          "sourceOrigin":"https://downloads.example",
          "suggestedFilename":"../evil.pdf",
          "destinationFilename":"../outside.pdf",
          "state":"completed",
          "bytesReceived":7,
          "totalBytesExpected":7
        }]}
        """
        try Data(archive.utf8).write(to: fixture.recoveryURL)

        let restored = fixture.store.restoreRecords()

        XCTAssertEqual(restored.count, 1)
        XCTAssertEqual(restored[0].id, id)
        XCTAssertEqual(restored[0].suggestedFilename, "evil.pdf")
        XCTAssertEqual(restored[0].status, .failed)
        XCTAssertNil(restored[0].destinationURL)
        XCTAssertNotEqual(restored[0].destinationURL, outsideURL)
    }

    func testCompletedRestoreRejectsSymlinkToOutsideFile() throws {
        let fixture = try makeFixture()
        try FileManager.default.createDirectory(
            at: fixture.downloadsURL,
            withIntermediateDirectories: true
        )
        let outsideURL = fixture.rootURL.appendingPathComponent("outside.pdf")
        let symlinkURL = fixture.downloadsURL.appendingPathComponent("linked.pdf")
        try Data("outside".utf8).write(to: outsideURL)
        try FileManager.default.createSymbolicLink(
            at: symlinkURL,
            withDestinationURL: outsideURL
        )
        try FileManager.default.createDirectory(
            at: fixture.recoveryURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        let archive = """
        {"schemaVersion":1,"records":[{
          "id":"\(UUID().uuidString)",
          "sourceOrigin":"https://downloads.example",
          "suggestedFilename":"linked.pdf",
          "destinationFilename":"linked.pdf",
          "state":"completed",
          "bytesReceived":7,
          "totalBytesExpected":7
        }]}
        """
        try Data(archive.utf8).write(to: fixture.recoveryURL)

        let restored = fixture.store.restoreRecords()

        XCTAssertEqual(restored.map(\.status), [.failed])
        XCTAssertNil(restored.first?.destinationURL)
    }

    func testPrivateAndCancelledRecordsNeverCreateRecoveryArchive() throws {
        let fixture = try makeFixture()
        let url = try XCTUnwrap(URL(string: "https://downloads.example/file.zip"))
        let privateRecord = MobileDownloadRecord(
            sourceURL: url,
            suggestedFilename: "private.zip",
            status: .downloading,
            isPrivate: true
        )
        let cancelled = MobileDownloadRecord(
            sourceURL: url,
            suggestedFilename: "cancelled.zip",
            status: .cancelled,
            isPrivate: false
        )

        try fixture.store.save(records: [privateRecord, cancelled])

        XCTAssertFalse(FileManager.default.fileExists(atPath: fixture.recoveryURL.path))
        XCTAssertTrue(fixture.store.restoreRecords().isEmpty)
    }

    func testLateRuntimeUpdatesCannotOverwriteCancelledOrTerminalDownload() throws {
        let sourceURL = try XCTUnwrap(URL(string: "https://downloads.example/file.zip"))
        let destinationURL = try XCTUnwrap(URL(string: "file:///tmp/file.zip"))

        for terminalStatus in [
            MobileDownloadStatus.cancelled,
            .completed,
            .failed,
        ] {
            var record = MobileDownloadRecord(
                sourceURL: sourceURL,
                suggestedFilename: "file.zip",
                status: terminalStatus,
                errorMessage: terminalStatus == .failed ? "Existing failure" : nil,
                bytesReceived: terminalStatus == .completed ? 8 : 0,
                totalBytesExpected: terminalStatus == .completed ? 8 : nil,
                progressFraction: terminalStatus == .completed ? 1 : nil,
                isPrivate: false
            )
            let terminalRecord = record

            XCTAssertFalse(record.applyRuntimeUpdate(.destination(
                filename: "late.zip",
                url: destinationURL,
                expectedContentLength: 32
            )))
            XCTAssertEqual(record, terminalRecord)
            XCTAssertFalse(record.applyRuntimeUpdate(.finished))
            XCTAssertEqual(record, terminalRecord)
            XCTAssertFalse(record.applyRuntimeUpdate(.failed(message: "Late failure")))
            XCTAssertEqual(record, terminalRecord)
        }
    }

    func testActiveDownloadRuntimeUpdatesAdvanceMonotonically() throws {
        let sourceURL = try XCTUnwrap(URL(string: "https://downloads.example/file.zip"))
        let destinationURL = try XCTUnwrap(URL(string: "file:///tmp/file.zip"))

        for activeStatus in [
            MobileDownloadStatus.starting,
            .downloading,
        ] {
            var completed = MobileDownloadRecord(
                sourceURL: sourceURL,
                suggestedFilename: "file.zip",
                status: activeStatus,
                bytesReceived: 4,
                isPrivate: false
            )
            XCTAssertTrue(completed.applyRuntimeUpdate(.destination(
                filename: "safe.zip",
                url: destinationURL,
                expectedContentLength: 8
            )))
            XCTAssertEqual(completed.destinationURL, destinationURL)
            XCTAssertEqual(completed.totalBytesExpected, 8)
            XCTAssertTrue(completed.applyRuntimeUpdate(.finished))
            XCTAssertEqual(completed.status, .completed)
            XCTAssertEqual(completed.bytesReceived, 8)
            XCTAssertEqual(completed.progressFraction, 1)
            let completedRecord = completed
            XCTAssertFalse(completed.applyRuntimeUpdate(.failed(message: "Late failure")))
            XCTAssertEqual(completed, completedRecord)

            var failed = MobileDownloadRecord(
                sourceURL: sourceURL,
                suggestedFilename: "file.zip",
                status: activeStatus,
                isPrivate: false
            )
            XCTAssertTrue(failed.applyRuntimeUpdate(.failed(message: "Transfer failed")))
            XCTAssertEqual(failed.status, .failed)
            XCTAssertEqual(failed.errorMessage, "Transfer failed")
            let failedRecord = failed
            XCTAssertFalse(failed.applyRuntimeUpdate(.finished))
            XCTAssertEqual(failed, failedRecord)
        }
    }

    func testFinishedCallbackWithoutDestinationFailsClosed() throws {
        let sourceURL = try XCTUnwrap(URL(string: "https://downloads.example/file.zip"))
        var record = MobileDownloadRecord(
            sourceURL: sourceURL,
            suggestedFilename: "file.zip",
            status: .downloading,
            bytesReceived: 8,
            totalBytesExpected: 8,
            progressFraction: 0.9,
            isPrivate: false
        )

        XCTAssertTrue(record.applyRuntimeUpdate(.finished))
        XCTAssertEqual(record.status, .failed)
        XCTAssertNotNil(record.errorMessage)
        XCTAssertNil(record.destinationURL)
        XCTAssertNil(record.progressFraction)
    }

    @MainActor
    func testOversizedArchiveIsRejectedBeforeCoordinatorPublication() async throws {
        let fixture = try makeFixture()
        try FileManager.default.createDirectory(
            at: fixture.recoveryURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try Data(repeating: 0x41, count: 300 * 1_024).write(to: fixture.recoveryURL)
        let coordinator = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )

        await coordinator.loadRecoveryState()
        await coordinator.flushRecoveryState()

        XCTAssertTrue(coordinator.downloads.isEmpty)
        XCTAssertFalse(FileManager.default.fileExists(atPath: fixture.recoveryURL.path))
    }

    func testPrivateRecordFloodCannotEvictOlderNormalRecovery() throws {
        let fixture = try makeFixture()
        let privateRecords = try (0..<140).map { index in
            MobileDownloadRecord(
                sourceURL: try XCTUnwrap(URL(string: "https://private.example/\(index)")),
                suggestedFilename: "private-\(index).zip",
                status: .downloading,
                isPrivate: true
            )
        }
        let normal = MobileDownloadRecord(
            sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/normal.zip")),
            suggestedFilename: "normal.zip",
            status: .downloading,
            isPrivate: false
        )

        try fixture.store.save(records: privateRecords + [normal])

        XCTAssertEqual(fixture.store.restoreRecords().map(\.suggestedFilename), ["normal.zip"])
    }

    @MainActor
    func testEndingPrivateSessionDropsPrivateRuntimeStateAndKeepsNormalHistory() async throws {
        let fixture = try makeFixture()
        let coordinator = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        let normalURL = try XCTUnwrap(URL(string: "https://downloads.example/normal.pdf"))
        let privateURL = try XCTUnwrap(URL(string: "https://private.example/secret.pdf"))
        coordinator.recordFailure(
            sourceURL: normalURL,
            isPrivate: false,
            message: "normal failure"
        )
        coordinator.recordFailure(
            sourceURL: privateURL,
            isPrivate: true,
            message: "private failure"
        )
        coordinator.start(
            request: URLRequest(url: privateURL),
            websiteDataStore: .nonPersistent(),
            initiatingOrigin: "https://private.example",
            isPrivate: true
        )

        XCTAssertEqual(coordinator.downloads.filter(\.isPrivate).count, 2)
        XCTAssertTrue(coordinator.downloads.filter(\.isPrivate).allSatisfy {
            !coordinator.canRetry($0.id)
        })
        coordinator.endPrivateSession()
        await coordinator.flushRecoveryState()

        XCTAssertFalse(coordinator.downloads.contains(where: \.isPrivate))
        XCTAssertEqual(coordinator.downloads.map(\.sourceURL), [normalURL])
        let archive = try String(contentsOf: fixture.recoveryURL, encoding: .utf8)
        XCTAssertFalse(archive.contains("private.example"))
        XCTAssertFalse(archive.contains("secret.pdf"))
    }

    @MainActor
    func testClosingFinalPrivateTabEndsPrivateDownloadSession() async throws {
        let fixture = try makeFixture()
        let downloads = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        let browser = MobileBrowserController(
            store: InMemoryMobileBrowserSessionStore(),
            downloadCoordinator: downloads
        )
        await browser.load()
        let privateTabID = browser.createTab(mode: .privateBrowsing)
        downloads.recordFailure(
            sourceURL: try XCTUnwrap(URL(string: "https://private.example/closed.pdf")),
            isPrivate: true,
            message: "private failure"
        )

        browser.close(privateTabID)

        XCTAssertTrue(browser.privateTabs.isEmpty)
        XCTAssertFalse(downloads.downloads.contains(where: \.isPrivate))
    }

    @MainActor
    func testClearingAllPrivateTabsEndsPrivateDownloadSession() async throws {
        let fixture = try makeFixture()
        let downloads = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        let browser = MobileBrowserController(
            store: InMemoryMobileBrowserSessionStore(),
            downloadCoordinator: downloads
        )
        await browser.load()
        _ = browser.createTab(mode: .privateBrowsing)
        _ = browser.createTab(mode: .privateBrowsing)
        downloads.recordFailure(
            sourceURL: try XCTUnwrap(URL(string: "https://private.example/clear.pdf")),
            isPrivate: true,
            message: "private failure"
        )

        browser.clearPrivateTabs()

        XCTAssertTrue(browser.privateTabs.isEmpty)
        XCTAssertFalse(downloads.downloads.contains(where: \.isPrivate))
    }

    @MainActor
    func testCorruptArchiveFailsClosedAndIsRemovedDuringCoordinatorRestore() async throws {
        let fixture = try makeFixture()
        try FileManager.default.createDirectory(
            at: fixture.recoveryURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try Data("not-json-and-not-a-request".utf8).write(to: fixture.recoveryURL)

        let coordinator = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        await coordinator.loadRecoveryState()
        await coordinator.flushRecoveryState()

        XCTAssertTrue(coordinator.downloads.isEmpty)
        XCTAssertFalse(FileManager.default.fileExists(atPath: fixture.recoveryURL.path))
    }

    @MainActor
    func testRecoveryWriteFailureIsVisibleWithoutLeakingFilesystemDetails() async throws {
        let fixture = try makeFixture()
        let blockedParent = fixture.rootURL.appendingPathComponent("blocked-parent")
        try Data("not-a-directory".utf8).write(to: blockedParent)
        let blockedStore = MobileDownloadRecoveryStore(
            fileURL: blockedParent.appendingPathComponent("normal-v1.json"),
            downloadDirectoryURL: fixture.downloadsURL
        )
        let coordinator = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: blockedStore
        )

        coordinator.recordFailure(
            sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/file.pdf")),
            isPrivate: false,
            message: "network failure"
        )
        await coordinator.flushRecoveryState()

        let message = try XCTUnwrap(coordinator.recoveryErrorMessage)
        XCTAssertFalse(message.contains(blockedParent.path))
        XCTAssertFalse(message.contains("normal-v1.json"))
    }

    @MainActor
    func testRuntimeDownloadFailuresNeverProjectUnderlyingDetails() throws {
        let secret = NSError(
            domain: "secret.webkit.internal",
            code: 7,
            userInfo: [
                NSLocalizedDescriptionKey:
                    "Authorization token-secret at /Users/person/private/report.pdf",
            ]
        )
        for kind in [
            MobileDownloadFailureKind.policy,
            .destination,
            .transfer,
        ] {
            let message = MobileDownloadFailurePresentation.message(
                for: kind,
                underlyingError: secret
            )
            XCTAssertFalse(message.contains("token-secret"))
            XCTAssertFalse(message.contains("/Users/person"))
            XCTAssertFalse(message.contains("secret.webkit.internal"))
        }

        let fixture = try makeFixture()
        let coordinator = MobileDownloadCoordinator(
            directoryURL: fixture.downloadsURL,
            recoveryStore: fixture.store
        )
        coordinator.recordFailure(
            sourceURL: try XCTUnwrap(URL(string: "https://downloads.example/file.pdf")),
            isPrivate: false,
            message: secret.localizedDescription
        )
        let projected = try XCTUnwrap(coordinator.downloads.first?.errorMessage)
        XCTAssertFalse(projected.contains("token-secret"))
        XCTAssertFalse(projected.contains("/Users/person"))
    }

    private func makeFixture() throws -> Fixture {
        let rootURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiMobileDownloadRecoveryTests-\(UUID().uuidString)")
        let downloadsURL = rootURL.appendingPathComponent("Downloads", isDirectory: true)
        let recoveryURL = rootURL
            .appendingPathComponent("ApplicationSupport", isDirectory: true)
            .appendingPathComponent("normal-v1.json")
        try FileManager.default.createDirectory(at: rootURL, withIntermediateDirectories: true)
        addTeardownBlock {
            try? FileManager.default.removeItem(at: rootURL)
        }
        return Fixture(
            rootURL: rootURL,
            downloadsURL: downloadsURL,
            recoveryURL: recoveryURL,
            store: MobileDownloadRecoveryStore(
                fileURL: recoveryURL,
                downloadDirectoryURL: downloadsURL
            )
        )
    }

    private struct Fixture {
        let rootURL: URL
        let downloadsURL: URL
        let recoveryURL: URL
        let store: MobileDownloadRecoveryStore
    }
}
