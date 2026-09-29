import AhoiCloudKitSpike
import UIKit

@MainActor
enum MobileWorkspaceIconPolicy {
    static let fallbackSystemName = "square.stack.3d.up"

    private static let legacyMappings: [String: String] = [
        "⚓": "anchor",
        "⛵": "sailboat.fill",
        "🚀": "rocket.fill",
        "💼": "briefcase.fill",
        "📚": "books.vertical.fill",
        "🔖": "bookmark.fill",
        "🧪": "flask.fill",
        "✏️": "pencil",
        "✏": "pencil",
    ]

    /// The symbol of `workspace`, or the fallback when there is none (for
    /// example while browsing privately).
    static func systemName(for workspace: Workspace?) -> String {
        guard let workspace else { return fallbackSystemName }
        return systemName(for: workspace.icon)
    }

    static func systemName(for rawValue: String) -> String {
        let candidate = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !candidate.isEmpty else { return fallbackSystemName }
        if let mapped = legacyMappings[candidate] { return mapped }

        // Synced workspace metadata is untrusted presentation input. Only a
        // name that UIKit resolves as an SF Symbol may reach `Image`.
        guard candidate.unicodeScalars.allSatisfy({ scalar in
            scalar.isASCII && (
                CharacterSet.alphanumerics.contains(scalar) ||
                    scalar == "." || scalar == "-"
            )
        }), UIImage(systemName: candidate) != nil else {
            return fallbackSystemName
        }
        return candidate
    }
}
