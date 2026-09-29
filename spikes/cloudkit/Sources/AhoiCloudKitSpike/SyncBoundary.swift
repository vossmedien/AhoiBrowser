import Foundation

public enum SyncDisposition: Equatable, Sendable {
    case allowed
    case denied
    case requiresExplicitOptIn
}

public struct SyncAuthorizationContext: Sendable {
    public var optedInDeveloperAssetIDs: Set<UUID>

    public init(optedInDeveloperAssetIDs: Set<UUID> = []) {
        self.optedInDeveloperAssetIDs = optedInDeveloperAssetIDs
    }
}

public enum SyncBoundaryError: Error, Equatable {
    case dataClassDenied(SyncDataClass)
    case developerAssetNotOptedIn(UUID)
    case invalidSchemaVersion
    case invalidCiphertext
    case invalidTombstone
}

public struct SyncBoundary: Sendable {
    public init() {}

    public func disposition(for dataClass: SyncDataClass) -> SyncDisposition {
        guard SharedSyncFormat.supportedDataClasses.contains(dataClass) else {
            return .denied
        }
        return dataClass == .developerAsset ? .requiresExplicitOptIn : .allowed
    }

    public func authorize(
        _ record: SyncRecord,
        context: SyncAuthorizationContext = .init()
    ) throws {
        guard record.schemaVersion == SharedSyncFormat.currentVersion else {
            throw SyncBoundaryError.invalidSchemaVersion
        }
        guard SharedSyncFormat.supportedDataClasses.contains(record.dataClass) else {
            throw SyncBoundaryError.dataClassDenied(record.dataClass)
        }
        guard record.encryptedValue.keyVersion > 0,
              record.encryptedValue.nonce.count == 12,
              record.encryptedValue.ciphertextAndTag.count >= 16 else {
            throw SyncBoundaryError.invalidCiphertext
        }

        if let tombstone = record.tombstone {
            guard Self.supportsTombstone(record.dataClass) else {
                throw SyncBoundaryError.invalidTombstone
            }
            guard
                  tombstone.entityID == record.entityID,
                  tombstone.deletedAt == record.modifiedAt,
                  tombstone.deletedBy == record.originatingDevice,
                  tombstone.purgeAfterMilliseconds
                    > tombstone.deletedAt.physicalMilliseconds else {
                throw SyncBoundaryError.invalidTombstone
            }
        }

        switch disposition(for: record.dataClass) {
        case .allowed:
            return
        case .denied:
            throw SyncBoundaryError.dataClassDenied(record.dataClass)
        case .requiresExplicitOptIn:
            guard record.tombstone != nil ||
                    context.optedInDeveloperAssetIDs.contains(record.entityID) else {
                throw SyncBoundaryError.developerAssetNotOptedIn(record.entityID)
            }
        }
    }

    private static func supportsTombstone(_ dataClass: SyncDataClass) -> Bool {
        switch dataClass {
        case .device, .workspace, .treeNode, .deviceSession, .deviceTab,
             .historyVisit, .appearance, .permittedSetting, .extensionInventory,
             .developerAsset, .bookmark, .deviceCapability, .splitGroup, .tabArchiveEntry:
            return true
        case .orderKey, .tombstone, .recoveryMetadata, .history, .remoteCommand,
             .cookie, .password, .autofill, .siteData, .cache, .permission,
             .extensionStorage, .incognito, .keychainSecret, .headerSecret,
             .httpAuthSecret:
            return false
        }
    }
}
