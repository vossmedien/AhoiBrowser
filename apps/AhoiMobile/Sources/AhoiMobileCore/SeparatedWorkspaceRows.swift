import SwiftUI
import AhoiCloudKitSpike

/// Workspace-list rows for fully separated Workspaces (ADR 0011 level
/// `isolated`). Each is its own entry with its own sync opt-in; its content is
/// never shown in or merged into the main Workspace tree.
struct SeparatedWorkspaceRows: View {
    @ObservedObject var coordinator: SeparatedWorkspaceSyncCoordinator
    let accentTint: Color
    let onOpen: ((WorkspaceID) -> Void)?

    var body: some View {
        ForEach(coordinator.entries) { entry in
            HStack(spacing: 10) {
                Button {
                    onOpen?(WorkspaceID(rawValue: entry.workspaceID))
                } label: {
                    HStack(spacing: 10) {
                        Image(systemName: MobileWorkspaceIconPolicy.systemName(
                            for: entry.icon ?? ""
                        ))
                        .font(.body.weight(.semibold))
                        .foregroundStyle(accentTint)
                        .frame(width: 32, height: 32)
                        .background(accentTint.opacity(0.11),
                                    in: RoundedRectangle(cornerRadius: 9))
                        .accessibilityHidden(true)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(entry.displayName).lineLimit(1)
                            Text(SeparatedWorkspaceEntry.isolationLevelLabel)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                        }
                        Spacer(minLength: 8)
                    }
                    .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .disabled(onOpen == nil)
                Toggle(
                    CompanionL10n.string(
                        "workspace.separated.sync_toggle",
                        fallback: "Sync on this device"
                    ),
                    isOn: Binding(
                        get: { entry.syncEnabled },
                        set: { enabled in
                            Task { await coordinator.setSyncEnabled(enabled, for: entry.workspaceID) }
                        }
                    )
                )
                .labelsHidden()
                .accessibilityIdentifier(
                    "browser.workspace.separated.sync.\(entry.workspaceID.uuidString.lowercased())"
                )
            }
            .accessibilityIdentifier(
                "browser.workspace.separated.\(entry.workspaceID.uuidString.lowercased())"
            )
        }
    }
}
