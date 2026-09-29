import Foundation

// Helpers shared by the Library views (CompanionViews.swift and
// WorkspaceDetailView.swift): localized text and stable accessibility IDs.

func L(_ key: String, _ fallback: String) -> String {
    CompanionL10n.string(key, fallback: fallback)
}

func stableUUID(_ id: UUID) -> String {
    id.uuidString.lowercased()
}

func stableMoveTarget(_ target: CompanionTreeMoveTarget) -> String {
    let parent = target.parentID.map { stableUUID($0.rawValue) } ?? "root"
    return "\(stableUUID(target.workspaceID.rawValue)).\(parent)"
}
