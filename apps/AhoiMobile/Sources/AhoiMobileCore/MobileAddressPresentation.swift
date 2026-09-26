import Foundation

/// What the browser chrome shows for the selected page's address: the label,
/// its accessibility value, the origin and the security symbol. Private
/// browsing names only the origin (split from AhoiMobileBrowserView.swift,
/// source line budget).
struct MobileAddressPresentation: Equatable {
    let isPrivate: Bool
    let url: URL?

    /// The address to present: after a failed load the tab's committed URL,
    /// otherwise the live page URL, falling back to the tab's URL.
    static func addressURL(pageFailed: Bool, tabURL: String?, pageURL: URL?) -> URL? {
        if pageFailed, let tabURL {
            return URL(string: tabURL)
        }
        return pageURL ?? tabURL.flatMap(URL.init(string:))
    }

    var originHost: String? {
        guard let url, let host = url.host(), !host.isEmpty else { return nil }
        guard let port = url.port else { return host }
        return "\(host):\(port)"
    }

    var label: String {
        if isPrivate {
            if let host = originHost {
                return CompanionL10n.format(
                    "browser.private.address",
                    fallback: "Private · %@",
                    host
                )
            }
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        if let url {
            return originHost ?? url.absoluteString
        }
        return CompanionL10n.string("browser.search_or_address", fallback: "Search or address")
    }

    var accessibilityValue: String {
        if isPrivate {
            if let host = originHost {
                return CompanionL10n.format(
                    "browser.private.address.value",
                    fallback: "Private browsing, %@",
                    host
                )
            }
            return CompanionL10n.string("browser.private", fallback: "Private")
        }
        return url?.absoluteString
            ?? CompanionL10n.string("browser.search_or_address", fallback: "Search or address")
    }

    var securitySymbol: String {
        url?.scheme?.lowercased() == "https" ? "lock.fill" : "globe"
    }
}
