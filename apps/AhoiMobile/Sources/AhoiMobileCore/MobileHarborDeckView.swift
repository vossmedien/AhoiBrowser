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
        .buttonStyle(MobileChromeButtonStyle())
        .environment(\.colorScheme, mode == .privateBrowsing ? .dark : colorScheme)
    }

    private var deckContent: some View {
        VStack(spacing: 0) {
            workspaceRailSlot
            MobileHarborControlsLayout(isCollapsed: isCollapsed) {
                backButton
                collapsingControl {
                    forwardButton
                }
                addressButton
                collapsingControl {
                    reloadButton
                }
                tabsButton
                moreButton
            }
            .fixedSize(horizontal: false, vertical: true)
        }
        .animation(layoutAnimation, value: isCollapsed)
    }

    private var workspaceRailSlot: some View {
        MobileHarborRailLayout(
            expansion: isCollapsed ? 0 : 1,
            bottomSpacing: 7
        ) {
            workspaceRail
                .opacity(isCollapsed ? 0 : 1)
                .animation(contentAnimation, value: isCollapsed)
                // A custom Layout is not an accessibility container. Hide the
                // semantic rail on the actual HStack so its zero-height visual
                // placeholder cannot remain discoverable after compaction.
                .accessibilityHidden(isCollapsed)
        }
        .clipped()
        .allowsHitTesting(!isCollapsed)
    }

    private var workspaceRail: some View {
        HStack(spacing: 6) {
            Label {
                Text(deckTitle)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
                    .contentTransition(.opacity)
                    .animation(contentAnimation, value: deckTitle)
            } icon: {
                Image(systemName: workspaceSymbol)
                    .foregroundStyle(accentTint)
                    .contentTransition(symbolContentTransition)
                    .animation(contentAnimation, value: workspaceSymbol)
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
                    .contentTransition(symbolContentTransition)
                    .animation(contentAnimation, value: securitySystemImage)
                    .accessibilityHidden(true)
                Text(addressLabel)
                    .lineLimit(1)
                    .font(.subheadline.weight(.medium))
                    .contentTransition(.opacity)
                    .animation(contentAnimation, value: addressLabel)
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
                .contentTransition(symbolContentTransition)
                .animation(contentAnimation, value: isLoading)
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
                    .contentTransition(tabCountContentTransition)
                    .animation(contentAnimation, value: visibleTabCount)
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
        .accessibilityIdentifier(identifier)
        .accessibilityLabel(CompanionL10n.string(labelKey, fallback: fallback))
    }

    /// The layout owns the stable outer node and changes its geometry without
    /// a reduced-motion transaction. Only this inner content opacity animates.
    private func collapsingControl<Content: View>(
        @ViewBuilder content: () -> Content
    ) -> some View {
        ZStack {
            content()
                .opacity(isCollapsed ? 0 : 1)
                .animation(contentAnimation, value: isCollapsed)
                .allowsHitTesting(!isCollapsed)
                .accessibilityHidden(isCollapsed)
        }
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

    private var workspaceSymbol: String {
        mode == .privateBrowsing ? "hand.raised.fill" : workspaceSystemImage
    }

    private var contentAnimation: Animation {
        MobileBrowserChromeTheme.chromeContentAnimation(reduceMotion: reduceMotion)
    }

    private var layoutAnimation: Animation? {
        MobileBrowserChromeTheme.chromeAnimation(reduceMotion: reduceMotion)
    }

    private var symbolContentTransition: ContentTransition {
        reduceMotion ? .opacity : .symbolEffect(.replace)
    }

    private var tabCountContentTransition: ContentTransition {
        reduceMotion
            ? .opacity
            : .numericText(value: Double(visibleTabCount))
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

/// Keeps the workspace rail alive while its intrinsic height collapses to zero.
/// The layout value animates only in the normal-motion transaction; the rail's
/// opacity owns its separate reduced-motion-safe cross-fade.
private struct MobileHarborRailLayout: Layout {
    var expansion: CGFloat
    let bottomSpacing: CGFloat

    var animatableData: CGFloat {
        get { expansion }
        set { expansion = newValue }
    }

    func sizeThatFits(
        proposal: ProposedViewSize,
        subviews: Subviews,
        cache: inout ()
    ) -> CGSize {
        guard let subview = subviews.first else { return .zero }
        let proposedWidth = proposal.width.flatMap { width in
            width.isFinite ? width : nil
        }
        let size = subview.sizeThatFits(ProposedViewSize(
            width: proposedWidth,
            height: nil
        ))
        return CGSize(
            width: proposedWidth ?? size.width,
            height: max(0, size.height + bottomSpacing) * clampedExpansion
        )
    }

    func placeSubviews(
        in bounds: CGRect,
        proposal: ProposedViewSize,
        subviews: Subviews,
        cache: inout ()
    ) {
        guard let subview = subviews.first else { return }
        let size = subview.sizeThatFits(ProposedViewSize(
            width: bounds.width,
            height: nil
        ))
        subview.place(
            at: bounds.origin,
            anchor: .topLeading,
            proposal: ProposedViewSize(width: bounds.width, height: size.height)
        )
    }

    private var clampedExpansion: CGFloat {
        min(max(expansion, 0), 1)
    }
}

/// Lays out one stable control tree in all Harbor Deck states. Keeping the
/// subviews alive lets SwiftUI interpolate their frames instead of cross-fading
/// duplicate buttons when the deck collapses or expands.
private struct MobileHarborControlsLayout: Layout {
    private enum Control: Int {
        case back
        case forward
        case address
        case reload
        case tabs
        case more
    }

    private enum Arrangement {
        case collapsed
        case expandedRow
        case expandedStack
    }

    let isCollapsed: Bool

    private let itemSpacing: CGFloat = 6
    private let rowSpacing: CGFloat = 6
    private let minimumAddressWidth: CGFloat = 96
    private let minimumHitSize: CGFloat = 44

    func sizeThatFits(
        proposal: ProposedViewSize,
        subviews: Subviews,
        cache: inout ()
    ) -> CGSize {
        guard subviews.count == Control.more.rawValue + 1 else { return .zero }
        let sizes = measuredSizes(subviews)
        let width = resolvedWidth(proposal.width, sizes: sizes)
        return CGSize(
            width: width,
            height: layoutHeight(
                arrangement: arrangement(for: width, sizes: sizes),
                width: width,
                sizes: sizes,
                subviews: subviews
            )
        )
    }

    func placeSubviews(
        in bounds: CGRect,
        proposal: ProposedViewSize,
        subviews: Subviews,
        cache: inout ()
    ) {
        guard subviews.count == Control.more.rawValue + 1 else { return }
        let sizes = measuredSizes(subviews)
        switch arrangement(for: bounds.width, sizes: sizes) {
        case .collapsed:
            placeCollapsed(in: bounds, sizes: sizes, subviews: subviews)
        case .expandedRow:
            placeExpandedRow(in: bounds, sizes: sizes, subviews: subviews)
        case .expandedStack:
            placeExpandedStack(in: bounds, sizes: sizes, subviews: subviews)
        }
    }

    private func measuredSizes(_ subviews: Subviews) -> [CGSize] {
        subviews.map { subview in
            let size = subview.sizeThatFits(.unspecified)
            return CGSize(
                width: max(minimumHitSize, finite(size.width, fallback: minimumHitSize)),
                height: max(minimumHitSize, finite(size.height, fallback: minimumHitSize))
            )
        }
    }

    private func resolvedWidth(_ proposedWidth: CGFloat?, sizes: [CGSize]) -> CGFloat {
        let minimumWidth = minimumLayoutWidth(sizes: sizes)
        guard let proposedWidth, proposedWidth.isFinite else {
            let addressWidth = max(
                minimumAddressWidth,
                sizes[Control.address.rawValue].width
            )
            let controls = isCollapsed ? collapsedControls : expandedRowControls
            return controls.reduce(addressWidth) { result, control in
                result + sizes[control.rawValue].width
            } + itemSpacing * CGFloat(controls.count)
        }
        return max(minimumWidth, proposedWidth)
    }

    private func minimumLayoutWidth(sizes: [CGSize]) -> CGFloat {
        if isCollapsed {
            return collapsedControls.reduce(minimumAddressWidth) { result, control in
                result + sizes[control.rawValue].width
            } + itemSpacing * CGFloat(collapsedControls.count)
        }
        let bottomRowWidth = expandedStackControls.reduce(CGFloat.zero) {
            result, control in
            result + sizes[control.rawValue].width
        }
        return max(
            minimumAddressWidth,
            bottomRowWidth + itemSpacing * CGFloat(expandedStackControls.count - 1)
        )
    }

    private func arrangement(for width: CGFloat, sizes: [CGSize]) -> Arrangement {
        guard !isCollapsed else { return .collapsed }
        let fixedWidth = expandedRowControls.reduce(CGFloat.zero) { result, control in
            result + sizes[control.rawValue].width
        }
        let requiredWidth = fixedWidth + minimumAddressWidth +
            itemSpacing * CGFloat(expandedRowControls.count)
        return width >= requiredWidth ? .expandedRow : .expandedStack
    }

    private func layoutHeight(
        arrangement: Arrangement,
        width: CGFloat,
        sizes: [CGSize],
        subviews: Subviews
    ) -> CGFloat {
        switch arrangement {
        case .collapsed:
            return max(
                addressHeight(
                    width: addressWidth(
                        totalWidth: width,
                        controls: collapsedControls,
                        sizes: sizes
                    ),
                    subviews: subviews
                ),
                maximumHeight(of: collapsedControls, sizes: sizes)
            )
        case .expandedRow:
            return max(
                addressHeight(
                    width: addressWidth(
                        totalWidth: width,
                        controls: expandedRowControls,
                        sizes: sizes
                    ),
                    subviews: subviews
                ),
                maximumHeight(of: expandedRowControls, sizes: sizes)
            )
        case .expandedStack:
            return addressHeight(width: width, subviews: subviews) + rowSpacing +
                maximumHeight(of: expandedStackControls, sizes: sizes)
        }
    }

    private func placeCollapsed(
        in bounds: CGRect,
        sizes: [CGSize],
        subviews: Subviews
    ) {
        let addressWidth = addressWidth(
            totalWidth: bounds.width,
            controls: collapsedControls,
            sizes: sizes
        )
        let rowHeight = max(
            addressHeight(width: addressWidth, subviews: subviews),
            maximumHeight(of: collapsedControls, sizes: sizes)
        )
        placeRow(
            [.back, .address, .tabs, .more],
            in: bounds,
            rowHeight: rowHeight,
            addressWidth: addressWidth,
            sizes: sizes,
            subviews: subviews
        )

        // Hidden controls keep their identity and fade at their logical edge;
        // they remain outside hit testing and the accessibility projection.
        place(
            .forward,
            atX: bounds.minX + sizes[Control.back.rawValue].width + itemSpacing,
            rowY: bounds.minY,
            rowHeight: rowHeight,
            width: sizes[Control.forward.rawValue].width,
            sizes: sizes,
            subviews: subviews
        )
        let trailingWidth = sizes[Control.more.rawValue].width + itemSpacing +
            sizes[Control.tabs.rawValue].width
        place(
            .reload,
            atX: bounds.maxX - trailingWidth - itemSpacing -
                sizes[Control.reload.rawValue].width,
            rowY: bounds.minY,
            rowHeight: rowHeight,
            width: sizes[Control.reload.rawValue].width,
            sizes: sizes,
            subviews: subviews
        )
    }

    private func placeExpandedRow(
        in bounds: CGRect,
        sizes: [CGSize],
        subviews: Subviews
    ) {
        let addressWidth = addressWidth(
            totalWidth: bounds.width,
            controls: expandedRowControls,
            sizes: sizes
        )
        let rowHeight = max(
            addressHeight(width: addressWidth, subviews: subviews),
            maximumHeight(of: expandedRowControls, sizes: sizes)
        )
        placeRow(
            [.back, .forward, .address, .reload, .tabs, .more],
            in: bounds,
            rowHeight: rowHeight,
            addressWidth: addressWidth,
            sizes: sizes,
            subviews: subviews
        )
    }

    private func placeExpandedStack(
        in bounds: CGRect,
        sizes: [CGSize],
        subviews: Subviews
    ) {
        let addressHeight = addressHeight(width: bounds.width, subviews: subviews)
        place(
            .address,
            atX: bounds.minX,
            rowY: bounds.minY,
            rowHeight: addressHeight,
            width: bounds.width,
            sizes: sizes,
            subviews: subviews
        )

        let rowHeight = maximumHeight(of: expandedStackControls, sizes: sizes)
        let controlsWidth = expandedStackControls.reduce(CGFloat.zero) {
            result, control in
            result + sizes[control.rawValue].width
        }
        let distributedSpacing = max(
            itemSpacing,
            (bounds.width - controlsWidth) /
                CGFloat(expandedStackControls.count - 1)
        )
        var x = bounds.minX
        let rowY = bounds.minY + addressHeight + rowSpacing
        for control in expandedStackControls {
            place(
                control,
                atX: x,
                rowY: rowY,
                rowHeight: rowHeight,
                width: sizes[control.rawValue].width,
                sizes: sizes,
                subviews: subviews
            )
            x += sizes[control.rawValue].width + distributedSpacing
        }
    }

    private func placeRow(
        _ controls: [Control],
        in bounds: CGRect,
        rowHeight: CGFloat,
        addressWidth: CGFloat,
        sizes: [CGSize],
        subviews: Subviews
    ) {
        var x = bounds.minX
        for control in controls {
            let width = control == .address
                ? addressWidth
                : sizes[control.rawValue].width
            place(
                control,
                atX: x,
                rowY: bounds.minY,
                rowHeight: rowHeight,
                width: width,
                sizes: sizes,
                subviews: subviews
            )
            x += width + itemSpacing
        }
    }

    private func place(
        _ control: Control,
        atX x: CGFloat,
        rowY: CGFloat,
        rowHeight: CGFloat,
        width: CGFloat,
        sizes: [CGSize],
        subviews: Subviews
    ) {
        let height = control == .address
            ? rowHeight
            : sizes[control.rawValue].height
        subviews[control.rawValue].place(
            at: CGPoint(
                x: x,
                y: rowY + (rowHeight - height) / 2
            ),
            anchor: .topLeading,
            proposal: ProposedViewSize(width: width, height: height)
        )
    }

    private func addressWidth(
        totalWidth: CGFloat,
        controls: [Control],
        sizes: [CGSize]
    ) -> CGFloat {
        let fixedWidth = controls.reduce(CGFloat.zero) { result, control in
            result + sizes[control.rawValue].width
        }
        return max(
            minimumAddressWidth,
            totalWidth - fixedWidth - itemSpacing * CGFloat(controls.count)
        )
    }

    private func addressHeight(width: CGFloat, subviews: Subviews) -> CGFloat {
        let size = subviews[Control.address.rawValue].sizeThatFits(
            ProposedViewSize(width: width, height: nil)
        )
        return max(minimumHitSize, finite(size.height, fallback: minimumHitSize))
    }

    private func maximumHeight(of controls: [Control], sizes: [CGSize]) -> CGFloat {
        controls.reduce(minimumHitSize) { result, control in
            max(result, sizes[control.rawValue].height)
        }
    }

    private func finite(_ value: CGFloat, fallback: CGFloat) -> CGFloat {
        value.isFinite ? value : fallback
    }

    private var collapsedControls: [Control] {
        [.back, .tabs, .more]
    }

    private var expandedRowControls: [Control] {
        [.back, .forward, .reload, .tabs, .more]
    }

    private var expandedStackControls: [Control] {
        expandedRowControls
    }
}

private struct MobileChromeButtonStyle: ButtonStyle {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .opacity(configuration.isPressed ? 0.72 : 1)
            .scaleEffect(configuration.isPressed && !reduceMotion ? 0.96 : 1)
            .animation(
                MobileBrowserChromeTheme.chromeAnimation(reduceMotion: reduceMotion),
                value: configuration.isPressed
            )
    }
}
