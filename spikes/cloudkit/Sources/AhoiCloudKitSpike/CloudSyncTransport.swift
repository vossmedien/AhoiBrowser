import Foundation

public protocol CloudRecordTransport: Sendable {
    func ensureCustomZone() async throws
    func save(_ records: [SyncRecord]) async throws
    func fetch(recordIDs: [UUID]) async throws -> [SyncRecord]
}

/// The coordinator is the only supported sync entrance. It enforces the
/// allow/deny policy before uploads and again before fetched records reach the
/// rest of the application.
public actor CloudSyncCoordinator {
    private let transport: any CloudRecordTransport
    private let boundary: SyncBoundary

    public init(
        transport: any CloudRecordTransport,
        boundary: SyncBoundary = .init()
    ) {
        self.transport = transport
        self.boundary = boundary
    }

    public func prepare() async throws {
        try await transport.ensureCustomZone()
    }

    public func upload(
        _ records: [SyncRecord],
        authorization: SyncAuthorizationContext = .init()
    ) async throws {
        for record in records {
            try boundary.authorize(record, context: authorization)
        }
        try await transport.save(records)
    }

    public func fetch(
        recordIDs: [UUID],
        authorization: SyncAuthorizationContext = .init()
    ) async throws -> [SyncRecord] {
        let records = try await transport.fetch(recordIDs: recordIDs)
        for record in records {
            try boundary.authorize(record, context: authorization)
        }
        return records
    }

    /// User-visible deletion is exclusively a validated tombstone write. This
    /// spike deliberately exposes no raw CloudKit-record deletion operation.
    public func writeTombstone(
        _ record: SyncRecord,
        authorization: SyncAuthorizationContext = .init()
    ) async throws {
        guard record.dataClass == .tombstone else {
            throw SyncBoundaryError.invalidTombstone
        }
        try boundary.authorize(record, context: authorization)
        try await transport.save([record])
    }
}
