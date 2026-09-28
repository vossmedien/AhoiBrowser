import SwiftUI
#if DEBUG
import UniformTypeIdentifiers
#endif

struct CompanionExtensionSetupSection: View {
    @ObservedObject var model: CompanionAppModel
#if DEBUG
    @State private var showSpikeFolderPicker = false
    @State private var filesSpikeLoaded = false
    @State private var filesSpikeStatus = ""
#endif

    var body: some View {
        Section {
            Toggle(
                CompanionL10n.string(
                    "settings.extensions.share", fallback: "Show shared extension setup"
                ),
                isOn: Binding(
                    get: { model.isExtensionSetupMetadataApproved },
                    set: { model.setExtensionSetupMetadataApproved($0) }
                )
            )
            .disabled(!model.isSyncConfigured && !model.isExtensionSetupMetadataApproved)
            .accessibilityIdentifier("settings.extensions.shared")
            if model.isExtensionSetupMetadataApproved {
                ForEach(model.extensionSetupMetadata, id: \.extensionID) { setup in
                    LabeledContent(name(for: setup.extensionID), value: desiredLabel(setup))
                        .accessibilityIdentifier("settings.extensions.desired.\(setup.extensionID)")
                }
            }
            Toggle(
                CompanionL10n.string(
                    "settings.extensions.storage.share", fallback: "Show supported extension settings"
                ),
                isOn: Binding(
                    get: { model.isExtensionStorageMetadataApproved },
                    set: { model.setExtensionStorageMetadataApproved($0) }
                )
            )
            .disabled((!model.isSyncConfigured || !model.desiredSyncEnabled) &&
                !model.isExtensionStorageMetadataApproved)
            .accessibilityIdentifier("settings.extensions.storage.shared")
            if model.isExtensionStorageMetadataApproved {
                ForEach(model.extensionStorageMetadata, id: \.key) { setting in
                    LabeledContent(
                        "Vimium · " + CompanionL10n.string(
                            "settings.extensions.storage.\(setting.key)", fallback: setting.key
                        ), value: storageLabel(setting.value)
                    )
                    .accessibilityIdentifier("settings.extensions.storage.\(setting.key)")
                }
            }
#if DEBUG
            if ProcessInfo.processInfo.arguments.contains(MobileWebExtensionRuntime.launchArgument) {
                Button("Load unpacked Ahoi Spike from Files") {
                    showSpikeFolderPicker = true
                }
                .disabled(filesSpikeLoaded)
                .accessibilityIdentifier("settings.extensions.spike.import-folder")
                .fileImporter(isPresented: $showSpikeFolderPicker,
                              allowedContentTypes: [.folder]) { result in
                    switch result {
                    case .success(let url):
                        Task { @MainActor in
                            do {
                                try await MobileWebExtensionRuntime.shared.loadUnpackedFromFiles(url)
                                filesSpikeLoaded = true
                                filesSpikeStatus = "Unpacked test extension loaded"
                            } catch {
                                filesSpikeStatus = "Test extension import failed"
                            }
                        }
                    case .failure:
                        filesSpikeStatus = "Files selection failed"
                    }
                }
                if filesSpikeLoaded {
                    Button("Unload Files test extension") {
                        Task { @MainActor in
                            do {
                                try MobileWebExtensionRuntime.shared.unloadImportedFilesSpike()
                                filesSpikeLoaded = false
                                filesSpikeStatus = "Files test extension unloaded"
                            } catch {
                                filesSpikeStatus = "Test extension unload failed"
                            }
                        }
                    }
                    .accessibilityIdentifier("settings.extensions.spike.unload-folder")
                }
                Text(filesSpikeStatus.isEmpty ?
                     "Debug spike: choose an unpacked copy of the bundled Ahoi Spike fixture." :
                     filesSpikeStatus)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("settings.extensions.spike.files-status")
            }
#endif
        } header: {
            Text(CompanionL10n.string("settings.extensions.title", fallback: "Extensions"))
        } footer: {
            Text(CompanionL10n.string(
                "settings.extensions.metadata_only",
                fallback: "Shows the desired desktop configuration. Chromium extensions do not run on iOS."
            ))
        }
    }

    private func name(for id: String) -> String {
        model.snapshot.productRecords.extensionInventory.first {
            !$0.isDeleted && $0.extensionID == id && !$0.name.isEmpty
        }?.name ?? id
    }

    private func storageLabel(_ value: Bool?) -> String {
        guard let value else {
            return CompanionL10n.string("settings.extensions.storage.default", fallback: "Default")
        }
        return value ? CompanionL10n.string("settings.extensions.storage.on", fallback: "On") :
            CompanionL10n.string("settings.extensions.storage.off", fallback: "Off")
    }

    private func desiredLabel(_ setup: CompanionExtensionSetup) -> String {
        if !setup.installed {
            return CompanionL10n.string(
                "settings.extensions.desired_removed", fallback: "Desired: removed"
            )
        }
        return setup.enabled ? CompanionL10n.string(
            "settings.extensions.desired_enabled", fallback: "Desired: enabled"
        ) : CompanionL10n.string(
            "settings.extensions.desired_disabled", fallback: "Desired: disabled"
        )
    }
}
