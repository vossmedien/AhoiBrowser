import Foundation

#if canImport(CloudKit)

public struct CompanionCloudKitRuntime: Sendable {
    public let provider: CloudKitSyncProvider
    public let bridge: CompanionSyncBridge
}

public enum CompanionCloudKitBootstrapError: Error, Equatable, Sendable {
    case invalidContainerIdentifier
    case keyConfigurationMissing
    case providerUnavailable
    case recordStoreInitializationFailed
    case quarantineStoreInitializationFailed
    case systemFieldsStoreInitializationFailed
}

/// Creates the native provider only for a signed target that supplies a real
/// development container. Empty or unresolved build settings keep the app
/// local-first and cannot trigger an accidental CloudKit account request.
public enum CompanionCloudKitBootstrap {
    @available(iOS 17.0, macOS 14.0, *)
    public static func makeProvider(
        syncEnabled: Bool = false,
        containerIdentifier: String?,
        recordsURL: URL,
        stateURL: URL,
        automaticallySync: Bool = true,
        quarantineStore: (any SyncQuarantineStore)? = nil,
        systemFieldsStore: (any CloudKitSystemFieldsStore)? = nil
    ) -> CloudKitSyncProvider? {
        do {
            return try makeProviderChecked(
                syncEnabled: syncEnabled,
                containerIdentifier: containerIdentifier,
                recordsURL: recordsURL,
                stateURL: stateURL,
                automaticallySync: automaticallySync,
                quarantineStore: quarantineStore,
                systemFieldsStore: systemFieldsStore
            )
        } catch {
            // Compatibility-only probe used by provider-free tests. Production
            // activation calls the checked API and surfaces the exact failure.
            return nil
        }
    }

    @available(iOS 17.0, macOS 14.0, *)
    public static func makeProviderChecked(
        syncEnabled: Bool,
        containerIdentifier: String?,
        recordsURL: URL,
        stateURL: URL,
        automaticallySync: Bool = true,
        quarantineStore: (any SyncQuarantineStore)? = nil,
        systemFieldsStore: (any CloudKitSystemFieldsStore)? = nil
    ) throws -> CloudKitSyncProvider? {
        guard syncEnabled else { return nil }
        guard syncEnabled,
              let containerIdentifier,
              !containerIdentifier.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              containerIdentifier.hasPrefix("iCloud.") else {
            throw CompanionCloudKitBootstrapError.invalidContainerIdentifier
        }
        let recordStore: FileSyncRecordStore
        do {
            recordStore = try FileSyncRecordStore(fileURL: recordsURL)
        } catch {
            throw CompanionCloudKitBootstrapError.recordStoreInitializationFailed
        }
        let resolvedQuarantineStore: any SyncQuarantineStore
        if let quarantineStore {
            resolvedQuarantineStore = quarantineStore
        } else {
            let quarantineURL = stateURL.deletingLastPathComponent()
                .appendingPathComponent("sync-quarantine.json")
            do {
                resolvedQuarantineStore = try FileSyncQuarantineStore(
                    fileURL: quarantineURL
                )
            } catch {
                throw CompanionCloudKitBootstrapError
                    .quarantineStoreInitializationFailed
            }
        }
        let resolvedSystemFieldsStore: any CloudKitSystemFieldsStore
        if let systemFieldsStore {
            resolvedSystemFieldsStore = systemFieldsStore
        } else {
            let systemFieldsURL = stateURL.deletingLastPathComponent()
                .appendingPathComponent("sync-record-system-fields.json")
            do {
                resolvedSystemFieldsStore = try FileCloudKitSystemFieldsStore(
                    fileURL: systemFieldsURL
                )
            } catch {
                throw CompanionCloudKitBootstrapError
                    .systemFieldsStoreInitializationFailed
            }
        }
        return try CloudKitSyncProvider(
            configuration: .init(
                containerIdentifier: containerIdentifier,
                automaticallySync: automaticallySync
            ),
            recordStore: recordStore,
            stateStore: FileSyncEngineStateStore(fileURL: stateURL),
            quarantineStore: resolvedQuarantineStore,
            systemFieldsStore: resolvedSystemFieldsStore
        )
    }

    @available(iOS 17.0, macOS 14.0, *)
    public static func makeRuntime(
        syncEnabled: Bool = false,
        containerIdentifier: String?,
        keyConfiguration: CompanionSyncKeyConfiguration?,
        repository: LocalFirstRepository,
        recordsURL: URL,
        stateURL: URL,
        commandSigner: (any RemoteCommandSigning)? = nil,
        quarantineStore: (any SyncQuarantineStore)? = nil
    ) -> CompanionCloudKitRuntime? {
        do {
            return try makeRuntimeChecked(
                syncEnabled: syncEnabled,
                containerIdentifier: containerIdentifier,
                keyConfiguration: keyConfiguration,
                repository: repository,
                recordsURL: recordsURL,
                stateURL: stateURL,
                commandSigner: commandSigner,
                quarantineStore: quarantineStore
            )
        } catch {
            // Compatibility-only optional API. Runtime activation uses the
            // checked variant so Keychain/CloudKit failures remain visible.
            return nil
        }
    }

    @available(iOS 17.0, macOS 14.0, *)
    public static func makeRuntimeChecked(
        syncEnabled: Bool,
        containerIdentifier: String?,
        keyConfiguration: CompanionSyncKeyConfiguration?,
        repository: LocalFirstRepository,
        recordsURL: URL,
        stateURL: URL,
        commandSigner: (any RemoteCommandSigning)? = nil,
        quarantineStore: (any SyncQuarantineStore)? = nil
    ) throws -> CompanionCloudKitRuntime? {
        guard syncEnabled else { return nil }
        guard let keyConfiguration else {
            throw CompanionCloudKitBootstrapError.keyConfigurationMissing
        }
        let sealer = try KeychainCompanionPayloadSealer(
            configuration: keyConfiguration
        )
        guard let provider = try makeProviderChecked(
            syncEnabled: true,
            containerIdentifier: containerIdentifier,
            recordsURL: recordsURL,
            stateURL: stateURL,
            quarantineStore: quarantineStore
        ) else {
            throw CompanionCloudKitBootstrapError.providerUnavailable
        }
        return .init(
            provider: provider,
            bridge: CompanionSyncBridge(
                repository: repository,
                provider: provider,
                sealer: sealer,
                commandSigner: commandSigner
            )
        )
    }
}

#else

public enum CompanionCloudKitBootstrap {
    public static func makeProvider(
        syncEnabled: Bool = false,
        containerIdentifier: String?,
        recordsURL: URL,
        stateURL: URL
    ) -> Any? {
        _ = (syncEnabled, containerIdentifier, recordsURL, stateURL)
        return nil
    }
}

#endif

public enum CompanionSyncPreferences {
    public static let enabledKey = "AhoiMobile.Sync.Enabled"
    public static let historyRetentionDaysKey =
        "AhoiMobile.Sync.HistoryRetentionDays"
    public static let remoteControlEnabledKey =
        "AhoiMobile.RemoteControl.Enabled"
    public static let defaultHistoryRetentionDays = 90
    public static let historyRetentionChoices = [30, 90, 365, -1]
}
