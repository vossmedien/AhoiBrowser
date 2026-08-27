import SwiftUI
import AhoiCloudKitSpike

public struct RemoteTabRow: View {
    public let tab: RemoteTab
    public let openURL: OpenURLAction
    public let remoteControlAvailable: Bool
    public let onRemoteOpen: (() -> Void)?
    public let onRemoteFocus: (() -> Void)?
    public let onRemoteClose: (() -> Void)?
    public let accentTint: Color
    @State private var closeConfirmationPresented = false

    public init(
        tab: RemoteTab,
        openURL: OpenURLAction,
        remoteControlAvailable: Bool = false,
        onRemoteOpen: (() -> Void)? = nil,
        onRemoteFocus: (() -> Void)? = nil,
        onRemoteClose: (() -> Void)? = nil,
        accentTint: Color = .accentColor
    ) {
        self.tab = tab
        self.openURL = openURL
        self.remoteControlAvailable = remoteControlAvailable
        self.onRemoteOpen = onRemoteOpen
        self.onRemoteFocus = onRemoteFocus
        self.onRemoteClose = onRemoteClose
        self.accentTint = accentTint
    }

    public var body: some View {
        HStack(spacing: 8) {
            Button {
                guard let url = URL(string: tab.url) else { return }
                openURL(url)
            } label: {
                HStack(spacing: 10) {
                    Image(systemName: deviceSymbol)
                        .font(.body.weight(.semibold))
                        .foregroundStyle(accentTint)
                        .frame(width: 32, height: 32)
                        .background(accentTint.opacity(0.12), in: RoundedRectangle(cornerRadius: 9))
                        .accessibilityLabel(deviceKindLabel)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(tab.title.isEmpty ? tab.url : tab.title)
                            .lineLimit(1)
                        Text(CompanionL10n.format(
                            "remote_tab.subtitle",
                            fallback: "%@ · %@",
                            tab.deviceName,
                            tab.workspaceName ?? CompanionL10n.string(
                                "workspace.none",
                                fallback: "No workspace"
                            )
                        ))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                    }
                    Spacer(minLength: 8)
                    Text(Date(
                        timeIntervalSince1970: Double(
                            tab.lastActiveAt.physicalMilliseconds
                        ) / 1_000
                    ), style: .relative)
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.tertiary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .buttonStyle(.plain)
            if tab.deviceKind == .mac {
                Menu {
                    Button(CompanionL10n.string(
                        "remote.open_on_mac",
                        fallback: "Open on Mac"
                    ), action: onRemoteOpen ?? {})
                    .disabled(!remoteControlAvailable || onRemoteOpen == nil)
                    Button(CompanionL10n.string(
                        "remote.focus_mac_tab",
                        fallback: "Focus Mac tab"
                    ), action: onRemoteFocus ?? {})
                    .disabled(!remoteControlAvailable || onRemoteFocus == nil)
                    Button(
                        CompanionL10n.string(
                            "remote.close_tab",
                            fallback: "Close Mac tab"
                        ),
                        role: .destructive
                    ) {
                        closeConfirmationPresented = true
                    }
                    .disabled(!remoteControlAvailable || onRemoteClose == nil)
                    if !remoteControlAvailable {
                        Text(CompanionL10n.string(
                            "remote.signing_key_missing",
                            fallback: "Signing key or Mac approval is missing"
                        ))
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                        .accessibilityLabel(CompanionL10n.string(
                            "remote.menu.accessibility",
                            fallback: "Secure Mac control"
                        ))
                }
            }
        }
        .padding(.vertical, 5)
        .padding(.horizontal, 7)
        .background(accentTint.opacity(0.045), in: RoundedRectangle(cornerRadius: 12))
        .overlay {
            RoundedRectangle(cornerRadius: 12)
                .stroke(accentTint.opacity(0.10), lineWidth: 1)
                .allowsHitTesting(false)
        }
        .accessibilityLabel(CompanionL10n.format(
            "remote_tab.accessibility",
            fallback: "%@, %@, normal tab",
            tab.title,
            tab.deviceName
        ))
        .accessibilityHint(CompanionL10n.string(
            "remote_tab.open_hint",
            fallback: "Opens the URL in the default browser"
        ))
        .confirmationDialog(
            CompanionL10n.string(
                "remote.close_confirmation.title",
                fallback: "Close this tab on the Mac?"
            ),
            isPresented: $closeConfirmationPresented,
            titleVisibility: .visible
        ) {
            Button(
                CompanionL10n.string(
                    "remote.close_confirmation.confirm",
                    fallback: "Close tab"
                ),
                role: .destructive,
                action: onRemoteClose ?? {}
            )
            Button(CompanionL10n.string(
                "action.cancel",
                fallback: "Cancel"
            ), role: .cancel) {}
        } message: {
            Text(CompanionL10n.format(
                "remote.close_confirmation.message",
                fallback: "A signed close command will be sent to %@.",
                tab.deviceName
            ))
        }
    }

    private var deviceSymbol: String {
        switch tab.deviceKind {
        case .mac: "desktopcomputer"
        case .iPhone: "iphone"
        case .iPad: "ipad"
        }
    }

    private var deviceKindLabel: String {
        switch tab.deviceKind {
        case .mac: CompanionL10n.string("device.kind.mac", fallback: "Mac")
        case .iPhone: CompanionL10n.string("device.kind.iphone", fallback: "iPhone")
        case .iPad: CompanionL10n.string("device.kind.ipad", fallback: "iPad")
        }
    }
}
