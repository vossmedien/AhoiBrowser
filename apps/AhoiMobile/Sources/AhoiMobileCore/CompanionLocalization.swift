import Foundation

public enum CompanionL10n {
    public static func string(_ key: String, fallback: String) -> String {
        bundle.localizedString(forKey: key, value: fallback, table: "Localizable")
    }

    public static func format(
        _ key: String,
        fallback: String,
        _ arguments: CVarArg...
    ) -> String {
        String(
            format: string(key, fallback: fallback),
            locale: Locale.current,
            arguments: arguments
        )
    }

    private static var bundle: Bundle {
#if SWIFT_PACKAGE
        .module
#else
        Bundle(for: CompanionLocalizationBundleMarker.self)
#endif
    }
}

private final class CompanionLocalizationBundleMarker: NSObject {}
