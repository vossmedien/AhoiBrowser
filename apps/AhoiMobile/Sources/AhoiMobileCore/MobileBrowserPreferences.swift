import Foundation

public enum MobileBrowserPreferences {
    public static let searchEngineKey = "AhoiMobile.Browser.SearchEngine"
}

public enum MobileSearchEngine: String, CaseIterable, Identifiable, Sendable {
    case duckDuckGo
    case google
    case bing

    public var id: String { rawValue }

    public var searchTemplate: String {
        switch self {
        case .duckDuckGo:
            "https://duckduckgo.com/?q=%@"
        case .google:
            "https://www.google.com/search?q=%@"
        case .bing:
            "https://www.bing.com/search?q=%@"
        }
    }

    public var localizedName: String {
        switch self {
        case .duckDuckGo:
            "DuckDuckGo"
        case .google:
            "Google"
        case .bing:
            "Bing"
        }
    }

    public static func resolved(from rawValue: String) -> Self {
        Self(rawValue: rawValue) ?? .duckDuckGo
    }
}
