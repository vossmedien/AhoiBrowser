import SwiftUI
import AhoiMobileCore
import AhoiCloudKitSpike

@main
struct AhoiMobileApp: App {
    @StateObject private var bootstrap = AhoiMobileBootstrap()

    var body: some Scene {
        WindowGroup {
            Group {
                if let runtime = bootstrap.runtime {
                    AhoiMobileBrowserView(
                        companionModel: runtime.model,
                        browser: runtime.browser
                    )
                } else if let error = bootstrap.error {
                    ContentUnavailableView {
                        Label("AhoiBrowser", systemImage: "exclamationmark.triangle")
                    } description: {
                        Text(error)
                    } actions: {
                        Button(CompanionL10n.string(
                            "action.try_again",
                            fallback: "Try Again"
                        )) {
                            Task { await bootstrap.load() }
                        }
                        .accessibilityIdentifier("bootstrap.retry")
                    }
                } else {
                    ProgressView(CompanionL10n.string(
                        "bootstrap.progress",
                        fallback: "Preparing AhoiBrowser…"
                    ))
                        .accessibilityIdentifier("bootstrap.progress")
                }
            }
            .task { await bootstrap.load() }
        }
    }
}

@MainActor
private final class AhoiMobileBootstrap: ObservableObject {
    struct Runtime {
        let model: CompanionAppModel
        let browser: MobileBrowserController
    }

    @Published private(set) var runtime: Runtime?
    @Published private(set) var error: String?
    private var isLoading = false

    func load() async {
        guard runtime == nil, !isLoading else { return }
        isLoading = true
        error = nil
        defer { isLoading = false }

        do {
            runtime = try await makeRuntime()
        } catch {
            self.error = error.localizedDescription
        }
    }

    private func makeRuntime() async throws -> Runtime {
        let applicationSupportURL = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        )[0]
        let legacySupportURL = applicationSupportURL
            .appendingPathComponent("AhoiCompanion", isDirectory: true)
        let supportURL = applicationSupportURL
            .appendingPathComponent("AhoiMobile", isDirectory: true)
        let storagePreparation = MobileStoragePreparation(
            legacyDirectory: legacySupportURL,
            destinationDirectory: supportURL
        )
        // This is a security boundary, not merely a browser-session concern.
        // CloudKit's provider eagerly reads its serialized engine and safety
        // sidecars, so every legacy file must be migrated before any store,
        // repository, provider, bridge, or sync factory can be constructed.
        try await storagePreparation.prepare()

        let store = FileCompanionStore(fileURL: supportURL.appendingPathComponent("snapshot.json"))
        let defaults = UserDefaults.standard
        let sourceDeviceUUID = CompanionDeviceIdentity.loadOrCreate(in: defaults)
        let mobileSessionID = DeviceSessionID(
            rawValue: CompanionDeviceIdentity.loadOrCreateSession(in: defaults)
        )
        let repository = LocalFirstRepository(
            store: store,
            localDeviceID: DeviceID(rawValue: sourceDeviceUUID)
        )
        let bundle = Bundle.main
        let keyVersion = configuredUInt32(bundle.object(
            forInfoDictionaryKey: "AHOI_SYNC_KEY_VERSION"
        ))
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
        let runtimeFactory: CompanionSyncRuntimeFactory?
        if let keyConfiguration, let containerIdentifier {
            let keyStore = try KeychainCompanionPayloadKeyStore(
                configuration: keyConfiguration
            )
            let bootstrapTransport = try CloudKitKeyBootstrapTransport(
                containerIdentifier: containerIdentifier,
                zoneName: "AhoiBrowserSyncZone"
            )
            let keyLifecycle = CompanionKeyLifecycleCoordinator(
                transport: bootstrapTransport,
                keyStore: keyStore,
                generator: CompanionSecureKeyGenerator.aes256
            )
            runtimeFactory = { @MainActor in
                let status: CompanionKeyLifecycleStatus
                do {
                    status = try await keyLifecycle.activate(
                        explicitOptIn: true,
                        desiredKeyVersion: keyConfiguration.keyVersion
                    )
                } catch {
                    await keyLifecycle.shutdown()
                    throw error
                }
                await keyLifecycle.shutdown()
                guard status.permitsEncryptedDomainRecords else {
                    return .init(status: status, runtime: nil)
                }
                let commandSigner: (any RemoteCommandSigning)?
                if let commandConfiguration {
                    let signer = KeychainRemoteCommandSigner(
                        configuration: commandConfiguration
                    )
                    _ = try signer.ensureIdentity()
                    commandSigner = signer
                } else {
                    commandSigner = nil
                }
                let runtime = try CompanionCloudKitBootstrap.makeRuntimeChecked(
                    syncEnabled: true,
                    containerIdentifier: containerIdentifier,
                    keyConfiguration: keyConfiguration,
                    repository: repository,
                    recordsURL: recordsURL,
                    stateURL: stateURL,
                    commandSigner: commandSigner
                )
                return .init(status: status, runtime: runtime)
            }
        } else {
            runtimeFactory = nil
        }
        let model = CompanionAppModel(
            repository: repository,
            syncRuntimeFactory: runtimeFactory,
            mobileSessionID: mobileSessionID,
            mobileDeviceName: UIDevice.current.name,
            mobileDeviceKind: UIDevice.current.userInterfaceIdiom == .pad ? .iPad : .iPhone
        )
        if defaults.bool(forKey: CompanionSyncPreferences.enabledKey) {
            await model.setSyncEnabled(true)
        }
        let browser = MobileBrowserController(
            store: FileMobileBrowserSessionStore(
                fileURL: supportURL.appendingPathComponent("browser-session.json")
            )
        )
        return Runtime(model: model, browser: browser)
    }
}

private func configuredValue(_ value: Any?) -> String? {
    guard let string = value as? String else { return nil }
    let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !trimmed.isEmpty, !trimmed.contains("$(") else { return nil }
    return trimmed
}

private func configuredUInt32(_ value: Any?) -> UInt32? {
    if let number = value as? NSNumber {
        let raw = number.uint64Value
        return raw > 0 && raw <= UInt64(UInt32.max) ? UInt32(raw) : nil
    }
    guard let string = configuredValue(value),
          let parsed = UInt32(string), parsed > 0 else {
        return nil
    }
    return parsed
}

private enum CompanionDeviceIdentity {
    private static let defaultsKey = "AhoiMobile.RemoteCommand.SourceDeviceID"
    private static let legacyDefaultsKey = "AhoiCompanion.RemoteCommand.SourceDeviceID"
    private static let sessionDefaultsKey = "AhoiMobile.Browser.SessionID"

    static func loadOrCreate(in defaults: UserDefaults) -> UUID {
        if let value = defaults.string(forKey: defaultsKey),
           let existing = UUID(uuidString: value) {
            return existing
        }
        if let value = defaults.string(forKey: legacyDefaultsKey),
           let existing = UUID(uuidString: value) {
            defaults.set(existing.uuidString.lowercased(), forKey: defaultsKey)
            return existing
        }
        let created = UUID()
        defaults.set(created.uuidString.lowercased(), forKey: defaultsKey)
        return created
    }

    static func loadOrCreateSession(in defaults: UserDefaults) -> UUID {
        if let value = defaults.string(forKey: sessionDefaultsKey),
           let existing = UUID(uuidString: value) {
            return existing
        }
        let created = UUID()
        defaults.set(created.uuidString.lowercased(), forKey: sessionDefaultsKey)
        return created
    }
}
