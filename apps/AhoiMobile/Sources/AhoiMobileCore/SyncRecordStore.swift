import Foundation
import AhoiCloudKitSpike

public protocol LocalSyncRecordStore: Sendable {
    func record(for recordID: UUID) async throws -> SyncRecord?
    func mergeRecords(
        _ records: [SyncRecord],
        policy: SyncRecordMergePolicy
    ) async throws -> [SyncRecordMergeOutcome]
    func allRecords() async throws -> [SyncRecord]
    func stageFetchedRecords(_ records: [SyncRecord]) async throws
    func fetchedRecords() async throws -> [SyncRecord]
    func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws
}

public enum SyncRecordMergePolicy: Equatable, Sendable {
    /// The caller has already authenticated and resolved the plaintext domain
    /// value. It wins at equal authority metadata, including schema migrations
    /// and field-clock-only corrections hidden inside ciphertext.
    case authoritativeIncoming
    /// The caller only has opaque transport envelopes. Resolve by a stable total
    /// order without allowing a stale read-compute-write batch to overwrite a
    /// record inserted concurrently in the store actor.
    case transportLastWriterWins
}

public struct SyncRecordMergeOutcome: Equatable, Sendable {
    public let incoming: SyncRecord
    public let existing: SyncRecord?
    public let snapshot: SyncRecord
}

public extension LocalSyncRecordStore {
    func upsert(_ record: SyncRecord) async throws {
        _ = try await mergeRecords([record], policy: .authoritativeIncoming)
    }

    func upsert(_ records: [SyncRecord]) async throws {
        _ = try await mergeRecords(records, policy: .authoritativeIncoming)
    }

    func stageFetchedRecord(_ record: SyncRecord) async throws {
        try await stageFetchedRecords([record])
    }

    func acknowledgeFetchedRecord(_ record: SyncRecord) async throws {
        try await acknowledgeFetchedRecords([record])
    }
}

public actor InMemorySyncRecordStore: LocalSyncRecordStore {
    private var records: [UUID: SyncRecord]
    private var stagedFetchedRecords: [SyncRecord]

    public init(
        records: [SyncRecord] = [],
        fetchedRecords: [SyncRecord] = []
    ) {
        self.records = [:]
        for record in records {
            self.records[record.recordID] = SyncRecordTransportResolver.resolve(
                self.records[record.recordID],
                record
            )
        }
        self.stagedFetchedRecords = []
        for record in fetchedRecords where !stagedFetchedRecords.contains(record) {
            stagedFetchedRecords.append(record)
        }
    }

    public func record(for recordID: UUID) async throws -> SyncRecord? {
        records[recordID]
    }

    public func mergeRecords(
        _ records: [SyncRecord],
        policy: SyncRecordMergePolicy
    ) async throws -> [SyncRecordMergeOutcome] {
        var outcomes: [SyncRecordMergeOutcome] = []
        outcomes.reserveCapacity(records.count)
        for incoming in records {
            let existing = self.records[incoming.recordID]
            let snapshot = SyncRecordTransportResolver.resolve(
                existing,
                incoming,
                policy: policy
            )
            self.records[incoming.recordID] = snapshot
            outcomes.append(.init(
                incoming: incoming,
                existing: existing,
                snapshot: snapshot
            ))
        }
        return outcomes
    }

    public func allRecords() async throws -> [SyncRecord] {
        records.values.sorted { $0.recordID.uuidString < $1.recordID.uuidString }
    }

    public func stageFetchedRecords(_ records: [SyncRecord]) async throws {
        var seen = Set(stagedFetchedRecords)
        for record in records where seen.insert(record).inserted {
            stagedFetchedRecords.append(record)
        }
    }

    public func fetchedRecords() async throws -> [SyncRecord] {
        stagedFetchedRecords
    }

    public func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws {
        guard !records.isEmpty else { return }
        let acknowledged = Set(records)
        stagedFetchedRecords.removeAll { acknowledged.contains($0) }
    }
}

/// Durable local payload storage for CKSyncEngine's pending record IDs. The
/// engine state contains identifiers and change tokens, while this store keeps
/// the corresponding encrypted envelopes available after an app restart.
/// Fetched envelopes use a sidecar inbox so an opaque record that loses the
/// transport-level LWW choice still reaches the plaintext domain merge. The
/// primary records file retains its existing format and staged wire bytes are
/// persisted without decrypting or resealing their encrypted payload.
public actor FileSyncRecordStore: LocalSyncRecordStore {
    private let fileURL: URL
    private let fetchedRecordsURL: URL
    private var records: [UUID: SyncRecord]
    private var stagedFetchedRecords: [SyncRecord]
    private let encoder: JSONEncoder

    public init(fileURL: URL) throws {
        let fetchedRecordsURL = fileURL.appendingPathExtension("fetched")
        self.fileURL = fileURL
        self.fetchedRecordsURL = fetchedRecordsURL
        self.encoder = JSONEncoder()
        self.encoder.outputFormatting = [.sortedKeys]
        if FileManager.default.fileExists(atPath: fileURL.path) {
            self.records = try JSONDecoder().decode(
                [UUID: SyncRecord].self,
                from: Data(contentsOf: fileURL)
            )
        } else {
            self.records = [:]
        }
        if FileManager.default.fileExists(atPath: fetchedRecordsURL.path) {
            let fetched = try JSONDecoder().decode(
                [SyncRecord].self,
                from: Data(contentsOf: fetchedRecordsURL)
            )
            self.stagedFetchedRecords = []
            for record in fetched where !stagedFetchedRecords.contains(record) {
                stagedFetchedRecords.append(record)
            }
        } else {
            self.stagedFetchedRecords = []
        }
    }

    public func record(for recordID: UUID) async throws -> SyncRecord? {
        records[recordID]
    }

    public func mergeRecords(
        _ records: [SyncRecord],
        policy: SyncRecordMergePolicy
    ) async throws -> [SyncRecordMergeOutcome] {
        guard !records.isEmpty else { return [] }
        let previous = self.records
        var outcomes: [SyncRecordMergeOutcome] = []
        outcomes.reserveCapacity(records.count)
        for incoming in records {
            let existing = self.records[incoming.recordID]
            let snapshot = SyncRecordTransportResolver.resolve(
                existing,
                incoming,
                policy: policy
            )
            self.records[incoming.recordID] = snapshot
            outcomes.append(.init(
                incoming: incoming,
                existing: existing,
                snapshot: snapshot
            ))
        }
        guard self.records != previous else { return outcomes }
        do {
            try persist()
        } catch {
            self.records = previous
            throw error
        }
        return outcomes
    }

    public func allRecords() async throws -> [SyncRecord] {
        records.values.sorted { $0.recordID.uuidString < $1.recordID.uuidString }
    }

    public func stageFetchedRecords(_ records: [SyncRecord]) async throws {
        guard !records.isEmpty else { return }
        let previous = stagedFetchedRecords
        var seen = Set(stagedFetchedRecords)
        for record in records where seen.insert(record).inserted {
            stagedFetchedRecords.append(record)
        }
        guard stagedFetchedRecords != previous else { return }
        do {
            try persistFetchedRecords()
        } catch {
            stagedFetchedRecords = previous
            throw error
        }
    }

    public func fetchedRecords() async throws -> [SyncRecord] {
        stagedFetchedRecords
    }

    public func acknowledgeFetchedRecords(_ records: [SyncRecord]) async throws {
        guard !records.isEmpty else { return }
        let previous = stagedFetchedRecords
        let acknowledged = Set(records)
        stagedFetchedRecords.removeAll { acknowledged.contains($0) }
        guard stagedFetchedRecords != previous else { return }
        do {
            try persistFetchedRecords()
        } catch {
            stagedFetchedRecords = previous
            throw error
        }
    }

    private func persist() throws {
        let data = try encoder.encode(records)
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fileURL, options: [.atomic])
    }

    private func persistFetchedRecords() throws {
        let data = try encoder.encode(stagedFetchedRecords)
        try FileManager.default.createDirectory(
            at: fetchedRecordsURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fetchedRecordsURL, options: [.atomic])
    }
}

enum SyncRecordTransportResolver {
    static func resolve(
        _ lhs: SyncRecord?,
        _ rhs: SyncRecord,
        policy: SyncRecordMergePolicy = .transportLastWriterWins
    ) -> SyncRecord {
        guard let lhs else { return rhs }
        if lhs.modifiedAt.physicalMilliseconds != rhs.modifiedAt.physicalMilliseconds {
            return lhs.modifiedAt.physicalMilliseconds < rhs.modifiedAt.physicalMilliseconds
                ? rhs : lhs
        }
        if lhs.modifiedAt.submillisecondMicroseconds !=
            rhs.modifiedAt.submillisecondMicroseconds {
            return lhs.modifiedAt.submillisecondMicroseconds <
                rhs.modifiedAt.submillisecondMicroseconds ? rhs : lhs
        }
        if lhs.modifiedAt.logicalCounter != rhs.modifiedAt.logicalCounter {
            return lhs.modifiedAt.logicalCounter < rhs.modifiedAt.logicalCounter ? rhs : lhs
        }
        if lhs.schemaVersion != rhs.schemaVersion {
            return lhs.schemaVersion < rhs.schemaVersion ? rhs : lhs
        }
        if (lhs.tombstone != nil) != (rhs.tombstone != nil) {
            return rhs.tombstone != nil ? rhs : lhs
        }
        if lhs.originatingDevice != rhs.originatingDevice {
            return lhs.originatingDevice < rhs.originatingDevice ? rhs : lhs
        }
        if lhs.modifiedAt.nodeID != rhs.modifiedAt.nodeID {
            return lhs.modifiedAt.nodeID < rhs.modifiedAt.nodeID ? rhs : lhs
        }
        if policy == .authoritativeIncoming {
            return rhs
        }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        guard let left = try? encoder.encode(lhs),
              let right = try? encoder.encode(rhs),
              left != right else {
            return lhs
        }
        return left.lexicographicallyPrecedes(right) ? rhs : lhs
    }
}
