import Foundation

public enum MobileStorageMigrationError: Error, Equatable, Sendable {
    case legacyPathIsNotDirectory
    case destinationPathIsNotDirectory
}

public enum MobileStorageMigrator {
    public static let markerName = ".companion-migration-v1.json"
    public static let migratedFileNames = [
        "snapshot.json",
        "sync-records.json",
        "sync-engine-state.json",
        "sync-quarantine.json",
        "sync-safety-state.json",
    ]

    public static func migrateIfNeeded(
        legacyDirectory: URL,
        destinationDirectory: URL,
        fileManager: FileManager = .default
    ) throws {
        let markerURL = destinationDirectory.appendingPathComponent(markerName)
        if fileManager.fileExists(atPath: markerURL.path) { return }

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
                if !fileManager.fileExists(atPath: destination.path) {
                    try fileManager.copyItem(at: source, to: destination)
                    copied.append(fileName)
                }
            }
        }

        let marker: [String: Any] = [
            "schemaVersion": 1,
            "copiedFiles": copied.sorted(),
            "completedAt": ISO8601DateFormatter().string(from: Date()),
        ]
        let data = try JSONSerialization.data(
            withJSONObject: marker,
            options: [.sortedKeys]
        )
        try data.write(to: markerURL, options: [.atomic])
    }
}
