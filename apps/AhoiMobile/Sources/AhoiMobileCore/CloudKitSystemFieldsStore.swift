import Foundation

#if canImport(CloudKit)
import CloudKit

public enum CloudKitSystemFieldsStoreError: Error, Equatable, Sendable {
    case invalidArchive
    case archiveTooLarge
    case storeTooLarge
}

/// Persists only CloudKit-owned record metadata (record ID, change tag, and
/// server timestamps). Ahoi payload fields remain in `LocalSyncRecordStore`.
/// Keeping this sidecar separate lets local-first records stay portable while
/// ensuring updates reuse the server change tag instead of creating a fresh
/// `CKRecord` on every send.
public protocol CloudKitSystemFieldsStore: Sendable {
    func data(for recordID: UUID) async throws -> Data?
    func upsert(_ values: [UUID: Data]) async throws
    func remove(recordIDs: Set<UUID>) async throws
    func clear() async throws
}

public actor InMemoryCloudKitSystemFieldsStore: CloudKitSystemFieldsStore {
    private var values: [UUID: Data]

    public init(values: [UUID: Data] = [:]) {
        self.values = values
    }

    public func data(for recordID: UUID) async -> Data? {
        values[recordID]
    }

    public func upsert(_ values: [UUID: Data]) async {
        self.values.merge(values) { _, incoming in incoming }
    }

    public func remove(recordIDs: Set<UUID>) async {
        for recordID in recordIDs {
            values.removeValue(forKey: recordID)
        }
    }

    public func clear() async {
        values.removeAll(keepingCapacity: false)
    }
}

/// Restart-persistent CloudKit metadata. JSON is used only as an atomic map
/// container; `Data` remains the secure-coded archive produced by CKRecord.
public actor FileCloudKitSystemFieldsStore: CloudKitSystemFieldsStore {
    private static let maximumArchiveBytes = 512 * 1_024
    private static let maximumStoreBytes = 64 * 1_024 * 1_024

    private let fileURL: URL
    private var values: [UUID: Data]

    public init(fileURL: URL) throws {
        self.fileURL = fileURL
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            values = [:]
            return
        }
        let attributes = try FileManager.default.attributesOfItem(atPath: fileURL.path)
        if let size = attributes[.size] as? NSNumber,
           size.intValue > Self.maximumStoreBytes {
            throw CloudKitSystemFieldsStoreError.storeTooLarge
        }
        let decoded = try JSONDecoder().decode(
            [UUID: Data].self,
            from: Data(contentsOf: fileURL, options: [.mappedIfSafe])
        )
        guard decoded.values.allSatisfy({ $0.count <= Self.maximumArchiveBytes }) else {
            throw CloudKitSystemFieldsStoreError.archiveTooLarge
        }
        values = decoded
    }

    public func data(for recordID: UUID) async -> Data? {
        values[recordID]
    }

    public func upsert(_ incoming: [UUID: Data]) async throws {
        guard incoming.values.allSatisfy({ $0.count <= Self.maximumArchiveBytes }) else {
            throw CloudKitSystemFieldsStoreError.archiveTooLarge
        }
        guard !incoming.isEmpty else { return }
        let previous = values
        values.merge(incoming) { _, value in value }
        do {
            try persist()
        } catch {
            values = previous
            throw error
        }
    }

    public func remove(recordIDs: Set<UUID>) async throws {
        guard !recordIDs.isEmpty else { return }
        let previous = values
        for recordID in recordIDs {
            values.removeValue(forKey: recordID)
        }
        do {
            try persist()
        } catch {
            values = previous
            throw error
        }
    }

    public func clear() async throws {
        let previous = values
        values.removeAll(keepingCapacity: false)
        do {
            if FileManager.default.fileExists(atPath: fileURL.path) {
                try FileManager.default.removeItem(at: fileURL)
            }
        } catch {
            values = previous
            throw error
        }
    }

    private func persist() throws {
        let data = try JSONEncoder().encode(values)
        guard data.count <= Self.maximumStoreBytes else {
            throw CloudKitSystemFieldsStoreError.storeTooLarge
        }
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fileURL, options: [.atomic])
    }
}

enum CloudKitSystemFieldsCodec {
    static func encode(_ record: CKRecord) -> Data {
        let archiver = NSKeyedArchiver(requiringSecureCoding: true)
        record.encodeSystemFields(with: archiver)
        archiver.finishEncoding()
        return archiver.encodedData
    }

    static func decode(_ data: Data) throws -> CKRecord {
        let unarchiver = try NSKeyedUnarchiver(forReadingFrom: data)
        unarchiver.requiresSecureCoding = true
        defer { unarchiver.finishDecoding() }
        guard let record = CKRecord(coder: unarchiver) else {
            throw CloudKitSystemFieldsStoreError.invalidArchive
        }
        return record
    }
}

#endif
