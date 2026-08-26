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
            func append(_ folder: TreeNode, depth: Int, path: String) {
                guard visited.insert(folder.id).inserted else { return }
                let label = path.isEmpty ? folder.title : path + " / " + folder.title
                result.append(.init(
                    workspaceID: workspace.id,
                    parentID: folder.id,
                    label: workspace.name + " / " + label,
                    depth: depth
                ))
                for child in (children[folder.id] ?? []).sorted(by: nodeOrder) {
                    append(child, depth: depth + 1, path: label)
                }
            }
            for root in (children[nil] ?? []).sorted(by: nodeOrder) {
                append(root, depth: 1, path: "")
            }
            for orphan in folders.sorted(by: nodeOrder) where !visited.contains(orphan.id) {
                append(orphan, depth: 1, path: "")
            }
        }
        return result
    }

    private static func descendants(
        of id: TreeNodeID,
        in nodes: [TreeNode]
    ) -> Set<TreeNodeID> {
        var pending = [id]
        var result = Set<TreeNodeID>()
        while let current = pending.popLast(), result.insert(current).inserted {
            pending.append(contentsOf: nodes.filter {
                $0.parentID == current
            }.map(\.id))
        }
        return result
    }

    private static func nodeOrder(_ left: TreeNode, _ right: TreeNode) -> Bool {
        if left.syncSortKey != right.syncSortKey {
            return left.syncSortKey < right.syncSortKey
        }
        return left.id < right.id
    }
}
