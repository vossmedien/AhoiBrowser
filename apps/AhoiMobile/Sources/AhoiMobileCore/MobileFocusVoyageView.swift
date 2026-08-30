import SwiftUI

struct MobileFocusVoyageView: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency

    let mode: MobileBrowsingMode
    let workspaceName: String?
    let workspaceSystemImage: String
    let content: MobileFocusVoyageContent
    let accentTint: Color
    let onSearch: () -> Void
    let onOpen: (MobileFocusVoyageItem) -> Void

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 28) {
                voyageHeader
                searchButton

                if mode == .privateBrowsing {
                    privateExplanation
                } else {
                    if !content.journeyItems.isEmpty {
                        itemSection(
                            title: CompanionL10n.string(
                                "browser.focus.journey",
                                fallback: "Continue"
                            ),
                            items: content.journeyItems
                        )
                    }
                    if !content.savedItems.isEmpty {
                        itemSection(
                            title: CompanionL10n.string(
                                "browser.focus.saved",
                                fallback: "Saved"
                            ),
                            items: content.savedItems
                        )
                    }
                    if content.journeyItems.isEmpty && content.savedItems.isEmpty {
                        normalEmptyState
                    }
                }
            }
            .frame(maxWidth: 720, alignment: .leading)
            .padding(.horizontal, 22)
            .padding(.top, 42)
            .padding(.bottom, 36)
            .frame(maxWidth: .infinity)
        }
        .scrollIndicators(.hidden)
        .background(background)
        .animation(
            reduceMotion ? nil : .easeInOut(duration: MobileBrowserChromeTheme.motionDuration),
            value: content
        )
    }

    private var voyageHeader: some View {
        VStack(spacing: 11) {
            Image(systemName: mode == .privateBrowsing
                  ? "hand.raised.fill"
                  : "sailboat.fill")
                .font(.system(size: 42, weight: .semibold))
                .foregroundStyle(mode == .privateBrowsing
                                 ? accentTint
                                 : MobileBrowserChromeTheme.brandMoment)
                .accessibilityHidden(true)

            Label {
                Text(headerTitle)
                    .font(.title2.bold())
            } icon: {
                if mode == .normal {
                    Image(systemName: workspaceSystemImage)
                        .font(.subheadline.weight(.semibold))
                }
            }

            Text(headerSubtitle)
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier(mode == .privateBrowsing
                                 ? "browser.focus-voyage.private"
                                 : "browser.focus-voyage.header")
    }

    private var searchButton: some View {
        Button(action: onSearch) {
            HStack(spacing: 13) {
                Image(systemName: "magnifyingglass")
                    .font(.title3.weight(.semibold))
                    .foregroundStyle(accentTint)
                    .accessibilityHidden(true)
                Text(CompanionL10n.string(
                    "browser.focus.search",
                    fallback: "Search, address or command"
                ))
                .font(.body.weight(.medium))
                .foregroundStyle(.secondary)
                .lineLimit(1)
                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity, minHeight: 58)
            .padding(.horizontal, 18)
            .background(cardBackground, in: RoundedRectangle(
                cornerRadius: 22,
                style: .continuous
            ))
            .overlay {
                RoundedRectangle(cornerRadius: 22, style: .continuous)
                    .stroke(accentTint.opacity(0.20), lineWidth: 1)
                    .allowsHitTesting(false)
            }
            .shadow(color: accentTint.opacity(0.10), radius: 16, y: 7)
        }
        .buttonStyle(.plain)
        .keyboardShortcut("l", modifiers: .command)
        .accessibilityIdentifier("browser.focus-voyage.search")
        .accessibilityLabel(CompanionL10n.string(
            mode == .privateBrowsing
                ? "browser.private.address.accessibility"
                : "browser.address.accessibility",
            fallback: mode == .privateBrowsing
                ? "Private address and search"
                : "Address and search"
        ))
    }

    private func itemSection(
        title: String,
        items: [MobileFocusVoyageItem]
    ) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title)
                .font(.headline)
                .padding(.horizontal, 4)
            VStack(spacing: 0) {
                ForEach(Array(items.enumerated()), id: \.element.id) { index, item in
                    Button { onOpen(item) } label: {
                        HStack(spacing: 13) {
                            Image(systemName: item.systemImage)
                                .font(.body.weight(.semibold))
                                .foregroundStyle(accentTint)
                                .frame(width: 36, height: 36)
                                .background(
                                    accentTint.opacity(0.11),
                                    in: RoundedRectangle(cornerRadius: 10)
                                )
                                .accessibilityHidden(true)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(item.title)
                                    .font(.body.weight(.medium))
                                    .foregroundStyle(.primary)
                                    .lineLimit(1)
                                Text(item.subtitle)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                            Spacer(minLength: 8)
                            Image(systemName: "chevron.forward")
                                .font(.caption.weight(.bold))
                                .foregroundStyle(.tertiary)
                                .accessibilityHidden(true)
                        }
                        .frame(maxWidth: .infinity, minHeight: 62, alignment: .leading)
                        .padding(.horizontal, 14)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("browser.focus-voyage.item.\(item.id)")
                    .accessibilityHint(CompanionL10n.string(
                        "browser.history.open_hint",
                        fallback: "Opens this page in the current tab"
                    ))

                    if index < items.count - 1 {
                        Divider().padding(.leading, 63)
                    }
                }
            }
            .background(cardBackground, in: RoundedRectangle(
                cornerRadius: 20,
                style: .continuous
            ))
            .overlay {
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .stroke(Color.primary.opacity(0.075), lineWidth: 1)
                    .allowsHitTesting(false)
            }
        }
    }

    private var privateExplanation: some View {
        Label {
            Text(CompanionL10n.string(
                "browser.focus.private.message",
                fallback: "Private tabs, searches and page data stay out of Focus Voyage and Ahoi Sync."
            ))
            .font(.subheadline)
            .foregroundStyle(.secondary)
        } icon: {
            Image(systemName: "lock.shield.fill")
                .foregroundStyle(accentTint)
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(cardBackground, in: RoundedRectangle(cornerRadius: 18))
        .accessibilityIdentifier("browser.focus-voyage.private-explanation")
    }

    private var normalEmptyState: some View {
        Label {
            Text(CompanionL10n.string(
                "browser.focus.empty",
                fallback: "Recent normal tabs and saved pages will appear here."
            ))
            .font(.subheadline)
            .foregroundStyle(.secondary)
        } icon: {
            Image(systemName: "sparkles")
                .foregroundStyle(accentTint)
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(cardBackground, in: RoundedRectangle(cornerRadius: 18))
        .accessibilityIdentifier("browser.focus-voyage.empty")
    }

    @ViewBuilder
    private var background: some View {
        if mode == .privateBrowsing {
            MobileBrowserChromeTheme.privateBackground
                .overlay(alignment: .topLeading) {
                    RadialGradient(
                        colors: [accentTint.opacity(0.20), .clear],
                        center: .topLeading,
                        startRadius: 0,
                        endRadius: 460
                    )
                }
                .ignoresSafeArea()
        } else {
            Color(uiColor: .systemBackground)
                .overlay(alignment: .topLeading) {
                    RadialGradient(
                        colors: [
                            MobileBrowserChromeTheme.brandMoment.opacity(0.10),
                            accentTint.opacity(0.055),
                            .clear,
                        ],
                        center: .topLeading,
                        startRadius: 0,
                        endRadius: 520
                    )
                }
                .ignoresSafeArea()
        }
    }

    private var cardBackground: AnyShapeStyle {
        if reduceTransparency {
            return AnyShapeStyle(Color(uiColor: .secondarySystemBackground))
        }
        return AnyShapeStyle(.thinMaterial)
    }

    private var headerTitle: String {
        if mode == .privateBrowsing {
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        return workspaceName ?? "AhoiBrowser"
    }

    private var headerSubtitle: String {
        CompanionL10n.string(
            mode == .privateBrowsing
                ? "browser.focus.private.subtitle"
                : "browser.focus.subtitle",
            fallback: mode == .privateBrowsing
                ? "A separate, temporary voyage"
                : "Pick up where you left off"
        )
    }
}
