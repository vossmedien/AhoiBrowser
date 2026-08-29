import SwiftUI
import AhoiCloudKitSpike

public struct CompanionSettingsView: View {
    @ObservedObject private var model: CompanionAppModel
    @Binding private var syncEnabled: Bool
    @Environment(\.dismiss) private var dismiss
    @State private var accountRecoveryPresented = false
    @State private var zoneRecoveryPresented = false
    @AppStorage(MobileBrowserPreferences.searchEngineKey)
    private var searchEngineRawValue = MobileSearchEngine.duckDuckGo.rawValue

    public init(model: CompanionAppModel, syncEnabled: Binding<Bool>) {
        self.model = model
        self._syncEnabled = syncEnabled
    }

    public var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker(
                        CompanionL10n.string(
                            "settings.search_engine.title",
                            fallback: "Search engine"
                        ),
                        selection: $searchEngineRawValue
                    ) {
                        ForEach(MobileSearchEngine.allCases) { engine in
                            Text(engine.localizedName).tag(engine.rawValue)
                        }
                    }
                } header: {
                    Text(CompanionL10n.string(
                        "settings.browser.section",
                        fallback: "Browser"
                    ))
                } footer: {
                    Text(CompanionL10n.string(
                        "settings.search_engine.footer",
                        fallback: "Search terms are sent only when you choose to navigate."
                    ))
                }

                Section {
                    Toggle(
                        CompanionL10n.string(
                            "settings.sync.enabled",
                            fallback: "CloudKit sync"
                        ),
                        isOn: $syncEnabled
                    )
                    LabeledContent(
                        CompanionL10n.string(
                            "settings.sync.state",
                            fallback: "Status"
                        ),
                        value: syncStateText
                    )
                    if let status = model.syncStatus {
                        Text(status.detail)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        if let retry = status.retryAfterSeconds {
                            Text(CompanionL10n.format(
                                "settings.sync.retry",
                                fallback: "Retry in %.0f seconds",
                                retry
                            ))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                        }
                    }
                    Button {
                        Task { await model.sync() }
                    } label: {
                        Label(
                            CompanionL10n.string(
                                "action.sync_now",
                                fallback: "Sync now"
                            ),
                            systemImage: "arrow.triangle.2.circlepath"
                        )
                    }
                    .disabled(!model.isSyncConfigured)
                } header: {
                    Text(CompanionL10n.string(
                        "settings.sync.section",
                        fallback: "Sync"
                    ))
                } footer: {
                    if syncEnabled && !model.isSyncConfigured {
                        Text(CompanionL10n.string(
                            "settings.sync.configuration_missing",
                            fallback: "Apple provisioning or the local encryption key is missing. Local data remains available."
                        ))
                    }
                }

                Section {
                    Picker(
                        CompanionL10n.string(
                            "settings.retention.title",
                            fallback: "Keep history"
                        ),
                        selection: Binding(
                            get: { model.historyRetentionDays },
                            set: { days in
                                Task { await model.setHistoryRetentionDays(days) }
                            }
                        )
                    ) {
                        Text(CompanionL10n.string(
                            "settings.retention.30",
                            fallback: "30 days"
                        )).tag(30)
                        Text(CompanionL10n.string(
                            "settings.retention.90",
                            fallback: "90 days"
                        )).tag(90)
                        Text(CompanionL10n.string(
                            "settings.retention.365",
                            fallback: "365 days"
                        )).tag(365)
                        Text(CompanionL10n.string(
                            "settings.retention.forever",
                            fallback: "Forever"
                        )).tag(-1)
                    }
                } header: {
                    Text(CompanionL10n.string(
                        "settings.retention.section",
                        fallback: "History"
                    ))
                } footer: {
                    Text(CompanionL10n.string(
                        "settings.retention.footer",
                        fallback: "Expired visits are converted to sync tombstones so deletion propagates to your other devices."
                    ))
                }

                if model.syncSafetyState.accountTransitionPending ||
                    model.syncSafetyState.zoneRecoveryPending {
                    Section {
                        if model.syncSafetyState.accountTransitionPending {
                            Button {
                                accountRecoveryPresented = true
                            } label: {
                                Label(
                                    CompanionL10n.string(
                                        "settings.recovery.account",
                                        fallback: "Resolve iCloud account change"
                                    ),
                                    systemImage: "person.crop.circle.badge.exclamationmark"
                                )
                            }
                        }
                        if model.syncSafetyState.zoneRecoveryPending {
                            Button {
                                zoneRecoveryPresented = true
                            } label: {
                                Label(
                                    CompanionL10n.string(
                                        "settings.recovery.zone",
                                        fallback: "Recreate sync zone"
                                    ),
                                    systemImage: "externaldrive.badge.exclamationmark"
                                )
                            }
                        }
                    } header: {
                        Text(CompanionL10n.string(
                            "settings.recovery.section",
                            fallback: "Recovery"
                        ))
                    } footer: {
                        Text(CompanionL10n.string(
                            "settings.recovery.footer",
                            fallback: "Recovery never deletes the local snapshot automatically."
                        ))
                    }
                }

                Section {
                    if visibleDevices.isEmpty {
                        Text(CompanionL10n.string(
                            "settings.devices.empty",
                            fallback: "No synced devices yet"
                        ))
                        .foregroundStyle(.secondary)
                    } else {
                        ForEach(visibleDevices) { device in
                            CompanionDeviceStatusRow(
                                device: device,
                                session: freshestSession(for: device.id)
                            )
                        }
                    }
                } header: {
                    Text(CompanionL10n.string(
                        "settings.devices.section",
                        fallback: "Devices"
                    ))
                }

                if !visibleExtensions.isEmpty || !visibleDeveloperAssets.isEmpty ||
                    !model.snapshot.productRecords.appearance.isEmpty ||
                    !model.snapshot.productRecords.permittedSettings.isEmpty {
                    Section {
                        if let appearance = model.snapshot.productRecords.appearance
                            .filter({ !$0.isDeleted })
                            .max(by: { $0.version < $1.version }) {
                            LabeledContent(
                                CompanionL10n.string(
                                    "settings.product.appearance",
                                    fallback: "Appearance"
                                ),
                                value: appearance.colorMode.rawValue.capitalized
                            )
                        }
                        ForEach(visibleExtensions) { item in
                            VStack(alignment: .leading, spacing: 2) {
                                Text(item.name.isEmpty ? item.extensionID : item.name)
                                Text(CompanionL10n.format(
                                    "settings.product.extension_detail",
                                    fallback: "%@ · version %@",
                                    deviceName(item.deviceID),
                                    item.extensionVersion
                                ))
                                .font(.caption)
                                .foregroundStyle(.secondary)
                            }
                        }
                        ForEach(visibleDeveloperAssets) { item in
                            LabeledContent(
                                item.name,
                                value: CompanionL10n.string(
                                    "settings.product.opted_in",
                                    fallback: "Opted in"
                                )
                            )
                        }
                    } header: {
                        Text(CompanionL10n.string(
                            "settings.product.section",
                            fallback: "Synced product data"
                        ))
                    } footer: {
                        Text(CompanionL10n.string(
                            "settings.product.footer",
                            fallback: "Extension inventory is advisory only. It never installs or enables an extension. Developer assets appear only after explicit per-asset opt-in."
                        ))
                    }
                }

                Section {
                    if let identity = model.remoteControlIdentity {
                        LabeledContent(
                            CompanionL10n.string(
                                "settings.remote.key",
                                fallback: "Signing key"
                            ),
                            value: identity.fingerprint
                        )
                        Text(CompanionL10n.string(
                            "settings.remote.approval_required",
                            fallback: "This iPhone or iPad must be approved on each Mac before it can send commands."
                        ))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    } else {
                        Text(CompanionL10n.string(
                            "settings.remote.unavailable",
                            fallback: "Remote control is unavailable until a Keychain signing key is provisioned."
                        ))
                        .foregroundStyle(.secondary)
                    }
                    ForEach(model.recentRemoteCommands) { item in
                        VStack(alignment: .leading, spacing: 3) {
                            Text(item.action)
                            Text(commandStatusText(item.status))
                                .font(.caption)
                                .foregroundStyle(item.status == .failed ? .red : .secondary)
                            if !item.resultCode.isEmpty {
                                Text(item.resultCode)
                                    .font(.caption2.monospaced())
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                } header: {
                    Text(CompanionL10n.string(
                        "settings.remote.section",
                        fallback: "Remote control"
                    ))
                }
            }
            .navigationTitle(CompanionL10n.string(
                "settings.title",
                fallback: "Settings"
            ))
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(CompanionL10n.string(
                        "action.done",
                        fallback: "Done"
                    )) {
                        dismiss()
                    }
                }
            }
            .confirmationDialog(
                CompanionL10n.string(
                    "settings.recovery.account.prompt",
                    fallback: "Use the local snapshot with the new iCloud account?"
                ),
                isPresented: $accountRecoveryPresented,
                titleVisibility: .visible
            ) {
                Button(CompanionL10n.string(
                    "settings.recovery.account.upload",
                    fallback: "Upload local data"
                )) {
                    Task { await model.confirmAccountTransition(allowLocalUpload: true) }
                }
                Button(CompanionL10n.string(
                    "settings.recovery.account.keep_local",
                    fallback: "Keep local only"
                )) {
                    Task { await model.confirmAccountTransition(allowLocalUpload: false) }
                }
                Button(CompanionL10n.string(
                    "action.cancel",
                    fallback: "Cancel"
                ), role: .cancel) {}
            } message: {
                Text(CompanionL10n.string(
                    "settings.recovery.account.message",
                    fallback: "Nothing is uploaded until you choose. Local data is retained in both cases."
                ))
            }
            .confirmationDialog(
                CompanionL10n.string(
                    "settings.recovery.zone.prompt",
                    fallback: "Recreate the CloudKit zone and upload the retained local snapshot?"
                ),
                isPresented: $zoneRecoveryPresented,
                titleVisibility: .visible
            ) {
                Button(CompanionL10n.string(
                    "settings.recovery.zone.confirm",
                    fallback: "Recreate and upload"
                )) {
                    Task { await model.confirmZoneRecovery() }
                }
                Button(CompanionL10n.string(
                    "action.cancel",
                    fallback: "Cancel"
                ), role: .cancel) {}
            }
        }
    }

    private var visibleDevices: [Device] {
        model.snapshot.devices.filter { !$0.isDeleted }.sorted {
            if $0.isRevoked != $1.isRevoked { return !$0.isRevoked }
            return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
    }

    private func freshestSession(for id: DeviceID) -> DeviceSession? {
        model.snapshot.sessions.filter {
            $0.deviceID == id && !$0.isDeleted
        }.max { $0.lastActiveAt < $1.lastActiveAt }
    }

    private var visibleExtensions: [CompanionExtensionInventoryRecord] {
        model.snapshot.productRecords.extensionInventory
            .filter { !$0.isDeleted }
            .sorted {
                $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
            }
    }

    private var visibleDeveloperAssets: [CompanionDeveloperAssetRecord] {
        model.snapshot.productRecords.developerAssets
            .filter { !$0.isDeleted && $0.optedIn }
            .sorted {
                $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
            }
    }

    private func deviceName(_ id: DeviceID) -> String {
        model.snapshot.devices.first { $0.id == id }?.name ??
            CompanionL10n.string("settings.product.unknown_device", fallback: "Other device")
    }

    private var syncStateText: String {
        guard syncEnabled else {
            return CompanionL10n.string("sync.state.off", fallback: "Off")
        }
        guard model.isSyncConfigured else {
            return CompanionL10n.string(
                "sync.state.local_only",
                fallback: "Local only"
            )
        }
        return model.syncStatus.map { status in
            switch status.phase {
            case .idle:
                CompanionL10n.string("sync.state.ready", fallback: "Ready")
            case .preparing:
                CompanionL10n.string("sync.state.preparing", fallback: "Preparing")
            case .syncing:
                CompanionL10n.string("sync.state.syncing", fallback: "Syncing")
            case .offline:
                CompanionL10n.string("sync.state.offline", fallback: "Offline")
            case .accountRequired:
                CompanionL10n.string(
                    "sync.state.account_required",
                    fallback: "Account action required"
                )
            case .retryScheduled:
                CompanionL10n.string(
                    "sync.state.retry_scheduled",
                    fallback: "Retry scheduled"
                )
            case .conflictResolved:
                CompanionL10n.string(
                    "sync.state.conflict_resolved",
                    fallback: "Conflict resolved"
                )
            case .quarantined:
                CompanionL10n.string(
                    "sync.state.quarantined",
                    fallback: "Record quarantined"
                )
            case .failed:
                CompanionL10n.string("sync.state.failed", fallback: "Failed")
            }
        } ?? CompanionL10n.string("sync.state.ready", fallback: "Ready")
    }

    private func commandStatusText(_ status: RemoteCommandStatus) -> String {
        switch status {
        case .queued:
            CompanionL10n.string("remote.state.queued", fallback: "Queued")
        case .delivered:
            CompanionL10n.string("remote.state.delivered", fallback: "Delivered")
        case .executed:
            CompanionL10n.string("remote.state.executed", fallback: "Executed")
        case .failed:
            CompanionL10n.string("remote.state.failed", fallback: "Failed")
        }
    }
}

private struct CompanionDeviceStatusRow: View {
    let device: Device
    let session: DeviceSession?

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: symbol)
                .frame(width: 24)
            VStack(alignment: .leading, spacing: 2) {
                Text(device.name)
                Text(status)
                    .font(.caption)
                    .foregroundStyle(device.isRevoked ? .red : .secondary)
            }
            Spacer()
            if let session {
                Text(Date(
                    timeIntervalSince1970: Double(
                        session.lastActiveAt.physicalMilliseconds
                    ) / 1_000
                ), style: .relative)
                .font(.caption2)
                .foregroundStyle(.tertiary)
            }
        }
    }

    private var symbol: String {
        switch device.kind {
        case .mac: "desktopcomputer"
        case .iPhone: "iphone"
        case .iPad: "ipad"
        }
    }

    private var status: String {
        if device.isRevoked {
            return CompanionL10n.string("device.state.revoked", fallback: "Revoked")
        }
        if session?.isOnline == true {
            return CompanionL10n.string("device.state.online", fallback: "Online")
        }
        return CompanionL10n.string("device.state.offline", fallback: "Offline")
    }
}
