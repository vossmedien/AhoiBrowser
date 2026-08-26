import SwiftUI
import AhoiCloudKitSpike

public struct CompanionSendLinkView: View {
    @ObservedObject private var model: CompanionAppModel
    @Environment(\.dismiss) private var dismiss
    @State private var url = ""
    @State private var targetDeviceID: DeviceID?
    @State private var targetWorkspaceID: WorkspaceID?
    @State private var isSending = false

    public init(model: CompanionAppModel) {
        self.model = model
    }

    public var body: some View {
        NavigationStack {
            Form {
                Section {
#if os(iOS)
                    TextField("https://example.com", text: $url)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
#else
                    TextField("https://example.com", text: $url)
#endif
                } header: {
                    Text(CompanionL10n.string(
                        "send_link.url",
                        fallback: "Link"
                    ))
                }

                Section {
                    Picker(
                        CompanionL10n.string(
                            "send_link.device",
                            fallback: "Mac"
                        ),
                        selection: $targetDeviceID
                    ) {
                        Text(CompanionL10n.string(
                            "send_link.select_device",
                            fallback: "Choose a Mac"
                        )).tag(DeviceID?.none)
                        ForEach(targetDevices) { device in
                            Text(device.name).tag(Optional(device.id))
                        }
                    }
                    Picker(
                        CompanionL10n.string(
                            "send_link.workspace",
                            fallback: "Workspace"
                        ),
                        selection: $targetWorkspaceID
                    ) {
                        Text(CompanionL10n.string(
                            "send_link.workspace.none",
                            fallback: "No specific workspace"
                        )).tag(WorkspaceID?.none)
                        ForEach(model.snapshot.visibleWorkspaces) { workspace in
                            Text(workspace.name).tag(Optional(workspace.id))
                        }
                    }
                } footer: {
                    Text(CompanionL10n.string(
                        "send_link.footer",
                        fallback: "Only HTTP and HTTPS links are accepted. The selected Mac must have approved this device."
                    ))
                }

                if let status = model.remoteCommandStatus {
                    Section {
                        Text(status)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle(CompanionL10n.string(
                "send_link.title",
                fallback: "Send link"
            ))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(CompanionL10n.string(
                        "action.cancel",
                        fallback: "Cancel"
                    )) {
                        dismiss()
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(CompanionL10n.string(
                        "action.send",
                        fallback: "Send"
                    )) {
                        send()
                    }
                    .disabled(!canSend || isSending)
                }
            }
            .onAppear {
                targetDeviceID = targetDeviceID ?? targetDevices.first?.id
            }
        }
    }

    private var targetDevices: [Device] {
        model.snapshot.devices.filter {
            $0.kind == .mac && !$0.isDeleted && !$0.isRevoked
        }.sorted {
            $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
    }

    private var canSend: Bool {
        guard targetDeviceID != nil,
              let components = URLComponents(string: url),
              let scheme = components.scheme?.lowercased(),
              scheme == "http" || scheme == "https" else {
            return false
        }
        return components.host?.isEmpty == false && components.user == nil &&
            components.password == nil
    }

    private func send() {
        guard let targetDeviceID, canSend else { return }
        isSending = true
        Task {
            await model.sendLink(
                url.trimmingCharacters(in: .whitespacesAndNewlines),
                to: targetDeviceID,
                workspaceID: targetWorkspaceID
            )
            isSending = false
        }
    }
}
