import Foundation
import AhoiCloudKitSpike

extension CompanionSyncBridge {
    public func enqueue(_ device: Device) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: device.id.rawValue,
            entityID: device.id.rawValue,
            dataClass: .device,
            version: device.version,
            plaintext: wireCodec.encode(device),
            tombstone: device.tombstone
        ))
    }

    public func enqueue(_ workspace: Workspace) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: workspace.id.rawValue,
            entityID: workspace.id.rawValue,
            dataClass: .workspace,
            version: workspace.version,
            plaintext: wireCodec.encode(workspace),
            tombstone: workspace.tombstone
        ))
    }

    public func enqueue(_ node: TreeNode) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: node.id.rawValue,
            entityID: node.id.rawValue,
            dataClass: .treeNode,
            version: node.version,
            plaintext: wireCodec.encode(node),
            orderKey: node.orderKey,
            tombstone: node.tombstone
        ))
    }

    public func enqueue(_ session: DeviceSession) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: session.id.rawValue,
            entityID: session.id.rawValue,
            dataClass: .deviceSession,
            version: session.version,
            plaintext: wireCodec.encode(session),
            tombstone: session.tombstone
        ))
    }

    public func enqueue(_ visit: HistoryVisit) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: visit.id.rawValue,
            entityID: visit.id.rawValue,
            dataClass: .historyVisit,
            version: visit.version,
            plaintext: wireCodec.encode(visit),
            tombstone: visit.tombstone
        ))
    }

    public func enqueue(_ tab: RemoteTab) async throws {
        guard tab.context == .normal else {
            throw CompanionModelError.incognitoNotSyncable
        }
        try await provider.enqueue(codec.makeRecord(
            recordID: tab.id.rawValue,
            entityID: tab.id.rawValue,
            dataClass: .deviceTab,
            version: tab.version,
            plaintext: wireCodec.encode(tab),
            tombstone: tab.tombstone
        ))
    }

    public func enqueue(_ value: CompanionAppearanceRecord) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: value.id,
            entityID: value.id,
            dataClass: .appearance,
            version: value.version,
            plaintext: wireCodec.encode(value),
            tombstone: value.tombstone
        ))
    }

    public func enqueue(_ value: CompanionPermittedSettingRecord) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: value.id,
            entityID: value.id,
            dataClass: .permittedSetting,
            version: value.version,
            plaintext: wireCodec.encode(value),
            tombstone: value.tombstone
        ))
    }

    public func enqueue(_ value: CompanionExtensionInventoryRecord) async throws {
        try await provider.enqueue(codec.makeRecord(
            recordID: value.id,
            entityID: value.id,
            dataClass: .extensionInventory,
            version: value.version,
            plaintext: wireCodec.encode(value),
            tombstone: value.tombstone
        ))
    }

    public func enqueue(_ value: CompanionDeveloperAssetRecord) async throws {
        guard value.isDeleted || value.optedIn else {
            throw CompanionProductRecordError.developerAssetNotOptedIn
        }
        try await provider.enqueue(
            codec.makeRecord(
                recordID: value.id,
                entityID: value.id,
                dataClass: .developerAsset,
                version: value.version,
                plaintext: wireCodec.encode(value),
                tombstone: value.tombstone
            ),
            authorization: .init(optedInDeveloperAssetIDs: [value.id])
        )
    }

    @discardableResult
    public func enqueueRemoteCommand(
        targetDeviceID: DeviceID,
        command: RemoteCommand,
        commandID: UUID = UUID(),
        issuedAtMilliseconds: UInt64 = UInt64(
            Date().timeIntervalSince1970 * 1_000
        )
    ) async throws -> RemoteCommandState {
        guard let commandSigner else {
            throw CompanionSyncBridgeError.remoteCommandSigningUnavailable
        }
        try RemoteCommandSemantics.validate(command)
        let payload = RemoteCommandPayload(
            commandID: commandID,
            sourceDeviceID: commandSigner.sourceDeviceID,
            targetDeviceID: targetDeviceID,
            nonce: try commandSigner.makeNonce(),
            issuedAtMilliseconds: issuedAtMilliseconds,
            command: command
        )
        let state = RemoteCommandState(
            envelope: try commandSigner.sign(payload),
            version: SyncVersion(
                modifiedAt: HybridLogicalClock(
                    physicalMilliseconds: issuedAtMilliseconds,
                    nodeID: commandSigner.sourceDeviceID
                ),
                modifiedBy: commandSigner.sourceDeviceID
            )
        )
        try await provider.enqueue(codec.makeRecord(
            recordID: commandID,
            entityID: commandID,
            dataClass: .remoteCommand,
            version: state.version,
            plaintext: try wireCodec.encode(state)
        ))
        commandStates[commandID] = state
        return state
    }

    public func remoteControlIdentity() throws -> RemoteControlProvisioningIdentity {
        guard let commandSigner else {
            throw CompanionSyncBridgeError.remoteCommandSigningUnavailable
        }
        return try commandSigner.provisioningIdentity()
    }

    public func remoteCommandState(_ commandID: UUID) -> RemoteCommandState? {
        commandStates[commandID]
    }

    public func remoteCommandStates(_ commandIDs: Set<UUID>) -> [RemoteCommandState] {
        commandIDs.compactMap { commandStates[$0] }
            .sorted {
                $0.envelope.payload.issuedAtMilliseconds >
                    $1.envelope.payload.issuedAtMilliseconds
            }
    }

    /// Rehydrates the in-memory command read model from the encrypted,
    /// file-backed transport authority. The fetched inbox may be acknowledged
    /// after a successful import because the selected primary envelope remains
    /// durable across process restarts.
    public func restorePersistedRemoteCommandStates() async throws {
        guard let commandSigner else {
            commandStates.removeAll(keepingCapacity: false)
            return
        }

        var restored: [UUID: RemoteCommandState] = [:]
        for record in try await provider.allRecords()
            where record.dataClass == .remoteCommand {
            let plaintext = try codec.openData(record)
            let state = try wireCodec.decodeRemoteCommand(
                record,
                plaintext: plaintext
            )
            try validate(record, identity: state.id, version: state.version)
            guard state.envelope.payload.sourceDeviceID == commandSigner.sourceDeviceID else {
                continue
            }
            if let existing = restored[state.id], existing.envelope != state.envelope {
                throw CompanionSyncBridgeError.envelopeMismatch
            }
            if restored[state.id].map({ $0.version >= state.version }) != true {
                restored[state.id] = state
            }
        }
        commandStates = restored
    }
}
