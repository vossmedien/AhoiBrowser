import Foundation
import AhoiCloudKitSpike

struct MobileFocusVoyageItem: Equatable, Identifiable, Sendable {
    enum Source: Equatable, Sendable {
        case openTab
        case history
        case savedPage
    }

    let id: String
    let title: String
    let subtitle: String
    let url: URL
    let source: Source
    let existingTabID: UUID?
    let workspaceID: WorkspaceID?

    var systemImage: String {
        switch source {
        case .openTab: "rectangle.on.rectangle"
        case .history: "clock.arrow.circlepath"
        case .savedPage: "bookmark.fill"
        }
    }
}

struct MobileFocusVoyageContent: Equatable, Sendable {
    let journeyItems: [MobileFocusVoyageItem]
    let savedItems: [MobileFocusVoyageItem]

    static let empty = Self(journeyItems: [], savedItems: [])

    static func make(
        mode: MobileBrowsingMode,
        tabs: [MobileTabRecord],
        snapshot: CompanionSnapshot,
        workspaceID: WorkspaceID?
    ) -> Self {
        // This early return is the privacy boundary: private Focus Voyage is
        // constructed without reading normal tabs, history, or saved nodes.
        guard mode == .normal else { return .empty }

        var seenJourneyURLs = Set<String>()
        let rankedTabs = tabs
            .filter { $0.mode == .normal }
            .sorted { lhs, rhs in
                let lhsMatches = workspaceID != nil && lhs.workspaceID == workspaceID
                let rhsMatches = workspaceID != nil && rhs.workspaceID == workspaceID
                if lhsMatches != rhsMatches { return lhsMatches }
                return lhs.lastActiveAt > rhs.lastActiveAt
            }

        var journeyItems = rankedTabs.compactMap { tab -> MobileFocusVoyageItem? in
            guard let url = validatedURL(tab.url),
                  seenJourneyURLs.insert(url.absoluteString).inserted else { return nil }
            return MobileFocusVoyageItem(
                id: "tab:\(tab.id.uuidString.lowercased())",
                title: displayTitle(tab.effectiveTitle, url: url),
                subtitle: displayOrigin(url),
                url: url,
                source: .openTab,
                existingTabID: tab.id,
                workspaceID: tab.workspaceID
            )
        }
        journeyItems = Array(journeyItems.prefix(4))

        if journeyItems.count < 4 {
            for visit in snapshot.visibleHistory {
                guard journeyItems.count < 4,
                      let url = validatedURL(visit.url),
                      seenJourneyURLs.insert(url.absoluteString).inserted else { continue }
                journeyItems.append(MobileFocusVoyageItem(
                    id: "history:\(visit.id.rawValue.uuidString.lowercased())",
                    title: displayTitle(visit.title, url: url),
                    subtitle: displayOrigin(url),
                    url: url,
                    source: .history,
                    existingTabID: nil,
                    workspaceID: nil
                ))
            }
        }

        guard let workspaceID else {
            return Self(journeyItems: journeyItems, savedItems: [])
        }
        var seenSavedURLs = Set<String>()
        let savedItems = snapshot.visibleTreeNodes.compactMap { node -> MobileFocusVoyageItem? in
            guard node.workspaceID == workspaceID,
                  node.kind == .savedPage,
                  let url = validatedURL(node.url),
                  seenSavedURLs.insert(url.absoluteString).inserted else { return nil }
            return MobileFocusVoyageItem(
                id: "saved:\(node.id.rawValue.uuidString.lowercased())",
                title: displayTitle(node.title, url: url),
                subtitle: displayOrigin(url),
                url: url,
                source: .savedPage,
                existingTabID: nil,
                workspaceID: workspaceID
            )
        }
        return Self(
            journeyItems: journeyItems,
            savedItems: Array(savedItems.prefix(4))
        )
    }

    private static func validatedURL(_ value: String?) -> URL? {
        guard let value, let url = URL(string: value),
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else { return nil }
        return url
    }

    private static func displayTitle(_ title: String, url: URL) -> String {
        let normalized = MobileTabRecord.normalizedTitle(title)
        return normalized.isEmpty ? displayOrigin(url) : normalized
    }

    private static func displayOrigin(_ url: URL) -> String {
        guard let host = url.host(), !host.isEmpty else { return url.absoluteString }
        guard let port = url.port else { return host }
        return "\(host):\(port)"
    }
}
