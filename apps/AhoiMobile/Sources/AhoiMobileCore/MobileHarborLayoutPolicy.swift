import CoreGraphics

enum MobileHarborLayoutPolicy {
    static func resolvedWidth(
        proposedWidth: CGFloat?,
        naturalWidth: CGFloat
    ) -> CGFloat {
        guard let proposedWidth, proposedWidth.isFinite else {
            return max(0, naturalWidth)
        }
        // A Layout must honor a finite parent proposal. Reporting a wider
        // minimum size makes NavigationSplitView overflow its compact column.
        return max(0, proposedWidth)
    }

    static func rows(
        itemWidths: [CGFloat],
        availableWidth: CGFloat,
        spacing: CGFloat
    ) -> [[Int]] {
        guard !itemWidths.isEmpty else { return [] }
        let availableWidth = max(0, availableWidth)
        let spacing = max(0, spacing)
        var result: [[Int]] = []
        var row: [Int] = []
        var occupiedWidth: CGFloat = 0

        for (index, rawWidth) in itemWidths.enumerated() {
            let width = min(max(0, rawWidth), availableWidth)
            let candidateWidth = row.isEmpty
                ? width
                : occupiedWidth + spacing + width
            if !row.isEmpty, candidateWidth > availableWidth {
                result.append(row)
                row = [index]
                occupiedWidth = width
            } else {
                row.append(index)
                occupiedWidth = candidateWidth
            }
        }
        if !row.isEmpty { result.append(row) }
        return result
    }
}
