import SwiftUI

/// Lays out one stable control tree in all Harbor Deck states. Keeping the
/// subviews alive lets SwiftUI interpolate their frames instead of cross-fading
/// duplicate buttons when the deck collapses or expands.
struct MobileHarborControlsLayout: Layout {
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
        case stacked
    }

    let isCollapsed: Bool
    let usesAccessibilityLayout: Bool
    private let itemSpacing: CGFloat = 6
    private let rowSpacing: CGFloat = 6
    // The native actions keep 44pt hit targets. Reserve a readable short host
    // instead of accepting a 96pt capsule whose icons/padding leave only "exa…".
    // Narrow expanded chrome uses our existing stacked arrangement; compact
    // chrome already has fewer buttons and normally keeps its single row.
    private let minimumAddressWidth: CGFloat = 180
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
        case .stacked:
            placeStacked(in: bounds, sizes: sizes, subviews: subviews)
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
        let controls = isCollapsed ? collapsedControls : expandedRowControls
        let naturalWidth = controls.reduce(minimumAddressWidth) {
            $0 + sizes[$1.rawValue].width
        } + itemSpacing * CGFloat(controls.count)
        return MobileHarborLayoutPolicy.resolvedWidth(
            proposedWidth: proposedWidth,
            naturalWidth: naturalWidth
        )
    }

    private func arrangement(for width: CGFloat, sizes: [CGSize]) -> Arrangement {
        if usesAccessibilityLayout { return .stacked }
        let controls = isCollapsed ? collapsedControls : expandedRowControls
        let fixedWidth = controls.reduce(CGFloat.zero) { result, control in
            result + sizes[control.rawValue].width
        }
        let requiredWidth = fixedWidth + minimumAddressWidth +
            itemSpacing * CGFloat(controls.count)
        guard width >= requiredWidth else { return .stacked }
        return isCollapsed ? .collapsed : .expandedRow
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
        case .stacked:
            let controls = isCollapsed ? collapsedControls : expandedStackControls
            let rows = controlRows(controls, width: width, sizes: sizes)
            return addressHeight(width: width, subviews: subviews) + rowSpacing +
                rows.enumerated().reduce(CGFloat.zero) { result, item in
                    result + maximumHeight(of: item.element, sizes: sizes) +
                        (item.offset == 0 ? 0 : rowSpacing)
                }
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

    private func placeStacked(
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

        let controls = isCollapsed ? collapsedControls : expandedStackControls
        var rowY = bounds.minY + addressHeight + rowSpacing
        for row in controlRows(controls, width: bounds.width, sizes: sizes) {
            let widths = row.map {
                min(sizes[$0.rawValue].width, max(0, bounds.width))
            }
            let controlsWidth = widths.reduce(0, +)
            let spacing = row.count > 1
                ? max(itemSpacing, (bounds.width - controlsWidth) / CGFloat(row.count - 1))
                : 0
            let rowHeight = maximumHeight(of: row, sizes: sizes)
            var x = bounds.minX + (row.count == 1
                ? max(0, bounds.width - controlsWidth) / 2
                : 0)
            for (control, width) in zip(row, widths) {
                place(
                    control,
                    atX: x,
                    rowY: rowY,
                    rowHeight: rowHeight,
                    width: width,
                    sizes: sizes,
                    subviews: subviews
                )
                x += width + spacing
            }
            rowY += rowHeight + rowSpacing
        }
        if isCollapsed {
            for control in [Control.forward, .reload] {
                place(
                    control,
                    atX: bounds.minX,
                    rowY: bounds.minY + addressHeight + rowSpacing,
                    rowHeight: minimumHitSize,
                    width: min(sizes[control.rawValue].width, max(0, bounds.width)),
                    sizes: sizes,
                    subviews: subviews
                )
            }
        }
    }
    private func controlRows(
        _ controls: [Control],
        width: CGFloat,
        sizes: [CGSize]
    ) -> [[Control]] {
        MobileHarborLayoutPolicy.rows(
            itemWidths: controls.map { sizes[$0.rawValue].width },
            availableWidth: width,
            spacing: itemSpacing
        ).map { row in row.map { controls[$0] } }
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
