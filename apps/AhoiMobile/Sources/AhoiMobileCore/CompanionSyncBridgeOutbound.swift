import Foundation
import AhoiCloudKitSpike

enum CompanionRemoteCommandRetention {
    static let maximumHydrationScanCount = 1_000
    static let maximumReadModelCount = 100
    static let terminalHistoryCount = 20

    static func newestTransportRecords(
        _ records: [SyncRecord],
        limit: Int = maximumHydrationScanCount
    ) -> [SyncRecord] {
        guard limit > 0 else { return [] }
        return Array(records.lazy.filter { $0.dataClass == .remoteCommand }
            .sorted(by: transportNewestFirst)
            .prefix(limit))
    }

    struct HydrationOutcome {
        let ownedStates: [RemoteCommandState]
        let rejectedRecords: [SyncRecord]
    }

    struct HydrationBatches {
        let exact: [SyncRecord]
        let migration: [SyncRecord]
    }

    static func hydrationBatches(
        exactRecords: [SyncRecord],
        migrationRecords: [SyncRecord],
        migrationCompleted: Bool
    ) -> HydrationBatches {
        let exact = newestTransportRecords(exactRecords)
        guard !migrationCompleted else {
            return .init(exact: exact, migration: [])
        }
        let exactIDs = Set(exact.map(\.recordID))
        return .init(
            exact: exact,
            migration: newestTransportRecords(migrationRecords.filter {
                !exactIDs.contains($0.recordID)
            })
        )
    }

    static func decodeOwnedStates(
        from records: [SyncRecord],
        sourceDeviceID: DeviceID,
        requireOwnedSource: Bool = false,
        decode: (SyncRecord) throws -> RemoteCommandState,
        validateOwned: (RemoteCommandState) throws -> Void
    ) -> HydrationOutcome {
        var restored: [UUID: RemoteCommandState] = [:]
        var rejected: [SyncRecord] = []
        for record in newestTransportRecords(records) {
            do {
                let state = try decode(record)
                guard state.envelope.payload.sourceDeviceID == sourceDeviceID else {
                    if requireOwnedSource { rejected.append(record) }
                    continue
                }
                try validateOwned(state)
                if let existing = restored[state.id],
                   existing.envelope != state.envelope {
                    rejected.append(record)
                    continue
                }
                if restored[state.id].map({ $0.version >= state.version }) != true {
                    restored[state.id] = state
                }
            } catch {
                rejected.append(record)
            }
        }
        return HydrationOutcome(
            ownedStates: Array(restored.values),
            rejectedRecords: rejected
        )
    }

    static func boundedStates(
        _ states: some Sequence<RemoteCommandState>,
        nowMilliseconds: UInt64,
        limit: Int = maximumReadModelCount,
        terminalLimit: Int = terminalHistoryCount
    ) -> [RemoteCommandState] {
        guard limit > 0 else { return [] }
        let ordered = states.sorted(by: CompanionRemoteCommandOrdering.newestFirst)
        let active = Array(ordered.lazy.filter {
            !$0.status.isTerminal &&
                $0.envelope.payload.expiresAtMilliseconds > nowMilliseconds
        }.prefix(limit))
        let remaining = max(0, limit - active.count)
        let history = ordered.lazy.filter {
            $0.status.isTerminal ||
                $0.envelope.payload.expiresAtMilliseconds <= nowMilliseconds
        }.prefix(min(max(0, terminalLimit), remaining))
        return (active + Array(history))
            .sorted(by: CompanionRemoteCommandOrdering.newestFirst)
    }

    private static func transportNewestFirst(
        _ lhs: SyncRecord,
        _ rhs: SyncRecord
    ) -> Bool {
        if lhs.modifiedAt != rhs.modifiedAt {
            return lhs.modifiedAt > rhs.modifiedAt
        }
        if lhs.originatingDevice != rhs.originatingDevice {
            return lhs.originatingDevice > rhs.originatingDevice
        }
        if lhs.schemaVersion != rhs.schemaVersion {
            return lhs.schemaVersion > rhs.schemaVersion
        }
        return lhs.recordID.uuidString > rhs.recordID.uuidString
    }
}

private extension RemoteCommandStatus {
    var isTerminal: Bool {
        self == .executed || self == .failed
    }
}

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
        let record = try codec.makeRecord(
            recordID: commandID,
            entityID: commandID,
            dataClass: .remoteCommand,
            version: state.version,
            plaintext: try wireCodec.encode(state)
        )
        let previousOwnership = try await commandOwnershipStore.load()
        let nextOwnership = previousOwnership.inserting(.init(
            recordID: commandID,
            issuedAtMilliseconds: issuedAtMilliseconds
        ))
        try await commandOwnershipStore.save(nextOwnership)
        do {
            try await provider.enqueue(record)
        } catch {
            let enqueueError = error
            let shouldRollback: Bool
            do {
                shouldRollback = try await provider.locallyPersistedRecord(
                    forRecordID: commandID
                ) != record
            } catch {
                // An unreadable transport store leaves the privacy-safe ID in
                // place. Removing it could make a durably written command
                // permanently undiscoverable after a later store recovery.
                throw enqueueError
            }
            if shouldRollback {
                do {
                    try await commandOwnershipStore.save(previousOwnership)
                } catch {
                    throw RemoteCommandOwnershipStoreError.rollbackFailed
                }
            }
            throw enqueueError
        }
        commandStates[commandID] = state
        pruneRemoteCommandReadModel(nowMilliseconds: issuedAtMilliseconds)
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
            .sorted(by: CompanionRemoteCommandOrdering.newestFirst)
    }

    public func remoteCommandStates(
        limit: Int = 100
    ) -> [RemoteCommandState] {
        guard limit > 0 else { return [] }
        return Array(commandStates.values
            .sorted(by: CompanionRemoteCommandOrdering.newestFirst)
            .prefix(limit))
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
        // A failed restart restore may never leave a previous in-memory view
        // visible. Only the fully revalidated durable set is republished.
        commandStates.removeAll(keepingCapacity: false)

        let ownership = try await commandOwnershipStore.load()
        let localIdentity = try commandSigner.provisioningIdentity()
        let exactRecords = try await provider.records(
            forRecordIDs: ownership.entries.map(\.recordID)
        )
        let migrationRecords: [SyncRecord]
        if ownership.migrationCompleted {
            migrationRecords = []
        } else {
            migrationRecords = try await provider.allRecords()
        }
        let batches = CompanionRemoteCommandRetention.hydrationBatches(
            exactRecords: exactRecords,
            migrationRecords: migrationRecords,
            migrationCompleted: ownership.migrationCompleted
        )
        let exactOutcome = CompanionRemoteCommandRetention.decodeOwnedStates(
            from: batches.exact,
            sourceDeviceID: commandSigner.sourceDeviceID,
            requireOwnedSource: true,
            decode: decodeRemoteCommandRecord,
            validateOwned: {
                try validateLocallyOwnedRemoteCommand($0, identity: localIdentity)
            }
        )
        let migrationOutcome = CompanionRemoteCommandRetention.decodeOwnedStates(
            from: batches.migration,
            sourceDeviceID: commandSigner.sourceDeviceID,
            decode: decodeRemoteCommandRecord,
            validateOwned: {
                try validateLocallyOwnedRemoteCommand($0, identity: localIdentity)
            }
        )
        for record in exactOutcome.rejectedRecords + migrationOutcome.rejectedRecords {
            try await provider.quarantineImportedRecord(
                record,
                reason: "persisted_remote_command_validation_failed"
            )
        }
        var restored: [UUID: RemoteCommandState] = [:]
        for state in exactOutcome.ownedStates + migrationOutcome.ownedStates {
            if let existing = restored[state.id], existing.envelope != state.envelope {
                throw CompanionSyncBridgeError.envelopeMismatch
            }
            if restored[state.id].map({ $0.version >= state.version }) != true {
                restored[state.id] = state
            }
        }
        let nextOwnership = ownership.replacingEntries(
            restored.values.map {
                .init(
                    recordID: $0.id,
                    issuedAtMilliseconds: $0.envelope.payload.issuedAtMilliseconds
                )
            },
            migrationCompleted: true
        )
        try await commandOwnershipStore.save(nextOwnership)
        commandStates = restored
        pruneRemoteCommandReadModel()
    }

    func pruneRemoteCommandReadModel(
        nowMilliseconds: UInt64 = UInt64(Date().timeIntervalSince1970 * 1_000)
    ) {
        let retained = CompanionRemoteCommandRetention.boundedStates(
            commandStates.values,
            nowMilliseconds: nowMilliseconds
        )
        commandStates = Dictionary(uniqueKeysWithValues: retained.map { ($0.id, $0) })
    }
}
