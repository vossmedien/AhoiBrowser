import SwiftUI
import AhoiMobileCore
import AhoiCloudKitSpike

@main
struct AhoiMobileApp: App {
    @StateObject private var bootstrap = AhoiMobileBootstrap()
    @StateObject private var browserCommands = MobileBrowserCommandRouter()

    var body: some Scene {
        WindowGroup {
            Group {
                if AhoiMobileProcessMode.isCloudKitE2EHost {
                    Color.clear
                        .accessibilityIdentifier("cloudkit.e2e.inert-host")
                } else if let runtime = bootstrap.runtime {
                    AhoiMobileBrowserView(
                        companionModel: runtime.model,
                        browser: runtime.browser
                    )
                    .environment(\.mobileBrowserCommandRouter, browserCommands)
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
            .task {
                guard !AhoiMobileProcessMode.isCloudKitE2EHost else { return }
                await bootstrap.load()
            }
            .onOpenURL { url in
                guard !AhoiMobileProcessMode.isCloudKitE2EHost else { return }
                bootstrap.handleExternalURL(url)
            }
        }
        .commands {
            MobileBrowserSceneCommands(router: browserCommands)
        }
    }
}

/// A hosted unit-test bundle must never race the product bootstrap. In
/// particular, persisted sync opt-in state must not construct the production
/// CloudKit runtime while the isolated real-container transport harness is
/// preparing its uniquely scoped synthetic zone. UI tests are out-of-process
/// and therefore do not satisfy either hosted-test signal.
private enum AhoiMobileProcessMode {
    static var isCloudKitE2EHost: Bool {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AHOI_CLOUDKIT_E2E_HOST_MODE"] == "1" else {
            return false
        }
        return environment["XCTestConfigurationFilePath"] != nil ||
            environment["XCInjectBundleInto"] != nil ||
            NSClassFromString("XCTest.XCTestCase") != nil
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
    private var pendingExternalURL: URL?
    private var externalOpenDeduplicator: MobileExternalOpenDeduplicator
    private let performanceRecorder = MobileBrowserPerformanceRecorder()
    private let performanceLaunchValidation: MobilePerformanceLaunchValidation

    init() {
        let launchValidation = MobilePerformanceLaunchRequest.validate(
            arguments: ProcessInfo.processInfo.arguments
        )
        performanceLaunchValidation = launchValidation
        switch launchValidation {
        case .valid, .invalid:
            // Performance flags are a process-start boundary. A malformed
            // request must not fall through to any product persistence.
            externalOpenDeduplicator = MobileExternalOpenDeduplicator()
            return
        case .notRequested:
            break
        }
#if DEBUG
        if CompanionSyncVisibleUITestLaunch.isRequested(
            arguments: ProcessInfo.processInfo.arguments
        ) {
            externalOpenDeduplicator = MobileExternalOpenDeduplicator()
            return
        }
#endif
        let applicationSupportURL = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        )[0]
        externalOpenDeduplicator = MobileExternalOpenDeduplicator(
            receiptURL: applicationSupportURL
                .appendingPathComponent("AhoiMobile", isDirectory: true)
                .appendingPathComponent("external-open-receipt.json")
        )
    }

    func load() async {
        guard runtime == nil, !isLoading else { return }
        isLoading = true
        error = nil
        defer { isLoading = false }

        do {
            let loadedRuntime = try await makeRuntime()
            runtime = loadedRuntime
            if let pendingExternalURL {
                self.pendingExternalURL = nil
                loadedRuntime.browser.handleClaimedExternalURL(pendingExternalURL)
            }
        } catch {
            self.error = error.localizedDescription
        }
    }

    func handleExternalURL(_ url: URL) {
        guard case .notRequested = performanceLaunchValidation else { return }
        let safeURL: URL
        do {
            safeURL = try MobileBrowserInputRouter.validateWebURL(url)
        } catch {
            deliverUnclaimedExternalURL(url)
            return
        }
        guard externalOpenDeduplicator.accepts(safeURL) else { return }
        if let runtime {
            runtime.browser.handleClaimedExternalURL(safeURL)
        } else {
            pendingExternalURL = safeURL
        }
    }

    private func deliverUnclaimedExternalURL(_ url: URL) {
        if let runtime {
            runtime.browser.handleExternalURL(url)
        } else {
            pendingExternalURL = url
        }
    }

    private func makeRuntime() async throws -> Runtime {
        switch performanceLaunchValidation {
        case .invalid:
            throw AhoiMobileBootstrapError.invalidPerformanceLaunch
        case let .valid(request):
#if DEBUG
            return try makePerformanceRuntime(request)
#else
            throw AhoiMobileBootstrapError.performanceLaunchRequiresDebug
#endif
        case .notRequested:
            break
        }
#if DEBUG
        if CompanionSyncVisibleUITestLaunch.isRequested(
            arguments: ProcessInfo.processInfo.arguments
        ) {
            return makeSyncVisibleUITestRuntime()
        }
#endif
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
        let desiredKeyVersion = configuredUInt32(bundle.object(
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
                desiredKeyVersion.map { _ in
                    CompanionSyncKeyConfiguration(
                        service: service,
                        account: account,
                        accessGroup: configuredValue(bundle.object(
                            forInfoDictionaryKey: "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP"
                        )),
                        keyVersion: CompanionSyncKeyFamily.anchorVersion
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
        let rotationJournalURL = supportURL.appendingPathComponent(
            "sync-key-rotation.json"
        )
        let runtimeFactory: CompanionSyncRuntimeFactory?
        if let keyConfiguration,
           let desiredKeyVersion,
           let containerIdentifier {
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
                var status: CompanionKeyLifecycleStatus
                do {
                    status = try await keyLifecycle.activate(
                        explicitOptIn: true,
                        desiredKeyVersion: desiredKeyVersion
                    )
                } catch {
                    await keyLifecycle.shutdown()
                    throw error
                }
                await keyLifecycle.shutdown()
                status = try await Self.resolveKeyRotationIfRequired(
                    status: status,
                    desiredKeyVersion: desiredKeyVersion,
                    familyAnchorConfiguration: keyConfiguration,
                    containerIdentifier: containerIdentifier,
                    recordsURL: recordsURL,
                    stateURL: stateURL,
                    journalURL: rotationJournalURL,
                    keyStore: keyStore
                )
                guard status.permitsEncryptedDomainRecords else {
                    return .init(status: status, runtime: nil)
                }
                guard case let .ready(activeKeyVersion) = status else {
                    return .init(status: status, runtime: nil)
                }
                let commandSigner: (any RemoteCommandSigning)?
                if let commandConfiguration {
                    let signer = KeychainRemoteCommandSigner(
                        configuration: commandConfiguration
                    )
                    do {
                        _ = try signer.ensureIdentity()
                        commandSigner = signer
                    } catch RemoteCommandSignerError.identityRevoked {
                        // The signing identity is optional and deliberately
                        // stays revoked across restart. Payload sync remains
                        // available. Keep only the fail-closed signer facade so
                        // Settings can perform an explicit re-enrolment; every
                        // provisioning/signing call still rejects the marker.
                        commandSigner = signer
                    }
                } else {
                    commandSigner = nil
                }
                let runtime = try CompanionCloudKitBootstrap.makeRuntimeChecked(
                    syncEnabled: true,
                    containerIdentifier: containerIdentifier,
                    keyConfiguration: keyConfiguration.canonicalConfiguration(
                        for: activeKeyVersion
                    ),
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
        let browser = MobileBrowserController(
            store: FileMobileBrowserSessionStore(
                fileURL: supportURL.appendingPathComponent("browser-session.json")
            ),
            performanceRecorder: performanceRecorder,
            externalOpenReceiptURL: supportURL.appendingPathComponent(
                "external-open-receipt.json"
            )
        )
        return Runtime(model: model, browser: browser)
    }

#if DEBUG
    private func makeSyncVisibleUITestRuntime() -> Runtime {
        let repository = LocalFirstRepository(
            store: InMemoryCompanionStore(),
            localDeviceID: DeviceID()
        )
        let model = CompanionAppModel(
            repository: repository,
            mobileSessionID: DeviceSessionID(),
            mobileDeviceName: UIDevice.current.name,
            mobileDeviceKind: UIDevice.current.userInterfaceIdiom == .pad ? .iPad : .iPhone
        )
        let browser = MobileBrowserController(
            store: InMemoryMobileBrowserSessionStore(),
            performanceRecorder: performanceRecorder
        )
        return Runtime(model: model, browser: browser)
    }

    private func makePerformanceRuntime(
        _ request: MobilePerformanceLaunchRequest
    ) throws -> Runtime {
        let defaultsSuite = "app.ahoibrowser.AhoiBrowser.performance.\(request.nonce)"
        guard let isolatedDefaults = UserDefaults(suiteName: defaultsSuite) else {
            throw AhoiMobileBootstrapError.performanceIsolationUnavailable
        }
        let model = CompanionAppModel(
            repository: LocalFirstRepository(store: InMemoryCompanionStore()),
            defaults: isolatedDefaults
        )
        let downloadDirectory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiPerformanceDownloads", isDirectory: true)
            .appendingPathComponent(request.nonce, isDirectory: true)
        let browser = MobileBrowserController(
            store: InMemoryMobileBrowserSessionStore(),
            downloadCoordinator: MobileDownloadCoordinator(
                directoryURL: downloadDirectory
            ),
            performanceRecorder: performanceRecorder
        )
        return Runtime(model: model, browser: browser)
    }
#endif

    private static func resolveKeyRotationIfRequired(
        status: CompanionKeyLifecycleStatus,
        desiredKeyVersion: UInt32,
        familyAnchorConfiguration: CompanionSyncKeyConfiguration,
        containerIdentifier: String,
        recordsURL: URL,
        stateURL: URL,
        journalURL: URL,
        keyStore: any CompanionPayloadKeyRotationStoring
    ) async throws -> CompanionKeyLifecycleStatus {
        guard case let .ready(activeKeyVersion) = status else { return status }
        let journalStore = FileCompanionKeyRotationJournalStore(fileURL: journalURL)
        let storedPlan = try await journalStore.loadRotationPlan()

        let currentVersion: UInt32
        let nextVersion: UInt32
        let transitionEndsAt: Date
        let shouldResume: Bool
        if let storedPlan, storedPlan.stage != .completed {
            guard activeKeyVersion == storedPlan.currentVersion ||
                    activeKeyVersion == storedPlan.nextVersion else {
                return .recovery(
                    reason: .keyVersionMismatch,
                    keyVersion: activeKeyVersion
                )
            }
            currentVersion = storedPlan.currentVersion
            nextVersion = storedPlan.nextVersion
            transitionEndsAt = storedPlan.transitionEndsAt
            shouldResume = true
        } else if desiredKeyVersion > activeKeyVersion {
            currentVersion = activeKeyVersion
            nextVersion = desiredKeyVersion
            transitionEndsAt = Date().addingTimeInterval(
                CompanionSyncKeyFamily.rotationWindow
            )
            shouldResume = false
        } else {
            return status
        }

        let rotation = try await CompanionCloudKitKeyRotationBootstrap
            .makeRuntimeChecked(
                containerIdentifier: containerIdentifier,
                familyAnchorConfiguration: familyAnchorConfiguration,
                currentVersion: currentVersion,
                nextVersion: nextVersion,
                recordsURL: recordsURL,
                stateURL: stateURL,
                journalURL: journalURL,
                keyStore: keyStore
            )
        do {
            let completed: CompanionKeyRotationPlan
            if shouldResume {
                completed = try await rotation.coordinator.resume()
            } else {
                completed = try await rotation.coordinator.begin(
                    currentVersion: currentVersion,
                    nextVersion: nextVersion,
                    transitionEndsAt: transitionEndsAt
                )
            }
            await rotation.provider.cancel()
            return completed.lifecycleStatus
        } catch {
            let durablePlan = try? await rotation.coordinator.currentPlan()
            await rotation.provider.cancel()
            if let durablePlan {
                return durablePlan.lifecycleStatus
            }
            throw error
        }
    }
}

private enum AhoiMobileBootstrapError: LocalizedError {
    case invalidPerformanceLaunch
    case performanceLaunchRequiresDebug
    case performanceIsolationUnavailable

    var errorDescription: String? {
        switch self {
        case .invalidPerformanceLaunch:
            return "The performance launch request is incomplete or inconsistent."
        case .performanceLaunchRequiresDebug:
            return "Performance evidence workloads require a DEBUG candidate."
        case .performanceIsolationUnavailable:
            return "The isolated performance runtime could not be created."
        }
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

private enum CompanionSyncKeyFamily {
    static let anchorVersion: UInt32 = 1
    static let rotationWindow: TimeInterval = 7 * 24 * 60 * 60
}
