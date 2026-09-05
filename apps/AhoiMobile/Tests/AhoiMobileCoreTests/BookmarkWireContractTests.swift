import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

final class BookmarkWireContractTests: XCTestCase {
    private let windowsEpochMicroseconds: Int64 = 11_644_473_600_000_000

    func testCanonicalGoldenPayloadsRoundTripByteForByte() throws {
        let document = try goldenDocument()
        XCTAssertEqual(document["data_class"] as? String, "bookmark")
        XCTAssertEqual((document["entity_type"] as? NSNumber)?.intValue, 11)
        XCTAssertEqual(
            Set(try XCTUnwrap(document["field_names"] as? [String])),
            BookmarkRecord.syncFields
        )
        let cases = try XCTUnwrap(document["cases"] as? [[String: Any]])
        XCTAssertEqual(cases.count, 3)

        for item in cases {
            let name = try XCTUnwrap(item["name"] as? String)
            let payload = try XCTUnwrap(item["payload"] as? [String: Any])
            let expected = try canonicalData(payload)
            let record = try envelope(for: payload)
            let decoded = try DesktopWirePayloadCodec().decodeBookmark(
                record,
                plaintext: expected
            )

            XCTAssertEqual(
                try DesktopWirePayloadCodec().encode(decoded),
                expected,
                "Golden case \(name) changed bytes"
            )
            XCTAssertEqual(decoded.id.rawValue, record.entityID)
            if name == "native_url_metadata" {
                XCTAssertEqual(decoded.url, "chrome://settings/")
            }
        }
    }

    func testNativeURLMetadataAndPre1970CreationTimeRoundTripUnchanged() throws {
        for url in [
            "https://example.test/path",
            "chrome://bookmarks/",
            "about:blank",
            "file:///tmp/guide.html",
            "javascript:alert(1)",
            "data:text/plain,hello",
        ] {
            var bookmark = try makeBookmark(url: url)
            bookmark.createdAt = 1
            try bookmark.validate()
            let payload = try DesktopWirePayloadCodec().encode(bookmark)
            let decoded = try DesktopWirePayloadCodec().decodeBookmark(
                envelope(for: bookmark),
                plaintext: payload
            )
            XCTAssertEqual(decoded.url, url)
            XCTAssertEqual(decoded.createdAt, 1)
            XCTAssertEqual(try DesktopWirePayloadCodec().encode(decoded), payload)
        }
    }

    func testDefaultCreationTimeUsesCheckedWindowsEpochMicroseconds() throws {
        let bookmark = try makeBookmark()
        let defaulted = try BookmarkRecord(
            bookmarkID: BookmarkID(),
            kind: .url,
            rootKind: .bar,
            sortKey: "A",
            title: "Default clock",
            url: "https://example.test/default",
            version: bookmark.version
        )
        XCTAssertEqual(
            defaulted.createdAt,
            windowsEpochMicroseconds + 1_000_100
        )

        let device = bookmark.version.modifiedBy
        let overflowingClock = HybridLogicalClock(
            physicalMilliseconds: UInt64.max,
            nodeID: device
        )
        let overflowingVersion = SyncVersion(
            modifiedAt: overflowingClock,
            modifiedBy: device
        )
        XCTAssertThrowsError(try BookmarkRecord(
            kind: .url,
            rootKind: .bar,
            sortKey: "A",
            title: "Overflow",
            url: "https://example.test/overflow",
            version: overflowingVersion
        ))
    }

    func testModelRejectsInvalidIdentityLocationMetadataAndClocks() throws {
        let bookmark = try makeBookmark()
        let zeroID = BookmarkID(rawValue: zeroUUID)

        XCTAssertThrowsError(try BookmarkRecord(
            bookmarkID: zeroID,
            kind: .url,
            rootKind: .bar,
            sortKey: "A",
            title: "Zero",
            url: "https://example.test",
            version: bookmark.version
        ))
        XCTAssertThrowsError(try replacingLocation(bookmark, root: nil, parent: nil))
        XCTAssertThrowsError(try replacingLocation(
            bookmark,
            root: .bar,
            parent: BookmarkID()
        ))
        XCTAssertThrowsError(try replacingLocation(
            bookmark,
            root: nil,
            parent: bookmark.id
        ))

        for sortKey in ["", " ", "a b", "\t", "\n", "\u{7f}", "ä"] {
            var invalid = bookmark
            invalid.sortKey = sortKey
            XCTAssertThrowsError(try invalid.validate())
        }
        var invalid = bookmark
        invalid.sortKey = String(repeating: "~", count: 1_025)
        XCTAssertThrowsError(try invalid.validate())
        invalid = bookmark
        invalid.title = String(repeating: "x", count: 65_537)
        XCTAssertThrowsError(try invalid.validate())
        invalid = bookmark
        invalid.title = "a\0b"
        XCTAssertThrowsError(try invalid.validate())
        invalid = bookmark
        invalid.url = "https://user:secret@example.test/"
        XCTAssertThrowsError(try invalid.validate())
        invalid.url = "not a url"
        XCTAssertThrowsError(try invalid.validate())
        invalid = try makeBookmark(kind: .folder, url: "")
        invalid.url = "https://example.test/"
        XCTAssertThrowsError(try invalid.validate())
        invalid = bookmark
        invalid.createdAt = 0
        XCTAssertThrowsError(try invalid.validate())

        let oldVersion = SyncVersion(
            schemaVersion: 1,
            modifiedAt: bookmark.version.modifiedAt,
            modifiedBy: bookmark.version.modifiedBy
        )
        XCTAssertThrowsError(try replacingVersion(bookmark, version: oldVersion))
        var unknownClock = bookmark.version
        unknownClock.fieldVersions["parent_id"] = unknownClock.modifiedAt
        XCTAssertThrowsError(try replacingVersion(bookmark, version: unknownClock))
        var futureClock = bookmark.version
        futureClock.fieldVersions["title"] = HybridLogicalClock(
            physicalMilliseconds: futureClock.modifiedAt.physicalMilliseconds + 1,
            nodeID: futureClock.modifiedBy
        )
        XCTAssertThrowsError(try replacingVersion(bookmark, version: futureClock))
    }

    func testCodecRejectsNullConflictingAndMalformedLocationMetadata() throws {
        let bookmark = try makeBookmark()
        let codec = DesktopWirePayloadCodec()
        let record = envelope(for: bookmark)
        let original = try codec.object(from: codec.encode(bookmark))

        var invalid = original
        invalid.removeValue(forKey: "root_kind")
        assertRejected(invalid, record: record)
        invalid = original
        invalid["parent_id"] = BookmarkID().rawValue.uuidString.lowercased()
        assertRejected(invalid, record: record)
        invalid = original
        invalid["root_kind"] = NSNull()
        assertRejected(invalid, record: record)
        invalid = original
        invalid.removeValue(forKey: "root_kind")
        invalid["parent_id"] = NSNull()
        assertRejected(invalid, record: record)
        invalid = original
        invalid["root_kind"] = true
        assertRejected(invalid, record: record)
        invalid = original
        invalid["kind"] = 2
        assertRejected(invalid, record: record)

        let integerPayload = String(decoding: try codec.encode(bookmark), as: UTF8.self)
        let floatingKind = integerPayload.replacingOccurrences(
            of: #""kind":1"#,
            with: #""kind":1.0"#
        )
        XCTAssertThrowsError(try codec.decodeBookmark(
            record,
            plaintext: Data(floatingKind.utf8)
        ))
    }

    func testCodecRejectsLengthURLAndCreationMetadataViolations() throws {
        let bookmark = try makeBookmark()
        let codec = DesktopWirePayloadCodec()
        let record = envelope(for: bookmark)
        let original = try codec.object(from: codec.encode(bookmark))

        var invalid = original
        invalid["sort_key"] = ""
        assertRejected(invalid, record: record)
        invalid = original
        invalid["sort_key"] = String(repeating: "~", count: 1_025)
        assertRejected(invalid, record: record)
        invalid = original
        invalid["title"] = String(repeating: "x", count: 65_537)
        assertRejected(invalid, record: record)
        invalid = original
        invalid["url"] = String(repeating: "x", count: 131_073)
        assertRejected(invalid, record: record)
        invalid = original
        invalid["url"] = "https://user@example.test/"
        assertRejected(invalid, record: record)
        invalid = original
        invalid["url"] = "https://example.test/a\0b"
        assertRejected(invalid, record: record)
        invalid = original
        invalid["kind"] = BookmarkKind.folder.rawValue
        assertRejected(invalid, record: record)
        for createdAt in ["0", "-1", "01", "1e3", "9223372036854775808"] {
            invalid = original
            invalid["created_at"] = createdAt
            assertRejected(invalid, record: record)
        }
    }

    func testCodecRejectsWrongFieldClocksAndEnvelopeMetadata() throws {
        let bookmark = try makeBookmark()
        let codec = DesktopWirePayloadCodec()
        let record = envelope(for: bookmark)
        let original = try codec.object(from: codec.encode(bookmark))

        var invalid = original
        var fields = try XCTUnwrap(invalid["field_versions"] as? [String: Any])
        fields.removeValue(forKey: "title")
        invalid["field_versions"] = fields
        assertRejected(invalid, record: record)
        invalid = original
        fields = try XCTUnwrap(invalid["field_versions"] as? [String: Any])
        fields["unknown"] = fields["title"]
        invalid["field_versions"] = fields
        assertRejected(invalid, record: record)
        invalid = original
        fields = try XCTUnwrap(invalid["field_versions"] as? [String: Any])
        var titleClock = try XCTUnwrap(fields["title"] as? [String: Any])
        let physicalText = try XCTUnwrap(titleClock["physical"] as? String)
        let physical = try XCTUnwrap(Int64(physicalText))
        titleClock["physical"] = String(physical + 1)
        fields["title"] = titleClock
        invalid["field_versions"] = fields
        assertRejected(invalid, record: record)
        invalid = original
        fields = try XCTUnwrap(invalid["field_versions"] as? [String: Any])
        titleClock = try XCTUnwrap(fields["title"] as? [String: Any])
        titleClock["device"] = "not-a-uuid"
        fields["title"] = titleClock
        invalid["field_versions"] = fields
        assertRejected(invalid, record: record)

        let otherID = UUID()
        let wrongEnvelope = SyncRecord(
            recordID: otherID,
            entityID: otherID,
            schemaVersion: record.schemaVersion,
            dataClass: .bookmark,
            modifiedAt: record.modifiedAt,
            originatingDevice: record.originatingDevice,
            encryptedValue: record.encryptedValue
        )
        XCTAssertThrowsError(try codec.decodeBookmark(
            wrongEnvelope,
            plaintext: codec.encode(bookmark)
        ))
    }

    func testSyncBoundaryAllowsBookmarkAndValidatesItsTombstone() throws {
        let original = try makeBookmark()
        let clock = original.version.modifiedAt
        let deletedBy = original.version.modifiedBy
        let tombstone = Tombstone(
            entityID: original.id.rawValue,
            deletedAt: clock,
            deletedBy: deletedBy,
            originalParentID: nil,
            originalOrderKey: nil,
            purgeAfterMilliseconds: clock.physicalMilliseconds + 1
        )
        let deleted = try BookmarkRecord(
            bookmarkID: original.id,
            kind: original.kind,
            rootKind: original.rootKind,
            parentID: original.parentID,
            sortKey: original.sortKey,
            title: original.title,
            url: original.url,
            createdAt: original.createdAt,
            version: original.version,
            tombstone: tombstone
        )
        let record = envelope(for: deleted, tombstone: tombstone)

        XCTAssertEqual(SyncBoundary().disposition(for: .bookmark), .allowed)
        XCTAssertNoThrow(try SyncBoundary().authorize(record))
        let payload = try DesktopWirePayloadCodec().encode(deleted)
        XCTAssertEqual(
            try DesktopWirePayloadCodec().decodeBookmark(record, plaintext: payload),
            deleted
        )
    }

    private func goldenDocument() throws -> [String: Any] {
        let url = try XCTUnwrap(
            Bundle(for: Self.self).url(
                forResource: "bookmark_wire_v2",
                withExtension: "json"
            ),
            "bookmark_wire_v2.json must be a test-bundle resource"
        )
        return try XCTUnwrap(
            JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any]
        )
    }

    private func canonicalData(_ value: [String: Any]) throws -> Data {
        try JSONSerialization.data(
            withJSONObject: value,
            options: [.sortedKeys, .withoutEscapingSlashes]
        )
    }

    private func makeBookmark(
        kind: BookmarkKind = .url,
        url: String = "https://example.test/%C3%BCberblick?q=%E6%B5%B7#section"
    ) throws -> BookmarkRecord {
        let device = DeviceID(
            rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000001")!
        )
        let clock = HybridLogicalClock(
            physicalMilliseconds: 1_000,
            submillisecondMicroseconds: 100,
            logicalCounter: 8,
            nodeID: device
        )
        let fields = Dictionary(uniqueKeysWithValues:
            BookmarkRecord.syncFields.map { ($0, clock) }
        )
        return try BookmarkRecord(
            bookmarkID: BookmarkID(
                rawValue: UUID(uuidString: "90000000-0000-4000-8000-000000000001")!
            ),
            kind: kind,
            rootKind: .bar,
            sortKey: "M",
            title: "Überblick — 海 ⚓",
            url: url,
            createdAt: windowsEpochMicroseconds + 123,
            version: SyncVersion(
                modifiedAt: clock,
                modifiedBy: device,
                fieldVersions: fields
            )
        )
    }

    private func replacingLocation(
        _ bookmark: BookmarkRecord,
        root: BookmarkRoot?,
        parent: BookmarkID?
    ) throws -> BookmarkRecord {
        try BookmarkRecord(
            bookmarkID: bookmark.id,
            kind: bookmark.kind,
            rootKind: root,
            parentID: parent,
            sortKey: bookmark.sortKey,
            title: bookmark.title,
            url: bookmark.url,
            createdAt: bookmark.createdAt,
            version: bookmark.version,
            tombstone: bookmark.tombstone
        )
    }

    private func replacingVersion(
        _ bookmark: BookmarkRecord,
        version: SyncVersion
    ) throws -> BookmarkRecord {
        try BookmarkRecord(
            bookmarkID: bookmark.id,
            kind: bookmark.kind,
            rootKind: bookmark.rootKind,
            parentID: bookmark.parentID,
            sortKey: bookmark.sortKey,
            title: bookmark.title,
            url: bookmark.url,
            createdAt: bookmark.createdAt,
            version: version,
            tombstone: bookmark.tombstone
        )
    }

    private func envelope(
        for bookmark: BookmarkRecord,
        tombstone: Tombstone? = nil
    ) -> SyncRecord {
        SyncRecord(
            recordID: bookmark.id.rawValue,
            entityID: bookmark.id.rawValue,
            schemaVersion: bookmark.version.schemaVersion,
            dataClass: .bookmark,
            modifiedAt: bookmark.version.modifiedAt,
            originatingDevice: bookmark.version.modifiedBy,
            encryptedValue: encryptedValue(),
            tombstone: tombstone
        )
    }

    private func envelope(for payload: [String: Any]) throws -> SyncRecord {
        let id = try XCTUnwrap(UUID(uuidString: try XCTUnwrap(payload["id"] as? String)))
        let rawPhysical = try XCTUnwrap(Int64(
            try XCTUnwrap(payload["version_physical"] as? String)
        ))
        guard rawPhysical >= windowsEpochMicroseconds else {
            throw TestContractError.invalidGoldenClock
        }
        let deviceString = try XCTUnwrap(payload["version_device"] as? String)
        let deviceUUID = try XCTUnwrap(
            UUID(uuidString: deviceString),
            "Golden clock devices must be canonical UUID strings"
        )
        let device = DeviceID(rawValue: deviceUUID)
        let unixMicroseconds = UInt64(rawPhysical - windowsEpochMicroseconds)
        let logicalNumber = try XCTUnwrap(payload["version_logical"] as? NSNumber)
        let logical = try XCTUnwrap(UInt32(exactly: logicalNumber.int64Value))
        let clock = HybridLogicalClock(
            physicalMilliseconds: unixMicroseconds / 1_000,
            submillisecondMicroseconds: UInt16(unixMicroseconds % 1_000),
            logicalCounter: logical,
            nodeID: device
        )
        return SyncRecord(
            recordID: id,
            entityID: id,
            schemaVersion: 2,
            dataClass: .bookmark,
            modifiedAt: clock,
            originatingDevice: device,
            encryptedValue: encryptedValue()
        )
    }

    private func encryptedValue() -> EncryptedValue {
        EncryptedValue(
            keyVersion: 1,
            nonce: Data(repeating: 0, count: 12),
            ciphertextAndTag: Data(repeating: 0, count: 16)
        )
    }

    private func assertRejected(
        _ value: [String: Any],
        record: SyncRecord,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        XCTAssertThrowsError(
            try DesktopWirePayloadCodec().decodeBookmark(
                record,
                plaintext: canonicalData(value)
            ),
            file: file,
            line: line
        )
    }

    private var zeroUUID: UUID {
        UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    }

    private enum TestContractError: Error {
        case invalidGoldenClock
    }
}
