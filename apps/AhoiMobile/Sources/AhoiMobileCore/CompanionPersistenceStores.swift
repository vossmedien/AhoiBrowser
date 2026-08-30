import Foundation
import AhoiCloudKitSpike

/// Deterministic local backend used by previews and tests. It has no network,
/// iCloud, or account dependency and exercises exactly the same repository
/// seam as the file-backed implementation.
public actor InMemoryCompanionStore: LocalCompanionStore {
    private var value: CompanionSnapshot

    public init(snapshot: CompanionSnapshot = .empty) {
        self.value = snapshot
    }

    public func load() async throws -> CompanionSnapshot {
        value
    }

    public func save(_ snapshot: CompanionSnapshot) async throws {
        value = snapshot
    }
}

/// Small, recoverable JSON persistence seam. A production target can replace
/// this with SwiftData or SQLite without changing the repository or CloudKit
/// provider. The app's container is expected to use Apple's file-protection
/// defaults; this type deliberately does not pretend to encrypt payloads.
public final class FileCompanionStore: LocalCompanionStore, @unchecked Sendable {
    private let fileURL: URL
    private let lock = NSLock()
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(fileURL: URL) {
        self.fileURL = fileURL
        self.encoder = JSONEncoder()
        self.decoder = JSONDecoder()
        self.encoder.outputFormatting = [.sortedKeys]
    }

    public func load() async throws -> CompanionSnapshot {
        try lock.withLock {
            guard FileManager.default.fileExists(atPath: fileURL.path) else {
                return .empty
            }
            let data = try Data(contentsOf: fileURL)
            return try decoder.decode(CompanionSnapshot.self, from: data)
        }
    }

    public func save(_ snapshot: CompanionSnapshot) async throws {
        try lock.withLock {
            // JSONEncoder is not Sendable/thread-safe. Keep encoding and the
            // matching atomic write under the same file-store lock.
            let data = try encoder.encode(snapshot)
            let directory = fileURL.deletingLastPathComponent()
            try FileManager.default.createDirectory(
                at: directory,
                withIntermediateDirectories: true
            )
            try data.write(to: fileURL, options: [.atomic])
        }
    }
}
