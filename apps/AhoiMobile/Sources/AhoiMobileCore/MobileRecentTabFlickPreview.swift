import SwiftUI
import UIKit

/// The drag state of a recent-tab flick on the address control. The same
/// thresholds decide whether a drag shows a preview and whether lifting
/// the finger switches tabs, so the preview never promises a switch that
/// does not happen.
struct MobileRecentTabFlickState: Equatable {
    static let previewDistance: CGFloat = 28
    static let commitDistance: CGFloat = 72
    static let dominance: CGFloat = 1.35

    let tab: MobileTabRecord
    let direction: Int
    let willCommit: Bool
    /// How far the card follows the finger.
    let offset: CGFloat

    init(tab: MobileTabRecord, direction: Int, translation: CGSize) {
        self.tab = tab
        self.direction = direction
        willCommit = Self.direction(of: translation, committing: true)
            == direction
        offset = max(-40, min(40, translation.width * 0.3))
    }

    /// 1 for a rightward drag (older tab), -1 for leftward, nil when the
    /// drag is too short or not horizontal enough.
    static func direction(of translation: CGSize, committing: Bool) -> Int? {
        let horizontal = translation.width
        let needed = committing ? commitDistance : previewDistance
        guard abs(horizontal) >= needed,
              abs(horizontal) > abs(translation.height) * dominance
        else { return nil }
        return horizontal > 0 ? 1 : -1
    }
}

/// The neighbor tab a recent-tab flick would reach, shown above the
/// Harbor Deck's address control while the finger is still down
/// (ADR 0012 section 3, MOB-FLICK-01). It names the page by favicon,
/// title and host and follows the finger a little; lifting the finger
/// past the commit distance switches to it, anything shorter keeps the
/// current tab.
struct MobileRecentTabFlickPreview: View {
    let tab: MobileTabRecord
    /// 1: an older tab (rightward drag), -1: back toward newer tabs.
    let direction: Int
    /// Whether the drag has passed the commit distance.
    let willCommit: Bool
    let accentTint: Color

    var body: some View {
        HStack(spacing: 9) {
            if direction > 0 { chevron("chevron.backward") }
            icon
            VStack(alignment: .leading, spacing: 1) {
                Text(tab.displayTitle)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
                if let host {
                    Text(host)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            if direction < 0 { chevron("chevron.forward") }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .frame(maxWidth: 280)
        .background(.regularMaterial, in: Capsule())
        .overlay {
            Capsule().stroke(
                accentTint.opacity(willCommit ? 0.55 : 0.22),
                lineWidth: willCommit ? 1.5 : 1
            )
        }
        .shadow(color: .black.opacity(0.14), radius: 12, y: 5)
        .accessibilityElement(children: .ignore)
        .accessibilityIdentifier("browser.tabs.flick-preview")
        .accessibilityLabel(tab.displayTitle)
        .accessibilityValue(tab.url ?? "")
    }

    private var host: String? {
        tab.url.flatMap(URL.init(string:))?.host()
    }

    private func chevron(_ name: String) -> some View {
        Image(systemName: name)
            .font(.caption.weight(.bold))
            .foregroundStyle(accentTint)
    }

    @ViewBuilder
    private var icon: some View {
        if let data = tab.faviconData, let image = UIImage(data: data) {
            Image(uiImage: image)
                .resizable()
                .scaledToFit()
                .frame(width: 20, height: 20)
                .clipShape(RoundedRectangle(
                    cornerRadius: 5, style: .continuous
                ))
        } else {
            Image(systemName: "globe")
                .foregroundStyle(.secondary)
                .frame(width: 20, height: 20)
        }
    }
}
