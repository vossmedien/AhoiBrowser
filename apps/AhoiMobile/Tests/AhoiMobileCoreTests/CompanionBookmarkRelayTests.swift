import CloudKit
import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

#if DEBUG
/// Swift-domain interoperability only: delivery is the existing DEBUG
/// transport, while repository, wire codec, AES-GCM and CloudKit record
/// serialization are production implementations. This is not Chromium or a
/// Production CloudKit end-to-end result.
final class CompanionBookmarkRelayTests: XCTestCase {
    func testDisabledCategoryNeitherSeedsNorDecryptsFetchedBookmarks() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiBookmarkOptIn-\(UUID().uuidString)",
            isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let source = makePeer(at: directory.appendingPathComponent("source.json"))
        let target = makePeer(at: directory.appendingPathComponent("target.json"))
        let localOnly = try await target.repository.createBookmark(
            kind: .url,
            rootKind: .mobile,
            parentID: nil,
            title: "Local only",
            url: "https://example.test/local"
        ).bookmark

        try await target.bridge.enqueueLocalSnapshot()
        let disabledOutgoing = try await target.records.allRecords()
        XCTAssertTrue(disabledOutgoing.isEmpty)

        await source.bridge.setBookmarkSyncEnabled(true)
        let remote = try await source.repository.createBookmark(
            kind: .url,
            rootKind: .bar,
            parentID: nil,
            title: "Must not decrypt",
            url: "https://example.test/remote"
        ).bookmark
        try await source.bridge.enqueue(remote)
        let storedRemote = try await source.records.record(for: remote.id.rawValue)
        let valid = try XCTUnwrap(storedRemote)
        let corrupt = SyncRecord(
            recordID: valid.recordID,
            entityID: valid.entityID,
            schemaVersion: valid.schemaVersion,
            dataClass: valid.dataClass,
            modifiedAt: valid.modifiedAt,
            originatingDevice: valid.originatingDevice,
            encryptedValue: EncryptedValue(
                keyVersion: valid.encryptedValue.keyVersion,
                nonce: valid.encryptedValue.nonce,
                ciphertextAndTag: Data(repeating: 0xa5, count: 16)
            )
        )
        _ = try await target.records.mergeRecords(
            [corrupt],
            policy: .transportLastWriterWins
        )
        try await target.records.stageFetchedRecords([corrupt])
        try await target.bridge.importFetchedRecords()

        let snapshot = try await target.repository.currentSnapshot()
        XCTAssertEqual(snapshot.visibleBookmarks.map(\.id), [localOnly.id])
        XCTAssertFalse(snapshot.bookmarks.contains { $0.id == remote.id })
        let quarantined = await target.quarantine.allQuarantined()
        let pendingFetched = try await target.records.fetchedRecords()
        let retainedEncrypted = try await target.records.record(for: remote.id.rawValue)
        XCTAssertTrue(quarantined.isEmpty)
        XCTAssertTrue(pendingFetched.isEmpty)
        XCTAssertNotNil(retainedEncrypted)
    }

    func testTwoRepositoriesRelayBookmarksBidirectionallyAcrossOptInAndRestart() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiBookmarkRelay-\(UUID().uuidString)",
            isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let mac = makePeer(at: directory.appendingPathComponent("mac.json"))
        let phone = makePeer(at: directory.appendingPathComponent("phone.json"))
        await mac.bridge.setBookmarkSyncEnabled(true)

        let folder = try await mac.repository.createBookmark(
            kind: .folder,
            rootKind: .bar,
            parentID: nil,
            title: "Shared folder",
            url: ""
        ).bookmark
        let page = try await mac.repository.createBookmark(
            kind: .url,
            rootKind: nil,
            parentID: folder.id,
            title: "From Mac",
            url: "chrome://bookmarks/"
        ).bookmark
        try await relay(mac, to: phone)

        let disabledSnapshot = try await phone.repository.currentSnapshot()
        let cachedRecords = try await phone.records.allRecords()
        XCTAssertTrue(disabledSnapshot.bookmarks.isEmpty)
        XCTAssertEqual(Set(cachedRecords.map(\.dataClass)), [.bookmark])
        await phone.bridge.setBookmarkSyncEnabled(true)
        try await phone.bridge.importFetchedRecords()
        var phoneSnapshot = try await phone.repository.currentSnapshot()
        XCTAssertEqual(Set(phoneSnapshot.visibleBookmarks.map(\.id)), Set([folder.id, page.id]))
        XCTAssertEqual(phoneSnapshot.bookmarks.first { $0.id == page.id }?.url,
                       "chrome://bookmarks/")

        let firstSeedRecord = try await mac.records.record(for: page.id.rawValue)
        let firstSeed = try XCTUnwrap(firstSeedRecord)
        try await mac.bridge.enqueueLocalSnapshot()
        try await mac.bridge.enqueueLocalSnapshot()
        let repeatedSeedRecord = try await mac.records.record(for: page.id.rawValue)
        let repeatedSeed = try XCTUnwrap(repeatedSeedRecord)
        XCTAssertEqual(repeatedSeed, firstSeed)
        let macRecordsAfterSeed = try await mac.records.allRecords()
        XCTAssertEqual(macRecordsAfterSeed.filter { $0.dataClass == .bookmark }.count, 2)

        _ = try await phone.repository.updateBookmark(
            page.id,
            title: "Edited on iPhone",
            url: "https://example.test/phone"
        )
        let mobilePage = try await phone.repository.createBookmark(
            kind: .url,
            rootKind: .mobile,
            parentID: nil,
            title: "From iPhone",
            url: "https://example.test/mobile"
        ).bookmark
        try await relay(phone, to: mac)
        let macSnapshot = try await mac.repository.currentSnapshot()
        XCTAssertEqual(macSnapshot.bookmarks.first { $0.id == page.id }?.title,
                       "Edited on iPhone")
        XCTAssertEqual(macSnapshot.bookmarks.first { $0.id == page.id }?.url,
                       "https://example.test/phone")
        XCTAssertEqual(macSnapshot.bookmarks.filter { $0.id == page.id }.count, 1)
        XCTAssertTrue(macSnapshot.visibleBookmarks.contains { $0.id == mobilePage.id })

        let phoneRecordsBeforeRestart = try await phone.records.allRecords()
        let archivedRecords = try JSONEncoder().encode(phoneRecordsBeforeRestart)
        let restoredRecords = try JSONDecoder().decode(
            [SyncRecord].self,
            from: archivedRecords
        )
        let restartedRecordStore = InMemorySyncRecordStore(records: restoredRecords)
        let restartedPhone = makePeer(
            at: phone.fileURL,
            deviceID: phone.deviceID,
            records: restartedRecordStore
        )
        await restartedPhone.bridge.setBookmarkSyncEnabled(true)
        phoneSnapshot = try await restartedPhone.repository.currentSnapshot()
        XCTAssertEqual(Set(phoneSnapshot.bookmarks.map(\.id)), Set([folder.id, page.id, mobilePage.id]))
        try await restartedPhone.bridge.enqueueLocalSnapshot()
        let restartedRecordIDs = try await restartedPhone.records.allRecords()
            .filter { $0.dataClass == .bookmark }
            .map(\.recordID)
        let originalRecordIDs = phoneRecordsBeforeRestart
            .filter { $0.dataClass == .bookmark }
            .map(\.recordID)
        XCTAssertEqual(
            restartedRecordIDs,
            originalRecordIDs
        )
    }

    private struct RelayPeer {
        let fileURL: URL
        let deviceID: DeviceID
        let repository: LocalFirstRepository
        let records: InMemorySyncRecordStore
        let quarantine: InMemorySyncQuarantineStore
        let transport: CompanionSyncVisibleTestTransport
        let sealer: KeychainCompanionPayloadSealer
        let bridge: CompanionSyncBridge
    }

    private func makePeer(
        at fileURL: URL,
        deviceID: DeviceID = DeviceID(),
        records: InMemorySyncRecordStore = InMemorySyncRecordStore()
    ) -> RelayPeer {
        let quarantine = InMemorySyncQuarantineStore()
        let transport = CompanionSyncVisibleTestTransport(
            recordStore: records,
            quarantineStore: quarantine
        )
        let repository = LocalFirstRepository(
            store: FileCompanionStore(fileURL: fileURL),
            localDeviceID: deviceID
        )
        let sealer = KeychainCompanionPayloadSealer(
            configuration: .init(
                service: "bookmark-relay-test",
                account: "fixture",
                keyVersion: 1
            ),
            keyLoader: { Data(repeating: 0x42, count: 32) }
        )
        return RelayPeer(
            fileURL: fileURL,
            deviceID: deviceID,
            repository: repository,
            records: records,
            quarantine: quarantine,
            transport: transport,
            sealer: sealer,
            bridge: CompanionSyncBridge(
                repository: repository,
                transport: transport,
                sealer: sealer
            )
        )
    }

    private func relay(_ source: RelayPeer, to target: RelayPeer) async throws {
        try await source.bridge.enqueueLocalSnapshot()
        try await source.bridge.syncNow()
        try await deliver(try await source.records.allRecords(), to: target)
    }

    private func deliver(_ records: [SyncRecord], to target: RelayPeer) async throws {
        let codec = AppleCloudKitRecordCodec()
        let zone = CKRecordZone.ID(
            zoneName: "BookmarkRelay",
            ownerName: CKCurrentUserDefaultName
        )
        let copied = try records.map { record in
            let cloud = try codec.encode(record, zoneID: zone)
            XCTAssertNil(cloud["title"])
            XCTAssertNil(cloud["url"])
            return try codec.decode(cloud)
        }
        _ = try await target.records.mergeRecords(
            copied,
            policy: .transportLastWriterWins
        )
        try await target.records.stageFetchedRecords(copied)
        try await target.bridge.syncNow()
    }
}
#endif
