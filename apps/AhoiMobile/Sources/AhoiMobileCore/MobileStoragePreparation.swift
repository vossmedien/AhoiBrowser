import Foundation

/// Runs the idempotent Companion-to-Mobile migration away from the main actor.
/// The bootstrap surface remains visible until this security boundary has
/// completed, so no repository or CloudKit runtime can observe half-migrated
/// state.
public actor MobileStoragePreparation {
    private let legacyDirectory: URL
    private let destinationDirectory: URL
    private var didPrepare = false

    public init(legacyDirectory: URL, destinationDirectory: URL) {
        self.legacyDirectory = legacyDirectory
        self.destinationDirectory = destinationDirectory
    }

    public func prepare() throws {
        guard !didPrepare else { return }
        try MobileStorageMigrator.migrateIfNeeded(
            legacyDirectory: legacyDirectory,
            destinationDirectory: destinationDirectory
        )
        didPrepare = true
    }
}
