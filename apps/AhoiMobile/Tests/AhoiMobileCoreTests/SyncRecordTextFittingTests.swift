import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Review row A6 (Crest 77457bb): writers fit records to the strictest reader,
/// `config/sync-format.json` `recordTextLimits`. Same vectors as
/// overlay/chromium/src/ahoi/browser/sync/sync_record_limits_unittest.cc.
final class SyncRecordTextFittingTests: XCTestCase {
    private let wide = "語"  // Three UTF-8 bytes.
    private let family = "👨‍👩‍👧‍👦"  // One Character of 25 UTF-8 bytes.

    func testLimitsMatchTheSharedTable() throws {
        XCTAssertEqual(SyncRecordTextFitting.maximumTitleUTF8Bytes, 1_024)
        XCTAssertEqual(SyncRecordTextFitting.maximumDeviceNameUTF8Bytes, 256)
        XCTAssertEqual(SyncRecordTextFitting.maximumWorkspaceNameUTF8Bytes, 256)
        XCTAssertEqual(SyncRecordTextFitting.maximumHistoryURLUTF8Bytes, 16_384)
        XCTAssertEqual(SyncRecordTextFitting.maximumHistoryTransitionUTF8Bytes, 128)
    }

    func testTitleVectorsAroundTheReaderLimit() {
        for size in [1_023, 1_024] {
            let title = String(repeating: "t", count: size)
            XCTAssertEqual(SyncRecordTextFitting.title(title), title)
        }
        XCTAssertEqual(SyncRecordTextFitting.title(String(repeating: "t", count: 1_025)),
                       String(repeating: "t", count: 1_024))
        XCTAssertEqual(SyncRecordTextFitting.title(""), "")
    }

    func testCutsOnlyBetweenWholeCharacters() {
        let fitted = SyncRecordTextFitting.title(String(repeating: wide, count: 342))
        XCTAssertEqual(fitted, String(repeating: wide, count: 341))
        XCTAssertEqual(fitted.utf8.count, 1_023)

        let emoji = String(repeating: "a", count: 1_022) + "😀"
        XCTAssertEqual(SyncRecordTextFitting.title(emoji), String(repeating: "a", count: 1_022))
        let exact = String(repeating: "a", count: 1_020) + "😀"
        XCTAssertEqual(SyncRecordTextFitting.title(exact), exact)

        // A grapheme cluster is never split, even where its scalars would fit.
        let families = SyncRecordTextFitting.title(String(repeating: family, count: 100))
        XCTAssertEqual(families, String(repeating: family, count: 40))
        XCTAssertEqual(families.utf8.count, 1_000)
    }

    func testNamesFitTheirOwnLimit() {
        XCTAssertEqual(SyncRecordTextFitting.deviceName(String(repeating: "n", count: 257)),
                       String(repeating: "n", count: 256))
        XCTAssertEqual(
            SyncRecordTextFitting.workspaceName(String(repeating: "n", count: 255) + "é"),
            String(repeating: "n", count: 255)
        )
        XCTAssertTrue(SyncRecordTextFitting.historyURLFits(address(ofUTF8Bytes: 16_384)))
        XCTAssertFalse(SyncRecordTextFitting.historyURLFits(address(ofUTF8Bytes: 16_385)))
    }

    /// The Mac Presence carries names borrowed from its Device and Workspace
    /// records. A long one an older writer published is fitted on read instead
    /// of quarantining every tab of that Device or Workspace.
    func testPresenceWithLongBorrowedNamesDecodes() throws {
        let codec = DesktopWirePayloadCodec()
        let deviceSample = try sample("device_mac")
        var device = try codec.decodeDevice(UnifiedSyncFixture.envelope(deviceSample),
                                            plaintext: deviceSample.data)
        device.name = String(repeating: "d", count: 300)
        let workspaceSample = try sample("workspace")
        var workspace = try codec.decodeWorkspace(UnifiedSyncFixture.envelope(workspaceSample),
                                                  plaintext: workspaceSample.data)
        workspace.name = String(repeating: wide, count: 100)
        let golden = try sample("presence_saved_web")
        let tab = try codec.decodeRemoteTab(
            UnifiedSyncFixture.envelope(golden), plaintext: golden.data,
            devices: [device.id: device], workspaces: [workspace.id: workspace]
        )
        XCTAssertEqual(tab.deviceName, String(repeating: "d", count: 256))
        XCTAssertEqual(tab.workspaceName, String(repeating: wide, count: 85))
    }

    /// Readers stay strict for a record's own fields: a history visit is
    /// rejected past the limit and accepted once its writer fitted it.
    func testHistoryReaderStaysStrictAndAcceptsFittedWriterOutput() throws {
        let codec = DesktopWirePayloadCodec()
        let golden = try sample("history_visit")
        var value = try UnifiedSyncFixture.object(golden.data)

        value["title"] = String(repeating: "t", count: 2_000)
        XCTAssertThrowsError(try codec.decodeHistory(
            UnifiedSyncFixture.envelope(golden), plaintext: UnifiedSyncFixture.canonical(value)
        )) { XCTAssertEqual($0 as? CompanionModelError, .metadataTooLarge) }

        value["title"] = SyncRecordTextFitting.title(String(repeating: "t", count: 2_000))
        let visit = try codec.decodeHistory(
            UnifiedSyncFixture.envelope(golden), plaintext: UnifiedSyncFixture.canonical(value)
        )
        XCTAssertEqual(visit.title.utf8.count, 1_024)

        value["url"] = address(ofUTF8Bytes: 20 * 1_024)
        XCTAssertThrowsError(try codec.decodeHistory(
            UnifiedSyncFixture.envelope(golden), plaintext: UnifiedSyncFixture.canonical(value)
        ))
    }

    func testMobileWritersFitWhatTheyAuthor() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore())
        let visit = try await repository.recordLocalHistoryVisit(
            title: String(repeating: wide, count: 700),
            url: "https://example.com/",
            transition: String(repeating: "x", count: 200)
        )
        XCTAssertEqual(visit.title, String(repeating: wide, count: 341))
        XCTAssertEqual(visit.transition, String(repeating: "x", count: 128))

        let created = try await repository.createWorkspace(name: String(repeating: "w", count: 300))
        XCTAssertEqual(created.name, String(repeating: "w", count: 256))
        let renamed = try await repository.updateWorkspace(
            created.id, name: String(repeating: "r", count: 257)
        )
        XCTAssertEqual(renamed.name, String(repeating: "r", count: 256))
    }

    // MARK: - Helpers

    private func sample(_ name: String) throws -> UnifiedSyncFixture.Sample {
        let (_, fixture) = try UnifiedSyncFixture.load()
        return try XCTUnwrap(fixture.records.first { $0.name == name }, name)
    }

    private func address(ofUTF8Bytes bytes: Int) -> String {
        let prefix = "https://example.com/"
        return prefix + String(repeating: "a", count: bytes - prefix.utf8.count)
    }
}
