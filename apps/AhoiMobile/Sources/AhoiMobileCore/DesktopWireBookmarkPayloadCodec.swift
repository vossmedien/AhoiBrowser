import Foundation
import AhoiCloudKitSpike

extension DesktopWirePayloadCodec {
    public func encode(_ bookmark: BookmarkRecord) throws -> Data {
        try bookmark.validate()
        var value = try common(
            id: bookmark.id.rawValue,
            tombstone: bookmark.isDeleted,
            version: bookmark.version,
            fields: BookmarkRecord.syncFields
        )
        value["kind"] = bookmark.kind.rawValue
        if let rootKind = bookmark.rootKind {
            value["root_kind"] = rootKind.rawValue
        } else if let parentID = bookmark.parentID {
            value["parent_id"] = uuid(parentID.rawValue)
        } else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        value["sort_key"] = bookmark.sortKey
        value["title"] = bookmark.title
        value["url"] = bookmark.url
        value["created_at"] = String(bookmark.createdAt)
        return try serialize(value)
    }

    public func decodeBookmark(
        _ record: SyncRecord,
        plaintext: Data
    ) throws -> BookmarkRecord {
        let value = try object(from: plaintext)
        try validateBookmarkCommonTypes(value)
        let resultVersion = try version(
            value,
            requiredFields: BookmarkRecord.syncFields
        )
        let bookmarkID = BookmarkID(rawValue: try canonicalUUID(value, "id"))
        let payloadDeleted = try strictBoolean(value, "tombstone")
        guard record.dataClass == .bookmark,
              record.schemaVersion == SharedSyncFormat.currentVersion,
              record.recordID == bookmarkID.rawValue,
              record.entityID == bookmarkID.rawValue,
              record.orderKey == nil,
              record.modifiedAt == resultVersion.modifiedAt,
              record.originatingDevice == resultVersion.modifiedBy,
              payloadDeleted == (record.tombstone != nil) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }

        let kindValue = try strictInteger(value, "kind")
        guard let kind = BookmarkKind(rawValue: kindValue) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let hasRoot = value.keys.contains("root_kind")
        let hasParent = value.keys.contains("parent_id")
        guard hasRoot != hasParent else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let rootKind: BookmarkRoot?
        let parentID: BookmarkID?
        if hasRoot {
            guard let decodedRoot = BookmarkRoot(
                rawValue: try strictInteger(value, "root_kind")
            ) else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            rootKind = decodedRoot
            parentID = nil
        } else {
            rootKind = nil
            parentID = BookmarkID(rawValue: try canonicalUUID(value, "parent_id"))
        }

        let bookmark = try BookmarkRecord(
            bookmarkID: bookmarkID,
            kind: kind,
            rootKind: rootKind,
            parentID: parentID,
            sortKey: try string(value, "sort_key"),
            title: try string(value, "title"),
            url: try string(value, "url"),
            createdAt: try positiveDecimalInt64(value, "created_at"),
            version: resultVersion,
            tombstone: try tombstone(record, value: value)
        )
        try bookmark.validate()
        return bookmark
    }

    private func validateBookmarkCommonTypes(_ value: [String: Any]) throws {
        let modelVersion = try strictInteger(value, "model_version")
        let versionModel = try strictInteger(value, "version_model")
        guard modelVersion == Int(SharedSyncFormat.currentVersion),
              versionModel == Int(SharedSyncFormat.currentVersion) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        _ = try strictUInt32(value, "version_logical")
        _ = try strictBoolean(value, "tombstone")
        _ = try canonicalUUID(value, "id")
        _ = try canonicalUUID(value, "version_device")
        _ = try unixRepresentableClockString(value, "version_physical")

        guard let fields = value["field_versions"] as? [String: Any],
              Set(fields.keys) == BookmarkRecord.syncFields else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        for field in BookmarkRecord.syncFields {
            guard let stamp = fields[field] as? [String: Any] else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            _ = try strictUInt32(stamp, "logical")
            _ = try canonicalUUID(stamp, "device")
            _ = try unixRepresentableClockString(stamp, "physical")
        }
    }

    private func strictBoolean(_ value: [String: Any], _ key: String) throws -> Bool {
        try SharedTabWireReadPolicy.strictBoolean(value, key: key)
    }

    private func strictInteger(_ value: [String: Any], _ key: String) throws -> Int {
        try SharedTabWireReadPolicy.strictInteger(value, key: key)
    }

    private func strictUInt32(_ value: [String: Any], _ key: String) throws -> UInt32 {
        try SharedTabWireReadPolicy.strictUInt32(value, key: key)
    }

    private func canonicalUUID(_ value: [String: Any], _ key: String) throws -> UUID {
        try SharedTabWireReadPolicy.strictUUID(value, key: key)
    }

    private func positiveDecimalInt64(
        _ value: [String: Any],
        _ key: String
    ) throws -> Int64 {
        let raw = try string(value, key)
        guard !raw.isEmpty, raw.utf8.allSatisfy({ $0 >= 0x30 && $0 <= 0x39 }),
              let result = Int64(raw), result > 0, String(result) == raw else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    private func unixRepresentableClockString(
        _ value: [String: Any],
        _ key: String
    ) throws -> Int64 {
        let raw = try string(value, key)
        guard !raw.isEmpty, raw.utf8.allSatisfy({ $0 >= 0x30 && $0 <= 0x39 }),
              let result = Int64(raw),
              result >= Self.windowsToUnixMicroseconds,
              String(result) == raw else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

}
