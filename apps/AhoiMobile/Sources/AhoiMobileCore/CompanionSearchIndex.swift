import Foundation
import AhoiCloudKitSpike

public enum CompanionSearchResultKind: String, Codable, Sendable {
    case workspace
    case folder
    case savedPage
    case remoteTab
    case history
}

public struct CompanionSearchResult: Codable, Equatable, Hashable, Sendable, Identifiable {
    public let id: UUID
    public let kind: CompanionSearchResultKind
    public let title: String
    public let detail: String
    public let url: String?
    public let deviceName: String?
    public let workspaceName: String?

    public init(
        id: UUID,
        kind: CompanionSearchResultKind,
        title: String,
        detail: String,
        url: String? = nil,
        deviceName: String? = nil,
        workspaceName: String? = nil
    ) {
        self.id = id
        self.kind = kind
        self.title = title
        self.detail = detail
        self.url = url
        self.deviceName = deviceName
        self.workspaceName = workspaceName
    }
}

/// Search stays local because the URL/title payloads are stored in
/// `CKRecord.encryptedValues` and therefore cannot be server-side queried.
public actor LocalSearchIndex {
    private var records: [CompanionSearchResult] = []

    public init() {}

    public func rebuild(snapshot: CompanionSnapshot) {
        var result: [CompanionSearchResult] = []
        result.reserveCapacity(
            snapshot.workspaces.count
                + snapshot.treeNodes.count
                + snapshot.remoteTabs.count
                + snapshot.history.count
        )
        var workspaceNames: [WorkspaceID: String] = [:]
        for workspace in snapshot.workspaces where !workspace.isDeleted {
            workspaceNames[workspace.id] = workspace.name
        }

        for workspace in snapshot.visibleWorkspaces {
            result.append(.init(
                id: workspace.id.rawValue,
                kind: .workspace,
                title: workspace.name,
                detail: CompanionL10n.string(
                    "search.kind.workspace",
                    fallback: "Workspace"
                )
            ))
        }
        for node in snapshot.visibleTreeNodes {
            result.append(.init(
                id: node.id.rawValue,
                kind: node.kind == .folder ? .folder : .savedPage,
                title: node.title,
                detail: node.kind == .folder
                    ? CompanionL10n.string("search.kind.folder", fallback: "Folder")
                    : CompanionL10n.string(
                        "search.kind.saved_page",
                        fallback: "Saved page"
                    ),
                url: node.url,
                workspaceName: workspaceNames[node.workspaceID]
            ))
        }
        for visit in snapshot.visibleHistory {
            result.append(.init(
                id: visit.id.rawValue,
                kind: .history,
                title: visit.title,
                detail: visit.url,
                url: visit.url
            ))
        }
        for tab in snapshot.visibleRemoteTabs {
            result.append(.init(
                id: tab.id.rawValue,
                kind: .remoteTab,
                title: tab.title,
                detail: tab.url,
                url: tab.url,
                deviceName: tab.deviceName,
                workspaceName: tab.workspaceName
            ))
        }
        records = result
    }

    public func search(_ query: String, limit: Int = 50) -> [CompanionSearchResult] {
        let normalizedQuery = query.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        guard !normalizedQuery.isEmpty else {
            return Array(records.prefix(limit))
        }

        return records.filter { record in
            [record.title, record.detail, record.url, record.deviceName, record.workspaceName]
                .compactMap { $0 }
                .contains {
                    $0.folding(
                        options: [.caseInsensitive, .diacriticInsensitive],
                        locale: .current
                    ).contains(normalizedQuery)
                }
        }.prefix(limit).map { $0 }
    }
}
