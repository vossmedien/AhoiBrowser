import Foundation

enum MobileExternalOpenPresentation {
    private static let maximumLabelBytes = 192

    static func originLabel(for rawOrigin: String) -> String {
        guard let url = URL(string: rawOrigin),
              let components = URLComponents(
                  url: url,
                  resolvingAgainstBaseURL: false
              ),
              let scheme = components.scheme?.lowercased(),
              ["http", "https"].contains(scheme),
              components.host != nil else {
            return CompanionL10n.string(
                "browser.external.unknown_origin",
                fallback: "This website"
            )
        }
        return bounded(MobileBrowserOriginFormatter.label(for: url))
    }

    /// Returns a useful handoff target without reflecting credentials, query
    /// values, fragments, full phone numbers, or mailbox local-parts into the
    /// native confirmation surface.
    static func targetLabel(for url: URL) -> String {
        guard let components = URLComponents(
            url: url,
            resolvingAgainstBaseURL: false
        ), let scheme = components.scheme?.lowercased() else {
            return CompanionL10n.string(
                "browser.external.unknown_target",
                fallback: "another app"
            )
        }
        let target: String
        switch scheme {
        case "mailto":
            target = maskedMailboxTarget(
                components.percentEncodedPath.removingPercentEncoding ?? "",
                scheme: scheme
            )
        case "facetime", "facetime-audio":
            let value = components.percentEncodedPath.removingPercentEncoding ?? ""
            target = value.contains("@")
                ? maskedMailboxTarget(value, scheme: scheme)
                : "\(scheme):••••"
        case "sms", "tel":
            target = "\(scheme):••••"
        default:
            target = "\(scheme):"
        }
        return bounded(target)
    }

    private static func maskedMailboxTarget(
        _ value: String,
        scheme: String
    ) -> String {
        let firstAddress = value.split(
            separator: ",",
            maxSplits: 1
        ).first.map(String.init) ?? ""
        guard let separator = firstAddress.lastIndex(of: "@") else {
            return "\(scheme):•••"
        }
        let rawDomain = firstAddress[firstAddress.index(after: separator)...]
        let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: ".-"))
        var domain = ""
        for scalar in rawDomain.unicodeScalars where allowed.contains(scalar) {
            domain.unicodeScalars.append(scalar)
        }
        return domain.isEmpty ? "\(scheme):•••" : "\(scheme):•••@\(domain)"
    }

    private static func bounded(_ value: String) -> String {
        guard value.utf8.count > maximumLabelBytes else { return value }
        let ellipsis = "…"
        var bytes = Data(value.utf8.prefix(maximumLabelBytes - ellipsis.utf8.count))
        while !bytes.isEmpty, String(data: bytes, encoding: .utf8) == nil {
            bytes.removeLast()
        }
        return (String(data: bytes, encoding: .utf8) ?? "") + ellipsis
    }
}
