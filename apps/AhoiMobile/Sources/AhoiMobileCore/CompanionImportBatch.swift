import Foundation
import AhoiCloudKitSpike

enum CompanionImportedValue: Sendable {
    case device(Device)
    case workspace(Workspace)
    case treeNode(TreeNode)
    case bookmark(BookmarkRecord)
    case deviceCapability(DeviceCapabilityRecord)
    case session(DeviceSession)
    case tab(RemoteTab)
    case history(HistoryVisit)
    case appearance(CompanionAppearanceRecord)
    case permittedSetting(CompanionPermittedSettingRecord)
    case extensionInventory(CompanionExtensionInventoryRecord)
    case developerAsset(CompanionDeveloperAssetRecord)

    var id: UUID {
        switch self {
        case .device(let value): return value.id.rawValue
        case .workspace(let value): return value.id.rawValue
        case .treeNode(let value): return value.id.rawValue
        case .bookmark(let value): return value.id.rawValue
        case .deviceCapability(let value): return value.id
        case .session(let value): return value.id.rawValue
        case .tab(let value): return value.id.rawValue
        case .history(let value): return value.id.rawValue
        case .appearance(let value): return value.id
        case .permittedSetting(let value): return value.id
        case .extensionInventory(let value): return value.id
        case .developerAsset(let value): return value.id
        }
    }
}

struct CompanionImportMutation: Sendable {
    let token: Int
    let value: CompanionImportedValue
}

enum CompanionImportDisposition: Sendable {
    case accepted(merged: CompanionImportedValue, shouldReenqueue: Bool)
    case rejected
}

struct CompanionImportMergeOutcome: Sendable {
    let token: Int
    let disposition: CompanionImportDisposition
}

extension LocalFirstRepository {
    /// Field-merges a complete fetched page on a working snapshot and persists
    /// at most once. Per-record merge conflicts are rejected without poisoning
    /// valid siblings; a store failure throws globally and leaves the actor's
    /// durable and in-memory snapshots unchanged.
    func mergeImportedBatch(
        _ mutations: [CompanionImportMutation]
    ) async throws -> [CompanionImportMergeOutcome] {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        guard !mutations.isEmpty else { return [] }
        let result = try mergeImportedBatch(mutations, into: snapshot)
        if result.snapshot != snapshot {
            try await commitImportedSnapshot(result.snapshot)
        }
        return result.outcomes
    }

    private func mergeImportedBatch(
        _ mutations: [CompanionImportMutation],
        into baseSnapshot: CompanionSnapshot
    ) throws -> (snapshot: CompanionSnapshot, outcomes: [CompanionImportMergeOutcome]) {
        var working = baseSnapshot

        var deviceIndexes: [DeviceID: Int] = [:]
        for index in working.devices.indices { deviceIndexes[working.devices[index].id] = index }
        var workspaceIndexes: [WorkspaceID: Int] = [:]
        for index in working.workspaces.indices {
            workspaceIndexes[working.workspaces[index].id] = index
        }
        var nodeIndexes: [TreeNodeID: Int] = [:]
        for index in working.treeNodes.indices { nodeIndexes[working.treeNodes[index].id] = index }
        var sessionIndexes: [DeviceSessionID: Int] = [:]
        for index in working.sessions.indices { sessionIndexes[working.sessions[index].id] = index }
        var tabIndexes: [TabID: Int] = [:]
        for index in working.remoteTabs.indices { tabIndexes[working.remoteTabs[index].id] = index }
        var historyIndexes: [HistoryVisitID: Int] = [:]
        for index in working.history.indices { historyIndexes[working.history[index].id] = index }
        var appearanceIndexes: [UUID: Int] = [:]
        for index in working.productRecords.appearance.indices {
            appearanceIndexes[working.productRecords.appearance[index].id] = index
        }
        var settingIndexes: [UUID: Int] = [:]
        for index in working.productRecords.permittedSettings.indices {
            settingIndexes[working.productRecords.permittedSettings[index].id] = index
        }
        var extensionIndexes: [UUID: Int] = [:]
        for index in working.productRecords.extensionInventory.indices {
            extensionIndexes[working.productRecords.extensionInventory[index].id] = index
        }
        var assetIndexes: [UUID: Int] = [:]
        for index in working.productRecords.developerAssets.indices {
            assetIndexes[working.productRecords.developerAssets[index].id] = index
        }

        var outcomes: [CompanionImportMergeOutcome] = []
        outcomes.reserveCapacity(mutations.count)
        for mutation in mutations {
            do {
                let accepted: CompanionImportedValue
                let shouldReenqueue: Bool
                switch mutation.value {
                case .device(let incoming):
                    let merged = if let index = deviceIndexes[incoming.id] {
                        try CompanionReadModelFieldMerge.merge(
                            working.devices[index], incoming
                        )
                    } else { incoming }
                    if let index = deviceIndexes[incoming.id] {
                        working.devices[index] = merged
                    } else {
                        deviceIndexes[incoming.id] = working.devices.count
                        working.devices.append(merged)
                    }
                    accepted = .device(merged)
                    shouldReenqueue = merged != incoming
                case .workspace(let incoming):
                    let merged = if let index = workspaceIndexes[incoming.id] {
                        try CompanionFieldMerge.merge(working.workspaces[index], incoming)
                    } else { incoming }
                    if let index = workspaceIndexes[incoming.id] {
                        working.workspaces[index] = merged
                    } else {
                        workspaceIndexes[incoming.id] = working.workspaces.count
                        working.workspaces.append(merged)
                    }
                    accepted = .workspace(merged)
                    shouldReenqueue = merged != incoming
                case .treeNode(let incoming):
                    let merged = if let index = nodeIndexes[incoming.id] {
                        try CompanionFieldMerge.merge(working.treeNodes[index], incoming)
                    } else { incoming }
                    if let index = nodeIndexes[incoming.id] {
                        working.treeNodes[index] = merged
                    } else {
                        nodeIndexes[incoming.id] = working.treeNodes.count
                        working.treeNodes.append(merged)
                    }
                    accepted = .treeNode(merged)
                    shouldReenqueue = merged != incoming
                case .bookmark(let incoming):
                    let existingIndex = working.bookmarks.firstIndex { $0.id == incoming.id }
                    let merged = try existingIndex.map {
                        try CompanionBookmarkFieldMerge.merge(working.bookmarks[$0], incoming)
                    } ?? incoming
                    var bookmarks = working.bookmarks
                    if let existingIndex { bookmarks[existingIndex] = merged }
                    else { bookmarks.append(merged) }
                    try CompanionBookmarkHierarchy.validate(bookmarks)
                    working.bookmarks = bookmarks
                    accepted = .bookmark(merged)
                    shouldReenqueue = merged != incoming
                case .deviceCapability(let incoming):
                    guard working.devices.contains(where: {
                        $0.id == incoming.deviceID && !$0.isDeleted && !$0.isRevoked
                    }) else { throw DeviceCapabilityError.unknownDevice }
                    try incoming.validate()
                    let index = working.deviceCapabilities.firstIndex { $0.id == incoming.id }
                    let merged = try index.map {
                        try CompanionCapabilityDomain.merge(working.deviceCapabilities[$0], incoming)
                    } ?? incoming
                    if let index { working.deviceCapabilities[index] = merged }
                    else { working.deviceCapabilities.append(merged) }
                    accepted = .deviceCapability(merged)
                    shouldReenqueue = merged != incoming
                case .session(let incoming):
                    let merged = if let index = sessionIndexes[incoming.id] {
                        try CompanionReadModelFieldMerge.merge(
                            working.sessions[index], incoming
                        )
                    } else { incoming }
                    if let index = sessionIndexes[incoming.id] {
                        working.sessions[index] = merged
                    } else {
                        sessionIndexes[incoming.id] = working.sessions.count
                        working.sessions.append(merged)
                    }
                    accepted = .session(merged)
                    shouldReenqueue = merged != incoming
                case .tab(let incoming):
                    guard incoming.context == .normal else {
                        throw CompanionModelError.incognitoNotSyncable
                    }
                    let merged = if let index = tabIndexes[incoming.id] {
                        try CompanionReadModelFieldMerge.merge(
                            working.remoteTabs[index], incoming
                        )
                    } else { incoming }
                    if let index = tabIndexes[incoming.id] {
                        working.remoteTabs[index] = merged
                    } else {
                        tabIndexes[incoming.id] = working.remoteTabs.count
                        working.remoteTabs.append(merged)
                    }
                    accepted = .tab(merged)
                    shouldReenqueue = merged != incoming
                case .history(let incoming):
                    let merged = if let index = historyIndexes[incoming.id] {
                        try CompanionReadModelFieldMerge.merge(
                            working.history[index], incoming
                        )
                    } else { incoming }
                    if let index = historyIndexes[incoming.id] {
                        working.history[index] = merged
                    } else {
                        historyIndexes[incoming.id] = working.history.count
                        working.history.append(merged)
                    }
                    accepted = .history(merged)
                    shouldReenqueue = merged != incoming
                case .appearance(let incoming):
                    let merged = try selectRecord(
                        appearanceIndexes[incoming.id].map {
                            working.productRecords.appearance[$0]
                        },
                        incoming
                    )
                    if let index = appearanceIndexes[incoming.id] {
                        working.productRecords.appearance[index] = merged
                    } else {
                        appearanceIndexes[incoming.id] = working.productRecords.appearance.count
                        working.productRecords.appearance.append(merged)
                    }
                    accepted = .appearance(merged)
                    shouldReenqueue = merged.version > incoming.version
                case .permittedSetting(let incoming):
                    let merged = try selectRecord(
                        settingIndexes[incoming.id].map {
                            working.productRecords.permittedSettings[$0]
                        },
                        incoming
                    )
                    if let index = settingIndexes[incoming.id] {
                        working.productRecords.permittedSettings[index] = merged
                    } else {
                        settingIndexes[incoming.id] =
                            working.productRecords.permittedSettings.count
                        working.productRecords.permittedSettings.append(merged)
                    }
                    accepted = .permittedSetting(merged)
                    shouldReenqueue = merged.version > incoming.version
                case .extensionInventory(let incoming):
                    let merged = try selectRecord(
                        extensionIndexes[incoming.id].map {
                            working.productRecords.extensionInventory[$0]
                        },
                        incoming
                    )
                    if let index = extensionIndexes[incoming.id] {
                        working.productRecords.extensionInventory[index] = merged
                    } else {
                        extensionIndexes[incoming.id] =
                            working.productRecords.extensionInventory.count
                        working.productRecords.extensionInventory.append(merged)
                    }
                    accepted = .extensionInventory(merged)
                    shouldReenqueue = merged.version > incoming.version
                case .developerAsset(let incoming):
                    let merged = try selectRecord(
                        assetIndexes[incoming.id].map {
                            working.productRecords.developerAssets[$0]
                        },
                        incoming
                    )
                    if let index = assetIndexes[incoming.id] {
                        working.productRecords.developerAssets[index] = merged
                    } else {
                        assetIndexes[incoming.id] =
                            working.productRecords.developerAssets.count
                        working.productRecords.developerAssets.append(merged)
                    }
                    accepted = .developerAsset(merged)
                    shouldReenqueue = merged.version > incoming.version
                }
                outcomes.append(.init(
                    token: mutation.token,
                    disposition: .accepted(
                        merged: accepted,
                        shouldReenqueue: shouldReenqueue
                    )
                ))
            } catch {
                outcomes.append(.init(token: mutation.token, disposition: .rejected))
            }
        }

        return (working, outcomes)
    }
}
