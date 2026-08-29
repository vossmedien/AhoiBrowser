import Foundation
import AhoiCloudKitSpike

public enum MobileStorageMigrationError: Error, Equatable, Sendable {
    case legacyPathIsNotDirectory
    case destinationPathIsNotDirectory
    case invalidMigrationMarker
}

public enum MobileStorageMigrator {
    /// Version two intentionally uses a new marker. Installations that already
    /// completed the original migration must still receive the lossless
    /// fetched-record inbox introduced after v1 shipped.
    public static let markerName = ".companion-migration-v2.json"
    public static let migratedFileNames = [
        "snapshot.json",
        "sync-records.json",
        "sync-records.json.fetched",
        "sync-engine-state.json",
        "sync-engine-state.json.safety",
        "sync-quarantine.json",
        // Kept for the short-lived development format used before the safety
        // state became a sidecar of the CKSyncEngine serialization.
        "sync-safety-state.json",
    ]

    public static func migrateIfNeeded(
        legacyDirectory: URL,
        destinationDirectory: URL,
        fileManager: FileManager = .default
    ) throws {
        let markerURL = destinationDirectory.appendingPathComponent(markerName)
        if fileManager.fileExists(atPath: markerURL.path) {
            let data = try Data(contentsOf: markerURL)
            guard let marker = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  (marker["schemaVersion"] as? NSNumber)?.intValue == 2 else {
                throw MobileStorageMigrationError.invalidMigrationMarker
            }
            return
        }

        var isDirectory: ObjCBool = false
        let legacyExists = fileManager.fileExists(
            atPath: legacyDirectory.path,
            isDirectory: &isDirectory
        )
        if legacyExists && !isDirectory.boolValue {
            throw MobileStorageMigrationError.legacyPathIsNotDirectory
        }

        isDirectory = false
        let destinationExists = fileManager.fileExists(
            atPath: destinationDirectory.path,
            isDirectory: &isDirectory
        )
        if destinationExists && !isDirectory.boolValue {
            throw MobileStorageMigrationError.destinationPathIsNotDirectory
        }
        if !destinationExists {
            try fileManager.createDirectory(
                at: destinationDirectory,
                withIntermediateDirectories: true
            )
        }

        var copied: [String] = []
        if legacyExists {
            // Keep extending the original immutable backup directory. Existing
            // files are never overwritten, while files introduced after v1
            // (notably the fetched inbox) are added exactly once.
            let backupDirectory = destinationDirectory
                .appendingPathComponent("CompanionBackup-v1", isDirectory: true)
            try fileManager.createDirectory(
                at: backupDirectory,
                withIntermediateDirectories: true
            )
            for fileName in migratedFileNames {
                let source = legacyDirectory.appendingPathComponent(fileName)
                guard fileManager.fileExists(atPath: source.path) else { continue }
                let backup = backupDirectory.appendingPathComponent(fileName)
                if !fileManager.fileExists(atPath: backup.path) {
                    try fileManager.copyItem(at: source, to: backup)
                }
                let destination = destinationDirectory.appendingPathComponent(fileName)
                if fileName == "sync-records.json.fetched",
                   fileManager.fileExists(atPath: destination.path) {
                    let destinationBackup = backupDirectory.appendingPathComponent(
                        "sync-records.json.fetched.destination"
                    )
                    if !fileManager.fileExists(atPath: destinationBackup.path) {
                        try fileManager.copyItem(at: destination, to: destinationBackup)
                    }
                    if try mergeFetchedRecordInboxes(
                        source: source,
                        destination: destination
                    ) {
                        copied.append(fileName)
                    }
                    continue
                }
                if !fileManager.fileExists(atPath: destination.path) {
                    try fileManager.copyItem(at: source, to: destination)
                    copied.append(fileName)
                }
            }
        }

        let marker: [String: Any] = [
            "schemaVersion": 2,
            "copiedFiles": copied.sorted(),
            "completedAt": ISO8601DateFormatter().string(from: Date()),
        ]
        let data = try JSONSerialization.data(
            withJSONObject: marker,
            options: [.sortedKeys]
        )
        try data.write(to: markerURL, options: [.atomic])
    }

    /// Combines the legacy and current durable inboxes by exact encrypted
    /// envelope identity. Both inputs must decode before the destination is
    /// replaced, so a corrupt upgrade fails closed without losing either file.
    @discardableResult
    private static func mergeFetchedRecordInboxes(
        source: URL,
        destination: URL
    ) throws -> Bool {
        let decoder = JSONDecoder()
        let legacy = try decoder.decode(
            [SyncRecord].self,
            from: Data(contentsOf: source)
        )
        let current = try decoder.decode(
            [SyncRecord].self,
            from: Data(contentsOf: destination)
        )
        var merged = current
        for record in legacy where !merged.contains(record) {
            merged.append(record)
        }
        guard merged != current else { return false }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        try encoder.encode(merged).write(to: destination, options: [.atomic])
        return true
    }
}
