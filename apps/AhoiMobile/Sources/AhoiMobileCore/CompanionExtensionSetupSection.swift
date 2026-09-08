import SwiftUI

struct CompanionExtensionSetupSection: View {
    @ObservedObject var model: CompanionAppModel

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
