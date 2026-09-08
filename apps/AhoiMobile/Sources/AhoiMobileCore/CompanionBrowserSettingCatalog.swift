import CoreFoundation
import CryptoKit
import Foundation

/// Pure metadata recognition matching Common's browser_setting_catalog.cc.
/// Membership does not grant consent, apply an iOS preference, or authorize upload.
public enum CompanionBrowserSettingCatalog {
    private enum Rule: Sendable {
        case boolean
        case autoHideDelay
        case charset
        case fontScale
        case readingColor
        case readingSpacing
        case networkPrediction
    }

    // Keep this positive set and its bounds equal to the 24 C++ descriptors.
    // No generic strings, paths, URLs, dictionaries, account or permission data.
    private static let rules: [String: Rule] = [
        "ahoi.appearance.glass_enabled": .boolean,
        "ahoi.appearance.sidebar_page_tint_enabled": .boolean,
        "ahoi.navigation.floating_auto_hide_enabled": .boolean,
        "ahoi.navigation.floating_reveal_notch_enabled": .boolean,
        "ahoi.navigation.floating_auto_hide_delay_ms": .autoHideDelay,
        "homepage_is_newtabpage": .boolean,
        "browser.show_home_button": .boolean,
        "browser.show_forward_button": .boolean,
        "browser.pin_split_tab_button": .boolean,
        "browser.split_view_drag_and_drop_enabled": .boolean,
        "download.prompt_for_download": .boolean,
        "plugins.always_open_pdf_externally": .boolean,
        "browser.enable_spellchecking": .boolean,
        "translate.enabled": .boolean,
        "intl.charset_default": .charset,
        "settings.a11y.read_anything.font_scale": .fontScale,
        "settings.a11y.read_anything.color_info": .readingColor,
        "settings.a11y.read_anything.line_spacing": .readingSpacing,
        "settings.a11y.read_anything.letter_spacing": .readingSpacing,
        "settings.a11y.read_anything.links_enabled": .boolean,
        "settings.a11y.read_anything.images_enabled": .boolean,
        "enable_do_not_track": .boolean,
        "search.suggest_enabled": .boolean,
        "net.network_prediction_options": .networkPrediction,
    ]

    /// Byte-for-byte identity from browser_settings_sync_types.cc, not UUIDv5.
    public static func recordID(for settingID: String) -> UUID {
        var bytes = Array(SHA256.hash(data: Data(("setting:" + settingID).utf8)).prefix(16))
        bytes[6] = (bytes[6] & 0x0f) | 0x40
        // Common overwrites the entire hex[16] nibble with '8'. Using the usual
        // RFC variant mask 0x3f here would produce different record identities.
        bytes[8] = (bytes[8] & 0x0f) | 0x80
        return UUID(uuid: (
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
            bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
            bytes[12], bytes[13], bytes[14], bytes[15]
        ))
    }

    public static func isPortable(_ record: CompanionPermittedSettingRecord) -> Bool {
        !record.isDeleted && record.id == recordID(for: record.settingID) &&
            validatesValue(settingID: record.settingID, valueJSON: record.valueJSON)
    }

    /// Value-only recognition deliberately does not enforce record identity:
    /// shape-only wire fixtures may use their existing arbitrary record UUIDs.
    public static func validatesValue(settingID: String, valueJSON: String) -> Bool {
        guard let rule = rules[settingID],
              let value = try? JSONSerialization.jsonObject(
                with: Data(valueJSON.utf8), options: [.fragmentsAllowed]
              ) else { return false }
        if value is NSNull { return true } // Explicit reset for a known ID only.
        if case .boolean = rule {
            guard let number = value as? NSNumber else { return false }
            return CFGetTypeID(number) == CFBooleanGetTypeID()
        }
        if case .charset = rule {
            guard let charset = value as? String else { return false }
            return charset == "UTF-8" || charset == "windows-1252"
        }
        guard let number = value as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID() else { return false }
        let numeric = number.doubleValue
        guard numeric.isFinite else { return false }
        let integer = Int(exactly: numeric)
        switch rule {
        case .autoHideDelay:
            return integer.map { (100...10_000).contains($0) } ?? false
        case .fontScale:
            return (0.5...4.5).contains(numeric)
        case .networkPrediction:
            // 1 remains the current native registered default, mapping to Standard.
            return integer.map { [0, 1, 2, 3].contains($0) } ?? false
        case .readingColor:
            return integer.map { [0, 1, 2, 3, 4, 5, 7, 8].contains($0) } ?? false
        case .readingSpacing:
            return integer.map { [1, 2, 3].contains($0) } ?? false
        case .boolean, .charset:
            return false
        }
    }
}
