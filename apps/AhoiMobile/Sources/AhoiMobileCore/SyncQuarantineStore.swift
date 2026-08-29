import Foundation

public struct SyncQuarantineEntry: Codable, Equatable, Sendable {
    public let reason: String
    public let generation: UUID

    public init(reason: String, generation: UUID = UUID()) {
        self.reason = reason
        self.generation = generation
    }
}

public protocol SyncQuarantineStore: Sendable {
    @discardableResult
    func quarantine(recordID: UUID, reason: String) async throws -> SyncQuarantineEntry
    func allQuarantined() async -> [UUID: String]
    func entry(for recordID: UUID) async -> SyncQuarantineEntry?
    @discardableResult
    func remove(recordID: UUID, expectedGeneration: UUID) async throws -> Bool
}

public actor InMemorySyncQuarantineStore: SyncQuarantineStore {
    private var values: [UUID: SyncQuarantineEntry] = [:]

    public init() {}

    public func quarantine(
        recordID: UUID,
        reason: String
    ) async -> SyncQuarantineEntry {
        if let existing = values[recordID],
           Self.isPhysicalDelete(existing.reason) != Self.isPhysicalDelete(reason) {
            // Never let an unauthenticated physical-delete observation erase a
            // known payload/decrypt/domain failure, or vice versa. Recovery is
            // intentionally monotonic until that exact generation is validated.
            return existing
        }
        let entry = SyncQuarantineEntry(reason: reason)
        values[recordID] = entry
        return entry
    }

    public func allQuarantined() async -> [UUID: String] {
        values.mapValues { $0.reason }
    }

    public func entry(for recordID: UUID) async -> SyncQuarantineEntry? {
        values[recordID]
    }

    public func remove(recordID: UUID, expectedGeneration: UUID) async -> Bool {
        guard values[recordID]?.generation == expectedGeneration else { return false }
        values.removeValue(forKey: recordID)
        return true
    }

    private static func isPhysicalDelete(_ reason: String) -> Bool {
        reason == "physical_delete_without_validated_tombstone"
    }
}

/// Restart-persistent quarantine metadata. Payloads stay solely in the sealed
/// record store; this file contains only UUIDs and bounded reason codes so a
/// corrupt remote record cannot leak private plaintext through diagnostics.
public actor FileSyncQuarantineStore: SyncQuarantineStore {
    private let fileURL: URL
    private var values: [UUID: SyncQuarantineEntry]

    public init(fileURL: URL) throws {
        self.fileURL = fileURL
        if FileManager.default.fileExists(atPath: fileURL.path) {
            let data = try Data(contentsOf: fileURL)
            if let current = try? JSONDecoder().decode(
                [UUID: SyncQuarantineEntry].self,
                from: data
            ) {
                values = current
            } else {
                let legacy = try JSONDecoder().decode([UUID: String].self, from: data)
                values = legacy.mapValues { SyncQuarantineEntry(reason: $0) }
            }
        } else {
            values = [:]
        }
    }

    public func quarantine(
        recordID: UUID,
        reason: String
    ) async throws -> SyncQuarantineEntry {
        let safeReason = Self.safeReason(reason)
        if let existing = values[recordID],
           Self.isPhysicalDelete(existing.reason) != Self.isPhysicalDelete(safeReason) {
            return existing
        }
        let entry = SyncQuarantineEntry(reason: safeReason)
        let previous = values.updateValue(entry, forKey: recordID)
        do {
            try persist()
        } catch {
            if let previous {
                values[recordID] = previous
            } else {
                values.removeValue(forKey: recordID)
            }
            throw error
        }
        return entry
    }

    public func allQuarantined() async -> [UUID: String] {
        values.mapValues { $0.reason }
    }

    public func entry(for recordID: UUID) async -> SyncQuarantineEntry? {
        values[recordID]
    }

    public func remove(
        recordID: UUID,
        expectedGeneration: UUID
    ) async throws -> Bool {
        guard values[recordID]?.generation == expectedGeneration else { return false }
        let previous = values
        values.removeValue(forKey: recordID)
        do {
            try persist()
        } catch {
            values = previous
            throw error
        }
        return true
    }

    private func persist() throws {
        let data = try JSONEncoder().encode(values)
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fileURL, options: [.atomic])
    }

    private static func safeReason(_ reason: String) -> String {
        let allowed = reason.unicodeScalars.filter {
            CharacterSet.alphanumerics.contains($0) || $0 == "_" || $0 == "-"
        }
        return String(String.UnicodeScalarView(allowed)).prefix(80).description
    }

    private static func isPhysicalDelete(_ reason: String) -> Bool {
        reason == "physical_delete_without_validated_tombstone"
    }
}
