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
#if DEBUG
        if let fixtureProvider = MobileSearchProviderFixtureOverride.active {
            return fixtureProvider.template
        }
#endif
        switch self {
        case .duckDuckGo:
            return "https://duckduckgo.com/?q=%@"
        case .google:
            return "https://www.google.com/search?q=%@"
        case .bing:
            return "https://www.bing.com/search?q=%@"
        }
    }

    public var localizedName: String {
#if DEBUG
        if let fixtureProvider = MobileSearchProviderFixtureOverride.active {
            return fixtureProvider.name
        }
#endif
        switch self {
        case .duckDuckGo:
            return "DuckDuckGo"
        case .google:
            return "Google"
        case .bing:
            return "Bing"
        }
    }

    public static func resolved(from rawValue: String) -> Self {
        Self(rawValue: rawValue) ?? .duckDuckGo
    }
}

#if DEBUG
private struct MobileSearchProviderFixtureOverride {
    private static let templateArgument = "-AhoiUITestSearchProviderTemplate"
    private static let nameArgument = "-AhoiUITestSearchProviderName"

    let name: String
    let template: String

    static var active: Self? {
        let arguments = ProcessInfo.processInfo.arguments
        guard let template = singleValue(after: templateArgument, in: arguments),
              let name = singleValue(after: nameArgument, in: arguments),
              isValid(name: name),
              isValid(template: template) else {
            return nil
        }
        return Self(name: name, template: template)
    }

    private static func singleValue(after key: String, in arguments: [String]) -> String? {
        let indices = arguments.indices.filter { arguments[$0] == key }
        guard indices.count == 1,
              let index = indices.first,
              arguments.indices.contains(index + 1) else {
            return nil
        }
        return arguments[index + 1]
    }

    private static func isValid(name: String) -> Bool {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        return !trimmed.isEmpty &&
            trimmed == name &&
            name.utf8.count <= 48 &&
            name.unicodeScalars.allSatisfy {
                !CharacterSet.controlCharacters.contains($0)
            }
    }

    private static func isValid(template: String) -> Bool {
        guard template.utf8.count <= 2_048,
              template.components(separatedBy: "%@").count == 2 else {
            return false
        }
        let candidate = template.replacingOccurrences(of: "%@", with: "ahoi-fixture-query")
        guard let components = URLComponents(string: candidate),
              components.scheme?.lowercased() == "https",
              let host = components.host?.lowercased(),
              host == "localhost" || host.hasSuffix(".localhost"),
              components.user == nil,
              components.password == nil,
              components.fragment == nil else {
            return false
        }
        return components.url != nil
    }
}
#endif
