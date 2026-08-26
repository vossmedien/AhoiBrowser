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
        quarantineStore: (any SyncQuarantineStore)? = nil
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
        return try? CloudKitSyncProvider(
            configuration: .init(containerIdentifier: containerIdentifier),
            recordStore: recordStore,
            stateStore: FileSyncEngineStateStore(fileURL: stateURL),
            quarantineStore: resolvedQuarantineStore
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
    public static let enabledKey = "AhoiCompanion.Sync.Enabled"
    public static let historyRetentionDaysKey =
        "AhoiCompanion.Sync.HistoryRetentionDays"
    public static let remoteControlEnabledKey =
        "AhoiCompanion.RemoteControl.Enabled"
    public static let defaultHistoryRetentionDays = 90
    public static let historyRetentionChoices = [30, 90, 365, -1]
}
