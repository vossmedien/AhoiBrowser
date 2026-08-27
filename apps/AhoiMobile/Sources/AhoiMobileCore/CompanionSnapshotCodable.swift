import Foundation
import AhoiCloudKitSpike

extension CompanionSnapshot {
    private enum CodingKeys: String, CodingKey {
        case devices
        case workspaces
        case treeNodes
        case sessions
        case remoteTabs
        case history
        case productRecords
    }

    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        self.init(
            devices: try values.decodeIfPresent([Device].self, forKey: .devices) ?? [],
            workspaces: try values.decodeIfPresent([Workspace].self, forKey: .workspaces) ?? [],
            treeNodes: try values.decodeIfPresent([TreeNode].self, forKey: .treeNodes) ?? [],
            sessions: try values.decodeIfPresent([DeviceSession].self, forKey: .sessions) ?? [],
            remoteTabs: try values.decodeIfPresent([RemoteTab].self, forKey: .remoteTabs) ?? [],
            history: try values.decodeIfPresent([HistoryVisit].self, forKey: .history) ?? [],
            productRecords: try values.decodeIfPresent(
                CompanionProductSnapshot.self,
                forKey: .productRecords
            ) ?? .empty
        )
    }

    public func encode(to encoder: Encoder) throws {
        var values = encoder.container(keyedBy: CodingKeys.self)
        try values.encode(devices, forKey: .devices)
        try values.encode(workspaces, forKey: .workspaces)
        try values.encode(treeNodes, forKey: .treeNodes)
        try values.encode(sessions, forKey: .sessions)
        try values.encode(remoteTabs, forKey: .remoteTabs)
        try values.encode(history, forKey: .history)
        try values.encode(productRecords, forKey: .productRecords)
    }
}
