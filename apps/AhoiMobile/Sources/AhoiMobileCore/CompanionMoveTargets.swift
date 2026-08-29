import Foundation
import AhoiCloudKitSpike

public struct CompanionTreeMoveTarget: Identifiable, Equatable, Sendable {
    public let workspaceID: WorkspaceID
    public let parentID: TreeNodeID?
    public let label: String
    public let depth: Int

    public var id: String {
        workspaceID.rawValue.uuidString.lowercased() + ":" +
            (parentID?.rawValue.uuidString.lowercased() ?? "root")
    }
}

public enum CompanionMoveTargetBuilder {
    public static func targets(
        snapshot: CompanionSnapshot,
        excluding node: TreeNode? = nil
    ) -> [CompanionTreeMoveTarget] {
        let liveNodes = snapshot.visibleTreeNodes
        let excludedIDs = node.map { descendants(of: $0.id, in: liveNodes) } ?? []
        var result: [CompanionTreeMoveTarget] = []
        for workspace in snapshot.visibleWorkspaces {
            result.append(.init(
                workspaceID: workspace.id,
                parentID: nil,
                label: workspace.name,
                depth: 0
            ))
            let folders = liveNodes.filter {
                $0.workspaceID == workspace.id && $0.kind == .folder &&
                    !excludedIDs.contains($0.id)
            }
            let folderIDs = Set(folders.map(\.id))
            let children = Dictionary(grouping: folders) { folder in
                folder.parentID.flatMap { folderIDs.contains($0) ? $0 : nil }
            }
            var visited = Set<TreeNodeID>()
            func appendForest(_ roots: [TreeNode]) {
                var pending = roots.reversed().map { ($0, 1, "") }
                while let (folder, rawDepth, parentPath) = pending.popLast() {
                    guard visited.insert(folder.id).inserted else { continue }
                    let label = boundedPath(parentPath, appending: folder.title)
                    result.append(.init(
                        workspaceID: workspace.id,
                        parentID: folder.id,
                        label: workspace.name + " / " + label,
                        depth: min(rawDepth, CompanionHierarchyPolicy.maximumDepth)
                    ))
                    let descendants = (children[folder.id] ?? []).sorted(by: nodeOrder)
                    pending.append(contentsOf: descendants.reversed().map {
                        ($0, rawDepth + 1, label)
                    })
                }
            }
            appendForest((children[nil] ?? []).sorted(by: nodeOrder))
            for orphan in folders.sorted(by: nodeOrder) where !visited.contains(orphan.id) {
                appendForest([orphan])
            }
        }
        return result
    }

    private static func descendants(
        of id: TreeNodeID,
        in nodes: [TreeNode]
    ) -> Set<TreeNodeID> {
        let children = Dictionary(grouping: nodes, by: \.parentID)
        var pending = [id]
        var result = Set<TreeNodeID>()
        while let current = pending.popLast(), result.insert(current).inserted {
            pending.append(contentsOf: children[current, default: []].map(\.id))
        }
        return result
    }

    private static func boundedPath(_ parent: String, appending title: String) -> String {
        let maximumBytes = 2_048
        let candidate = parent.isEmpty ? title : parent + " / " + title
        guard candidate.utf8.count > maximumBytes else { return candidate }
        let suffix = "… / " + title
        guard suffix.utf8.count <= maximumBytes else {
            var result = ""
            for character in title {
                let next = result + String(character)
                guard next.utf8.count <= maximumBytes - 3 else { break }
                result = next
            }
            return "…" + result
        }
        return suffix
    }

    private static func nodeOrder(_ left: TreeNode, _ right: TreeNode) -> Bool {
        if left.syncSortKey != right.syncSortKey {
            return left.syncSortKey < right.syncSortKey
        }
        return left.id < right.id
    }
}
