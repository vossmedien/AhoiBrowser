import Foundation
import AhoiCloudKitSpike

/// Reproducible provider-independent backend for unit tests and SwiftUI
/// previews. It applies the same boundary and deterministic conflict order as
/// the real CloudKit provider, but never creates a CKContainer or uses a
/// network connection.
public actor InMemoryCloudSyncBackend: CloudRecordTransport {
    private let boundary: SyncBoundary
    private var zoneReady = false
    private var records: [UUID: SyncRecord]

    public init(
        boundary: SyncBoundary = .init(),
        records: [SyncRecord] = []
    ) {
        self.boundary = boundary
        self.records = [:]
        for record in records {
            self.records[record.recordID] = record
        }
    }

    public func ensureCustomZone() async throws {
        zoneReady = true
    }

    public func save(_ values: [SyncRecord]) async throws {
        guard zoneReady else { throw InMemoryCloudSyncBackendError.zoneNotPrepared }
        for value in values {
            try boundary.authorize(value)
            if let existing = records[value.recordID] {
                records[value.recordID] = Self.resolve(existing, value)
            } else {
                records[value.recordID] = value
            }
        }
    }

    public func fetch(recordIDs: [UUID]) async throws -> [SyncRecord] {
        guard zoneReady else { throw InMemoryCloudSyncBackendError.zoneNotPrepared }
        return recordIDs.compactMap { records[$0] }
    }

    public func allRecords() -> [SyncRecord] {
        records.values.sorted { $0.recordID.uuidString < $1.recordID.uuidString }
    }

    private static func resolve(_ lhs: SyncRecord, _ rhs: SyncRecord) -> SyncRecord {
        guard let left = try? VersionedValue(
            value: lhs.encryptedValue,
            modifiedAt: lhs.modifiedAt,
            originatingDevice: lhs.originatingDevice,
            isTombstone: lhs.tombstone != nil
        ), let right = try? VersionedValue(
            value: rhs.encryptedValue,
            modifiedAt: rhs.modifiedAt,
            originatingDevice: rhs.originatingDevice,
            isTombstone: rhs.tombstone != nil
        ) else {
            return lhs.modifiedAt < rhs.modifiedAt ? rhs : lhs
        }
        return LastWriterWinsResolver().resolve(left, right) == right ? rhs : lhs
    }
}

public enum InMemoryCloudSyncBackendError: Error, Equatable, Sendable {
    case zoneNotPrepared
}
