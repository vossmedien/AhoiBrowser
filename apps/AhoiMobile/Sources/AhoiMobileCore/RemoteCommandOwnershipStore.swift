import Foundation
import AhoiCloudKitSpike

public enum RemoteCommandOwnershipStoreError: Error, Equatable, Sendable {
    case invalidArchive
    case archiveTooLarge
    case rollbackFailed
}

public struct RemoteCommandOwnershipEntry: Codable, Equatable, Sendable {
    public let recordID: UUID
    public let issuedAtMilliseconds: UInt64

    public init(recordID: UUID, issuedAtMilliseconds: UInt64) {
        self.recordID = recordID
        self.issuedAtMilliseconds = issuedAtMilliseconds
    }
}

public struct RemoteCommandOwnershipSnapshot: Equatable, Sendable {
    public static let maximumEntryCount = 1_000

    public let entries: [RemoteCommandOwnershipEntry]
    public let migrationCompleted: Bool

    public init(
        entries: [RemoteCommandOwnershipEntry] = [],
        migrationCompleted: Bool = false
    ) {
        self.entries = Self.canonicalEntries(entries)
        self.migrationCompleted = migrationCompleted
    }

    func inserting(_ entry: RemoteCommandOwnershipEntry) -> Self {
        var values = Dictionary(uniqueKeysWithValues: entries.map { ($0.recordID, $0) })
        values[entry.recordID] = entry
        return .init(
            entries: Array(values.values),
            migrationCompleted: migrationCompleted
        )
    }

    func replacingEntries(
        _ entries: [RemoteCommandOwnershipEntry],
        migrationCompleted: Bool? = nil
    ) -> Self {
        .init(
            entries: entries,
            migrationCompleted: migrationCompleted ?? self.migrationCompleted
        )
    }

    private static func canonicalEntries(
        _ entries: [RemoteCommandOwnershipEntry]
    ) -> [RemoteCommandOwnershipEntry] {
        var newestByID: [UUID: RemoteCommandOwnershipEntry] = [:]
        for entry in entries {
            if newestByID[entry.recordID].map({
                $0.issuedAtMilliseconds >= entry.issuedAtMilliseconds
            }) != true {
                newestByID[entry.recordID] = entry
            }
        }
        return Array(newestByID.values.sorted(by: newestFirst)
            .prefix(maximumEntryCount))
    }

    private static func newestFirst(
        _ lhs: RemoteCommandOwnershipEntry,
        _ rhs: RemoteCommandOwnershipEntry
    ) -> Bool {
        if lhs.issuedAtMilliseconds != rhs.issuedAtMilliseconds {
            return lhs.issuedAtMilliseconds > rhs.issuedAtMilliseconds
        }
        return lhs.recordID.uuidString > rhs.recordID.uuidString
    }
}

public protocol RemoteCommandOwnershipStoring: Sendable {
    func load() async throws -> RemoteCommandOwnershipSnapshot
    func save(_ snapshot: RemoteCommandOwnershipSnapshot) async throws
    func clear() async throws
}

public actor InMemoryRemoteCommandOwnershipStore: RemoteCommandOwnershipStoring {
    private var snapshot: RemoteCommandOwnershipSnapshot

    public init(snapshot: RemoteCommandOwnershipSnapshot = .init()) {
        self.snapshot = snapshot
    }

    public func load() async -> RemoteCommandOwnershipSnapshot {
        snapshot
    }

    public func save(_ snapshot: RemoteCommandOwnershipSnapshot) async {
        self.snapshot = snapshot
    }

    public func clear() async {
        snapshot = .init(migrationCompleted: true)
    }
}

/// Restart-persistent index of locally issued command record IDs. It stores no
/// command payload, URL, nonce, signature, result, device metadata, or key
/// material. A corrupt archive fails construction so CloudKit runtime bootstrap
/// cannot silently fall back to a global record scan.
public actor FileRemoteCommandOwnershipStore: RemoteCommandOwnershipStoring {
    private struct Archive: Codable {
        let schemaVersion: Int
        let migrationCompleted: Bool
        let entries: [RemoteCommandOwnershipEntry]
    }

    private static let schemaVersion = 1
    private static let maximumArchiveBytes = 256 * 1_024

    private let fileURL: URL
    private let encoder: JSONEncoder
    private var snapshot: RemoteCommandOwnershipSnapshot

    public init(fileURL: URL) throws {
        self.fileURL = fileURL
        self.encoder = JSONEncoder()
        self.encoder.outputFormatting = [.sortedKeys]
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            snapshot = .init()
            return
        }
        let attributes = try FileManager.default.attributesOfItem(atPath: fileURL.path)
        guard let size = attributes[.size] as? NSNumber,
              size.intValue <= Self.maximumArchiveBytes else {
            throw RemoteCommandOwnershipStoreError.archiveTooLarge
        }
        let archive: Archive
        do {
            archive = try JSONDecoder().decode(
                Archive.self,
                from: Data(contentsOf: fileURL, options: [.mappedIfSafe])
            )
        } catch {
            throw RemoteCommandOwnershipStoreError.invalidArchive
        }
        guard archive.schemaVersion == Self.schemaVersion,
              archive.entries.count <= RemoteCommandOwnershipSnapshot.maximumEntryCount,
              Set(archive.entries.map(\.recordID)).count == archive.entries.count else {
            throw RemoteCommandOwnershipStoreError.invalidArchive
        }
        snapshot = .init(
            entries: archive.entries,
            migrationCompleted: archive.migrationCompleted
        )
    }

    public func load() async -> RemoteCommandOwnershipSnapshot {
        snapshot
    }

    public func save(_ snapshot: RemoteCommandOwnershipSnapshot) async throws {
        guard snapshot != self.snapshot else { return }
        let previous = self.snapshot
        self.snapshot = snapshot
        do {
            try persist(snapshot)
        } catch {
            self.snapshot = previous
            throw error
        }
    }

    public func clear() async throws {
        try await save(.init(migrationCompleted: true))
    }

    private func persist(_ snapshot: RemoteCommandOwnershipSnapshot) throws {
        let data = try encoder.encode(Archive(
            schemaVersion: Self.schemaVersion,
            migrationCompleted: snapshot.migrationCompleted,
            entries: snapshot.entries
        ))
        guard data.count <= Self.maximumArchiveBytes else {
            throw RemoteCommandOwnershipStoreError.archiveTooLarge
        }
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
#if os(iOS)
        try data.write(
            to: fileURL,
            options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication]
        )
#else
        try data.write(to: fileURL, options: [.atomic])
#endif
        var resourceValues = URLResourceValues()
        resourceValues.isExcludedFromBackup = true
        var mutableURL = fileURL
        try? mutableURL.setResourceValues(resourceValues)
    }
}

extension CompanionSyncBridge {
    func decodeRemoteCommandRecord(_ record: SyncRecord) throws -> RemoteCommandState {
        let plaintext = try codec.openData(record)
        let state = try wireCodec.decodeRemoteCommand(record, plaintext: plaintext)
        try validate(record, identity: state.id, version: state.version)
        return state
    }

    func validateLocallyOwnedRemoteCommand(
        _ state: RemoteCommandState,
        identity: RemoteControlProvisioningIdentity? = nil
    ) throws {
        guard let commandSigner,
              state.envelope.payload.sourceDeviceID == commandSigner.sourceDeviceID else {
            throw CompanionSyncBridgeError.remoteCommandSigningUnavailable
        }
        try RemoteCommandSemantics.validate(state.envelope.payload.command)
        let isValid = try identity?.verify(state.envelope) ??
            commandSigner.verify(state.envelope)
        guard isValid else {
            throw RemoteCommandValidationError.invalidSignature
        }
    }

    func persistRemoteCommandOwnership(
        for states: [RemoteCommandState]
    ) async throws {
        var next = try await commandOwnershipStore.load()
        for state in states {
            next = next.inserting(.init(
                recordID: state.id,
                issuedAtMilliseconds: state.envelope.payload.issuedAtMilliseconds
            ))
        }
        try await commandOwnershipStore.save(next)
    }

    func clearRemoteCommandOwnership() async throws {
        try await commandOwnershipStore.clear()
    }
}
