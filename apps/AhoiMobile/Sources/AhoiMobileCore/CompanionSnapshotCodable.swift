import Foundation
import AhoiCloudKitSpike

extension CompanionSnapshot {
    private enum CodingKeys: String, CodingKey {
        case syncFormatVersion
        case structureRevision, splitGroups, archiveEntries
        case devices
        case workspaces
        case treeNodes
        case sessions
        case remoteTabs
        case history
        case productRecords
        case bookmarks
        case deviceCapabilities
        case mobileAppliedIntents
    }

    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        guard try values.decode(UInt32.self, forKey: .syncFormatVersion) == SharedSyncFormat.currentVersion,
              try values.decode(UInt32.self, forKey: .structureRevision) == 1 else {
            throw LocalCompanionStoreError.invalidSnapshot
        }
        let bookmarks = values.contains(.bookmarks)
            ? try values.decode([BookmarkRecord].self, forKey: .bookmarks) : []
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
            ) ?? .empty,
            bookmarks: bookmarks,
            deviceCapabilities: values.contains(.deviceCapabilities)
                ? try values.decode([DeviceCapabilityRecord].self, forKey: .deviceCapabilities) : [],
            mobileAppliedIntents: try values.decodeIfPresent(Set<UUID>.self, forKey: .mobileAppliedIntents) ?? []
        )
        splitGroups = try values.decode([SplitGroupRecord].self, forKey: .splitGroups)
        archiveEntries = try values.decode([TabArchiveEntryRecord].self, forKey: .archiveEntries)
        guard Set(splitGroups.map(\.id)).count == splitGroups.count,
              Set(archiveEntries.map(\.id)).count == archiveEntries.count else {
            throw LocalCompanionStoreError.invalidSnapshot
        }
        try CompanionBookmarkHierarchy.validate(bookmarks)
        guard Set(deviceCapabilities.map(\.id)).count == deviceCapabilities.count else {
            throw LocalCompanionStoreError.invalidSnapshot
        }
    }

    public func encode(to encoder: Encoder) throws {
        for value in splitGroups { try value.validate() }
        for value in archiveEntries { try value.validate() }
        var values = encoder.container(keyedBy: CodingKeys.self)
        try values.encode(SharedSyncFormat.currentVersion, forKey: .syncFormatVersion)
        try values.encode(1, forKey: .structureRevision)
        try values.encode(splitGroups, forKey: .splitGroups)
        try values.encode(archiveEntries, forKey: .archiveEntries)
        try values.encode(devices, forKey: .devices)
        try values.encode(workspaces, forKey: .workspaces)
        try values.encode(treeNodes, forKey: .treeNodes)
        try values.encode(sessions, forKey: .sessions)
        try values.encode(remoteTabs, forKey: .remoteTabs)
        try values.encode(history, forKey: .history)
        try values.encode(productRecords, forKey: .productRecords)
        try values.encode(bookmarks, forKey: .bookmarks)
        try values.encode(deviceCapabilities, forKey: .deviceCapabilities)
        try values.encode(mobileAppliedIntents.sorted { $0.uuidString < $1.uuidString }, forKey: .mobileAppliedIntents)
    }
}
