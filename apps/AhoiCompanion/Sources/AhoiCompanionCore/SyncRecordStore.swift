import Foundation
import AhoiCloudKitSpike

public protocol LocalSyncRecordStore: Sendable {
    func record(for recordID: UUID) async throws -> SyncRecord?
    func upsert(_ record: SyncRecord) async throws
    func allRecords() async throws -> [SyncRecord]
}

public actor InMemorySyncRecordStore: LocalSyncRecordStore {
    private var records: [UUID: SyncRecord]

    public init(records: [SyncRecord] = []) {
        self.records = Dictionary(uniqueKeysWithValues: records.map { ($0.recordID, $0) })
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
}

/// Durable local payload storage for CKSyncEngine's pending record IDs. The
/// engine state contains identifiers and change tokens, while this store keeps
/// the corresponding encrypted envelopes available after an app restart.
public actor FileSyncRecordStore: LocalSyncRecordStore {
    private let fileURL: URL
    private var records: [UUID: SyncRecord]
    private let encoder: JSONEncoder

    public init(fileURL: URL) throws {
        self.fileURL = fileURL
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

    private func persist() throws {
        let data = try encoder.encode(records)
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fileURL, options: [.atomic])
    }
}
