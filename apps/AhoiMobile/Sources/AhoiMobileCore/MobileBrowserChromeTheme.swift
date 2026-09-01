import Foundation
import SwiftUI

private struct MobileBrowserReduceMotionOverrideKey: EnvironmentKey {
    static let defaultValue: Bool? = nil
}

extension EnvironmentValues {
    var mobileBrowserReduceMotionOverride: Bool? {
        get { self[MobileBrowserReduceMotionOverrideKey.self] }
        set { self[MobileBrowserReduceMotionOverrideKey.self] = newValue }
    }
}

enum MobileBrowserChromeTheme {
    struct RGB: Equatable, Sendable {
        let red: Double
        let green: Double
        let blue: Double
    }

    static let motionDuration = 0.18
    static let collapseMotionDuration = 0.145
    static let reducedMotionCrossfadeDuration = 0.18
    static let pressMotionDuration = 0.11
    static let compactHarborDeckHeight: CGFloat = 60
    static let privateBackground = Color(red: 0.055, green: 0.060, blue: 0.085)
    static let privateAccent = Color(red: 0.53, green: 0.48, blue: 0.88)
    static let functionalAccent = Color(red: 0.10, green: 0.43, blue: 0.84)
    static let brandMoment = Color(red: 0.95, green: 0.45, blue: 0.15)

    static func chromeAnimation(reduceMotion: Bool) -> Animation? {
        chromeAnimation(toCollapsed: false, reduceMotion: reduceMotion)
    }

    static func chromeAnimation(
        toCollapsed: Bool,
        reduceMotion: Bool
    ) -> Animation? {
        guard !reduceMotion else { return nil }
        return toCollapsed
            ? .timingCurve(0.4, 0, 1, 1, duration: collapseMotionDuration)
            : .timingCurve(0.2, 0, 0, 1, duration: motionDuration)
    }

    /// Content changes may still cross-fade when Reduce Motion is enabled.
    /// Keep this animation scoped to opacity/content transitions so layout,
    /// position, scale, and symbol motion continue to update immediately.
    static func chromeContentAnimation(
        toCollapsed: Bool = false,
        reduceMotion: Bool
    ) -> Animation {
        guard !reduceMotion else {
            return .easeInOut(duration: reducedMotionCrossfadeDuration)
        }
        return chromeAnimation(toCollapsed: toCollapsed, reduceMotion: false)
            ?? .linear(duration: 0)
    }

    static func chromePressAnimation(reduceMotion: Bool) -> Animation? {
        guard !reduceMotion else { return nil }
        return .easeOut(duration: pressMotionDuration)
    }

    static func chromeTint(
        websiteTintARGB: UInt32?,
        mode: MobileBrowsingMode,
        colorScheme: ColorScheme
    ) -> Color {
        guard mode == .normal else { return privateAccent }
        guard let websiteTintARGB else { return functionalAccent }
        let channels = contrastAdjustedTint(
            red: Double((websiteTintARGB >> 16) & 0xFF) / 255,
            green: Double((websiteTintARGB >> 8) & 0xFF) / 255,
            blue: Double(websiteTintARGB & 0xFF) / 255,
            colorScheme: colorScheme
        )
        return Color(red: channels.red, green: channels.green, blue: channels.blue)
    }

    /// Website colors remain recognizable, while functional controls retain
    /// at least the WCAG 3:1 non-text contrast floor against the active system
    /// background.
    static func contrastAdjustedTint(
        red: Double,
        green: Double,
        blue: Double,
        colorScheme: ColorScheme
    ) -> RGB {
        let backgroundLuminance = colorScheme == .dark ? 0.0 : 1.0
        var candidate = RGB(red: red, green: green, blue: blue)
        for _ in 0..<32 {
            let luminance = relativeLuminance(candidate)
            let contrast = (max(luminance, backgroundLuminance) + 0.05) /
                (min(luminance, backgroundLuminance) + 0.05)
            if contrast >= 3 { break }
            if colorScheme == .dark {
                candidate = RGB(
                    red: candidate.red + (1 - candidate.red) * 0.08,
                    green: candidate.green + (1 - candidate.green) * 0.08,
                    blue: candidate.blue + (1 - candidate.blue) * 0.08
                )
            } else {
                candidate = RGB(
                    red: candidate.red * 0.90,
                    green: candidate.green * 0.90,
                    blue: candidate.blue * 0.90
                )
            }
        }
        return candidate
    }

    private static func relativeLuminance(_ color: RGB) -> Double {
        func linear(_ channel: Double) -> Double {
            channel <= 0.04045
                ? channel / 12.92
                : pow((channel + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * linear(color.red) +
            0.7152 * linear(color.green) +
            0.0722 * linear(color.blue)
    }
}

struct MobileChromeButtonStyle: ButtonStyle {
    @Environment(\.accessibilityReduceMotion) private var systemReduceMotion
    @Environment(\.mobileBrowserReduceMotionOverride) private var reduceMotionOverride
    private var reduceMotion: Bool { reduceMotionOverride ?? systemReduceMotion }

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .opacity(configuration.isPressed ? 0.72 : 1)
            .scaleEffect(configuration.isPressed && !reduceMotion ? 0.96 : 1)
            .animation(
                MobileBrowserChromeTheme.chromePressAnimation(reduceMotion: reduceMotion),
                value: configuration.isPressed
            )
    }
}
