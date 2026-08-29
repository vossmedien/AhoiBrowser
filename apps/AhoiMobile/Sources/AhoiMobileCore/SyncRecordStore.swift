import Foundation
import AhoiCloudKitSpike

public protocol LocalSyncRecordStore: Sendable {
    func record(for recordID: UUID) async throws -> SyncRecord?
    func upsert(_ record: SyncRecord) async throws
    func allRecords() async throws -> [SyncRecord]
    func stageFetchedRecord(_ record: SyncRecord) async throws
    func fetchedRecords() async throws -> [SyncRecord]
    func acknowledgeFetchedRecord(_ record: SyncRecord) async throws
}

public actor InMemorySyncRecordStore: LocalSyncRecordStore {
    private var records: [UUID: SyncRecord]
    private var stagedFetchedRecords: [SyncRecord]

    public init(
        records: [SyncRecord] = [],
        fetchedRecords: [SyncRecord] = []
    ) {
        self.records = Dictionary(uniqueKeysWithValues: records.map { ($0.recordID, $0) })
        self.stagedFetchedRecords = []
        for record in fetchedRecords where !stagedFetchedRecords.contains(record) {
            stagedFetchedRecords.append(record)
        }
    }

    public func record(for recordID: UUID) async throws -> SyncRecord? {
        records[recordID]
    }

    public func upsert(_ record: SyncRecord) async throws {
        records[record.recordID] = record
    }

    public func allRecords() async throws -> [SyncRecord] {
        records.values.sorted { $0.recordID.uuidString < $1.recordID.uuidString }
    }

    public func stageFetchedRecord(_ record: SyncRecord) async throws {
        guard !stagedFetchedRecords.contains(record) else { return }
        stagedFetchedRecords.append(record)
    }

    public func fetchedRecords() async throws -> [SyncRecord] {
        stagedFetchedRecords
    }

    public func acknowledgeFetchedRecord(_ record: SyncRecord) async throws {
        stagedFetchedRecords.removeAll { $0 == record }
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

    public func upsert(_ record: SyncRecord) async throws {
        let previous = records.updateValue(record, forKey: record.recordID)
        do {
            try persist()
        } catch {
            if let previous {
                records[record.recordID] = previous
            } else {
                records.removeValue(forKey: record.recordID)
            }
            throw error
        }
    }

    public func allRecords() async throws -> [SyncRecord] {
        records.values.sorted { $0.recordID.uuidString < $1.recordID.uuidString }
    }

    public func stageFetchedRecord(_ record: SyncRecord) async throws {
        guard !stagedFetchedRecords.contains(record) else { return }
        stagedFetchedRecords.append(record)
        do {
            try persistFetchedRecords()
        } catch {
            stagedFetchedRecords.removeLast()
            throw error
        }
    }

    public func fetchedRecords() async throws -> [SyncRecord] {
        stagedFetchedRecords
    }

    public func acknowledgeFetchedRecord(_ record: SyncRecord) async throws {
        let previous = stagedFetchedRecords
        stagedFetchedRecords.removeAll { $0 == record }
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
