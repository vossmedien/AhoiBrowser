import SwiftUI
import AhoiCompanionCore
import AhoiCloudKitSpike

@main
struct AhoiCompanionApp: App {
    @StateObject private var model: CompanionAppModel

    init() {
        let supportURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AhoiCompanion", isDirectory: true)
        let store = FileCompanionStore(fileURL: supportURL.appendingPathComponent("snapshot.json"))
        let defaults = UserDefaults.standard
        let sourceDeviceUUID = CompanionDeviceIdentity.loadOrCreate(in: defaults)
        let repository = LocalFirstRepository(
            store: store,
            localDeviceID: DeviceID(rawValue: sourceDeviceUUID)
        )
        let bundle = Bundle.main
        let keyVersion = (bundle.object(
            forInfoDictionaryKey: "AHOI_SYNC_KEY_VERSION"
        ) as? String).flatMap(UInt32.init)
        let keyService = configuredValue(bundle.object(
            forInfoDictionaryKey: "AHOI_SYNC_KEYCHAIN_SERVICE"
        ))
        let keyAccount = configuredValue(bundle.object(
            forInfoDictionaryKey: "AHOI_SYNC_KEYCHAIN_ACCOUNT"
        ))
        let keyConfiguration = keyService.flatMap { service in
            keyAccount.flatMap { account in
                keyVersion.map { version in
                    CompanionSyncKeyConfiguration(
                        service: service,
                        account: account,
                        accessGroup: configuredValue(bundle.object(
                            forInfoDictionaryKey: "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP"
                        )),
                        keyVersion: version
                    )
                }
            }
        }
        let commandConfiguration = configuredValue(bundle.object(
            forInfoDictionaryKey: "AHOI_COMMAND_KEYCHAIN_SERVICE"
        )).flatMap { service in
            configuredValue(bundle.object(
                forInfoDictionaryKey: "AHOI_COMMAND_KEYCHAIN_ACCOUNT"
            )).map { account in
                RemoteCommandKeyConfiguration(
                    service: service,
                    account: account,
                    accessGroup: configuredValue(bundle.object(
                        forInfoDictionaryKey: "AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP"
                    )),
                    sourceDeviceUUID: sourceDeviceUUID
                )
            }
        }
        let containerIdentifier = configuredValue(Bundle.main.object(
            forInfoDictionaryKey: "AHOI_CLOUDKIT_CONTAINER_ID"
        ))
        let recordsURL = supportURL.appendingPathComponent("sync-records.json")
        let stateURL = supportURL.appendingPathComponent("sync-engine-state.json")
        let runtimeFactory: () -> CompanionCloudKitRuntime? = {
            let commandSigner: (any RemoteCommandSigning)? = commandConfiguration.flatMap {
                let candidate = KeychainRemoteCommandSigner(configuration: $0)
                return (try? candidate.provisioningIdentity()) == nil ? nil : candidate
            }
            return CompanionCloudKitBootstrap.makeRuntime(
                syncEnabled: true,
                containerIdentifier: containerIdentifier,
                keyConfiguration: keyConfiguration,
                repository: repository,
                recordsURL: recordsURL,
                stateURL: stateURL,
                commandSigner: commandSigner
            )
        }
        let runtime = defaults.bool(forKey: CompanionSyncPreferences.enabledKey)
            ? runtimeFactory()
            : nil
        _model = StateObject(wrappedValue: CompanionAppModel(
            repository: repository,
            syncProvider: runtime?.provider,
            syncBridge: runtime?.bridge,
            syncRuntimeFactory: runtimeFactory
        ))
    }

    var body: some Scene {
        WindowGroup {
            CompanionRootView(model: model)
        }
    }
}

private func configuredValue(_ value: Any?) -> String? {
    guard let string = value as? String else { return nil }
    let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !trimmed.isEmpty, !trimmed.contains("$(") else { return nil }
    return trimmed
}

private enum CompanionDeviceIdentity {
    private static let defaultsKey = "AhoiCompanion.RemoteCommand.SourceDeviceID"

    static func loadOrCreate(in defaults: UserDefaults) -> UUID {
        if let value = defaults.string(forKey: defaultsKey),
           let existing = UUID(uuidString: value) {
            return existing
        }
        let created = UUID()
        defaults.set(created.uuidString.lowercased(), forKey: defaultsKey)
        return created
    }
}
