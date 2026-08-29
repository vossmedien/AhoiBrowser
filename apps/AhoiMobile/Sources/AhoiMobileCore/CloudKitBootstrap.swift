import Foundation

#if canImport(CloudKit)

public struct CompanionCloudKitRuntime: Sendable {
    public let provider: CloudKitSyncProvider
    public let bridge: CompanionSyncBridge
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
        quarantineStore: (any SyncQuarantineStore)? = nil,
        systemFieldsStore: (any CloudKitSystemFieldsStore)? = nil
    ) -> CloudKitSyncProvider? {
        guard syncEnabled,
              let containerIdentifier,
              !containerIdentifier.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              containerIdentifier.hasPrefix("iCloud.") else {
            return nil
        }
        guard let recordStore = try? FileSyncRecordStore(fileURL: recordsURL) else {
            return nil
        }
        let resolvedQuarantineStore: any SyncQuarantineStore
        if let quarantineStore {
            resolvedQuarantineStore = quarantineStore
        } else {
            let quarantineURL = stateURL.deletingLastPathComponent()
                .appendingPathComponent("sync-quarantine.json")
            guard let durableStore = try? FileSyncQuarantineStore(fileURL: quarantineURL) else {
                return nil
            }
            resolvedQuarantineStore = durableStore
        }
        let resolvedSystemFieldsStore: any CloudKitSystemFieldsStore
        if let systemFieldsStore {
            resolvedSystemFieldsStore = systemFieldsStore
        } else {
            let systemFieldsURL = stateURL.deletingLastPathComponent()
                .appendingPathComponent("sync-record-system-fields.json")
            guard let durableStore = try? FileCloudKitSystemFieldsStore(
                fileURL: systemFieldsURL
            ) else {
                return nil
            }
            resolvedSystemFieldsStore = durableStore
        }
        return try? CloudKitSyncProvider(
            configuration: .init(containerIdentifier: containerIdentifier),
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
        guard syncEnabled,
              let keyConfiguration,
              let sealer = try? KeychainCompanionPayloadSealer(
                configuration: keyConfiguration
              ),
              let provider = makeProvider(
                syncEnabled: true,
                containerIdentifier: containerIdentifier,
                recordsURL: recordsURL,
                stateURL: stateURL,
                quarantineStore: quarantineStore
              ) else {
            return nil
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
