import SwiftUI

struct MobileHarborDeckView: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.colorScheme) private var colorScheme

    let mode: MobileBrowsingMode
    let isCollapsed: Bool
    let workspaceName: String?
    let workspaceSystemImage: String
    let accentTint: Color
    let addressLabel: String
    let addressAccessibilityValue: String
    let securitySystemImage: String
    let visibleTabCount: Int
    let canGoBack: Bool
    let canGoForward: Bool
    let isLoading: Bool
    let canSwitchWorkspace: Bool
    let onGoBack: () -> Void
    let onGoForward: () -> Void
    let onPresentAddress: () -> Void
    let onReloadOrStop: () -> Void
    let onPresentTabs: () -> Void
    let onPresentMore: () -> Void
    let onSwitchWorkspace: (Int) -> Void

    var body: some View {
        deckContent
            .animation(
                reduceMotion ? nil : .easeInOut(
                    duration: MobileBrowserChromeTheme.motionDuration
                ),
                value: isCollapsed
            )
        .font(.body.weight(.semibold))
        .padding(.horizontal, 10)
        .padding(.vertical, 9)
        .background(cardBackground, in: RoundedRectangle(
            cornerRadius: 24,
            style: .continuous
        ))
        .overlay {
            ZStack {
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .fill(accentTint.opacity(reduceTransparency ? 0.13 : 0.085))
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .stroke(accentTint.opacity(0.20), lineWidth: 1)
            }
            .allowsHitTesting(false)
        }
        .shadow(color: Color.black.opacity(0.13), radius: 16, y: 7)
        .padding(.horizontal, 8)
        .padding(.vertical, 7)
        .background(mode == .privateBrowsing
                    ? MobileBrowserChromeTheme.privateBackground
                    : Color(uiColor: .systemBackground))
        .buttonStyle(.plain)
        .environment(\.colorScheme, mode == .privateBrowsing ? .dark : colorScheme)
    }

    @ViewBuilder
    private var deckContent: some View {
        if isCollapsed {
            HStack(spacing: 6) {
                backButton
                addressButton
                tabsButton
                moreButton
            }
        } else {
        VStack(spacing: 7) {
            workspaceRail
            ViewThatFits(in: .horizontal) {
                expandedControls
                compactControls
            }
        }
        }
    }

    private var workspaceRail: some View {
        HStack(spacing: 6) {
            Label {
                Text(deckTitle)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
            } icon: {
                Image(systemName: mode == .privateBrowsing
                      ? "hand.raised.fill"
                      : workspaceSystemImage)
                    .foregroundStyle(accentTint)
                    .accessibilityHidden(true)
            }
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            .accessibilityIdentifier("browser.harbor-deck.workspace")

            if canSwitchWorkspace {
                workspaceButton(
                    systemImage: "chevron.backward",
                    labelKey: "browser.workspace.previous",
                    fallback: "Previous workspace",
                    identifier: "browser.workspace.previous",
                    direction: -1
                )
                workspaceButton(
                    systemImage: "chevron.forward",
                    labelKey: "browser.workspace.next",
                    fallback: "Next workspace",
                    identifier: "browser.workspace.next",
                    direction: 1
                )
            }
        }
        .padding(.leading, 10)
        .contentShape(Rectangle())
        .simultaneousGesture(
            DragGesture(minimumDistance: 28).onEnded { value in
                let horizontal = value.translation.width
                let vertical = abs(value.translation.height)
                guard canSwitchWorkspace,
                      abs(horizontal) >= 72,
                      abs(horizontal) > vertical * 1.35 else { return }
                onSwitchWorkspace(horizontal < 0 ? 1 : -1)
            }
        )
    }

    private var expandedControls: some View {
        HStack(spacing: 6) {
            backButton
            forwardButton
            addressButton.frame(minWidth: 96)
            reloadButton
            tabsButton
            moreButton
        }
    }

    private var compactControls: some View {
        VStack(spacing: 6) {
            addressButton
            HStack(spacing: 8) {
                backButton
                Spacer(minLength: 0)
                forwardButton
                Spacer(minLength: 0)
                reloadButton
                Spacer(minLength: 0)
                tabsButton
                Spacer(minLength: 0)
                moreButton
            }
        }
    }

    private var backButton: some View {
        Button(action: onGoBack) {
            chromeIcon("chevron.backward")
        }
        .disabled(!canGoBack)
        .keyboardShortcut("[", modifiers: .command)
        .accessibilityIdentifier("browser.back")
        .accessibilityLabel(CompanionL10n.string("browser.back", fallback: "Back"))
    }

    private var forwardButton: some View {
        Button(action: onGoForward) {
            chromeIcon("chevron.forward")
        }
        .disabled(!canGoForward)
        .keyboardShortcut("]", modifiers: .command)
        .accessibilityIdentifier("browser.forward")
        .accessibilityLabel(CompanionL10n.string("browser.forward", fallback: "Forward"))
    }

    private var addressButton: some View {
        Button(action: onPresentAddress) {
            HStack(spacing: 7) {
                if mode == .normal {
                    Image(systemName: "sailboat.fill")
                        .font(.caption2.weight(.bold))
                        .foregroundStyle(MobileBrowserChromeTheme.brandMoment)
                        .accessibilityHidden(true)
                }
                Image(systemName: securitySystemImage)
                    .font(.caption)
                    .accessibilityHidden(true)
                Text(addressLabel)
                    .lineLimit(1)
                    .font(.subheadline.weight(.medium))
                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity, minHeight: 44)
            .padding(.horizontal, 12)
            .background(addressBackground, in: Capsule())
            .overlay {
                ZStack {
                    Capsule().fill(accentTint.opacity(reduceTransparency ? 0.12 : 0.075))
                    Capsule().stroke(accentTint.opacity(0.18), lineWidth: 1)
                }
                .allowsHitTesting(false)
            }
        }
        .buttonStyle(.plain)
        .keyboardShortcut("l", modifiers: .command)
        .accessibilityIdentifier(mode == .privateBrowsing
                                 ? "browser.address.private"
                                 : "browser.address")
        .accessibilityLabel(CompanionL10n.string(
            mode == .privateBrowsing
                ? "browser.private.address.accessibility"
                : "browser.address.accessibility",
            fallback: mode == .privateBrowsing
                ? "Private address and search"
                : "Address and search"
        ))
        .accessibilityValue(Text(addressAccessibilityValue))
    }

    private var reloadButton: some View {
        Button(action: onReloadOrStop) {
            chromeIcon(isLoading ? "xmark" : "arrow.clockwise")
        }
        .keyboardShortcut("r", modifiers: .command)
        .accessibilityIdentifier("browser.reload-stop")
        .accessibilityLabel(CompanionL10n.string(
            isLoading ? "browser.stop" : "browser.reload",
            fallback: isLoading ? "Stop loading" : "Reload"
        ))
    }

    private var tabsButton: some View {
        Button(action: onPresentTabs) {
            ZStack {
                RoundedRectangle(cornerRadius: 5)
                    .stroke(lineWidth: 1.5)
                    .frame(width: 24, height: 24)
                Text("\(visibleTabCount)")
                    .font(.caption2.monospacedDigit().weight(.bold))
            }
            .frame(width: 44, height: 44)
            .contentShape(Rectangle())
        }
        .accessibilityIdentifier("browser.tabs")
        .accessibilityLabel(CompanionL10n.format(
            "browser.tabs.count",
            fallback: "%d tabs",
            visibleTabCount
        ))
    }

    private var moreButton: some View {
        Button(action: onPresentMore) {
            chromeIcon("ellipsis.circle")
        }
        .accessibilityIdentifier("browser.more")
        .accessibilityLabel(CompanionL10n.string(
            "browser.more.accessibility",
            fallback: "More browser actions"
        ))
    }

    private func workspaceButton(
        systemImage: String,
        labelKey: String,
        fallback: String,
        identifier: String,
        direction: Int
    ) -> some View {
        Button { onSwitchWorkspace(direction) } label: {
            chromeIcon(systemImage)
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(identifier)
        .accessibilityLabel(CompanionL10n.string(labelKey, fallback: fallback))
    }

    private func chromeIcon(_ systemName: String) -> some View {
        Image(systemName: systemName)
            .frame(width: 44, height: 44)
            .contentShape(Rectangle())
    }

    private var deckTitle: String {
        if mode == .privateBrowsing {
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        return workspaceName ?? CompanionL10n.string(
            "browser.tabs.unassigned",
            fallback: "No workspace"
        )
    }

    private var cardBackground: AnyShapeStyle {
        if reduceTransparency {
            return AnyShapeStyle(Color(uiColor: .secondarySystemBackground))
        }
        return AnyShapeStyle(.regularMaterial)
    }

    private var addressBackground: AnyShapeStyle {
        if reduceTransparency {
            return AnyShapeStyle(Color(uiColor: .tertiarySystemBackground))
        }
        return AnyShapeStyle(.ultraThinMaterial)
    }
}
