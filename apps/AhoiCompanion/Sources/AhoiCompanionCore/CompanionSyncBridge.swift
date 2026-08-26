import Foundation
import AhoiCloudKitSpike

public enum CompanionSyncBridgeError: Error, Equatable, Sendable {
    case envelopeMismatch
    case remoteCommandSigningUnavailable
    case unsupportedDataClass(SyncDataClass)
}

/// Binds the local-first companion repository to the encrypted CKSyncEngine
/// record store. Local writes commit first; a missing key/account/container can
/// delay transport but can never roll back or hide the local mutation.
public actor CompanionSyncBridge {
    private let repository: LocalFirstRepository
    private let provider: CloudKitSyncProvider
    private let codec: CompanionPayloadCodec
    private let wireCodec = DesktopWirePayloadCodec()
    private let commandSigner: (any RemoteCommandSigning)?
    private var commandStates: [UUID: RemoteCommandState] = [:]

    public nonisolated let remoteControlConfigured: Bool

    public init(
        repository: LocalFirstRepository,
        provider: CloudKitSyncProvider,
        sealer: any CompanionPayloadSealer,
        commandSigner: (any RemoteCommandSigning)? = nil
    ) {
        self.repository = repository
        self.provider = provider
        self.codec = CompanionPayloadCodec(sealer: sealer)
        self.commandSigner = commandSigner
        self.remoteControlConfigured = commandSigner != nil
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
        try await provider.enqueue(codec.makeRecord(
            recordID: value.id,
            entityID: value.id,
            dataClass: .developerAsset,
            version: value.version,
            plaintext: wireCodec.encode(value),
            tombstone: value.tombstone
        ))
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

    /// Seeds transport from the durable local authority when sync is enabled
    /// after offline-only edits. Reusing stable record IDs makes this safe to
    /// repeat after a restart or interrupted first upload.
    public func enqueueLocalSnapshot() async throws {
        let snapshot = try await repository.currentSnapshot()
        for workspace in snapshot.workspaces { try await enqueue(workspace) }
        for node in snapshot.treeNodes { try await enqueue(node) }
        for visit in snapshot.history { try await enqueue(visit) }
        for value in snapshot.productRecords.appearance { try await enqueue(value) }
        for value in snapshot.productRecords.permittedSettings { try await enqueue(value) }
        for value in snapshot.productRecords.extensionInventory { try await enqueue(value) }
        for value in snapshot.productRecords.developerAssets
            where value.isDeleted || value.optedIn {
            try await enqueue(value)
        }
    }

    public func syncNow() async throws {
        try await provider.syncNow()
        try await importFetchedRecords()
    }

    public func importFetchedRecords() async throws {
        let records = try await provider.allRecords()
        for record in records where record.dataClass != .deviceTab
            && record.dataClass != .deviceSession {
            await importOrQuarantine(record)
        }
        for record in records where record.dataClass == .deviceSession {
            await importOrQuarantine(record)
        }
        for record in records where record.dataClass == .deviceTab {
            await importOrQuarantine(record)
        }
    }

    public nonisolated func status() -> CloudKitSyncStatus {
        provider.status()
    }

    private func importOrQuarantine(_ record: SyncRecord) async {
        do {
            try await importRecord(record)
        } catch {
            // Error descriptions may include payload detail. Persist only a
            // bounded classification and keep importing the rest of the page.
            await provider.quarantineImportedRecord(
                record.recordID,
                reason: "plaintext_or_merge_validation_failed"
            )
        }
    }

    private func importRecord(_ record: SyncRecord) async throws {
        let plaintext = try codec.openData(record)
        let snapshot = try await repository.currentSnapshot()
        let devices = Dictionary(uniqueKeysWithValues: snapshot.devices.map { ($0.id, $0) })
        let workspaces = Dictionary(uniqueKeysWithValues: snapshot.workspaces.map { ($0.id, $0) })
        switch record.dataClass {
        case .device:
            let value = try wireCodec.decodeDevice(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version)
            try await repository.upsert(value)
        case .workspace:
            let value = try wireCodec.decodeWorkspace(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .treeNode:
            let value = try wireCodec.decodeTreeNode(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .deviceSession:
            let value = try wireCodec.decodeSession(record, plaintext: plaintext, devices: devices)
            try validate(record, identity: value.id.rawValue, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .deviceTab:
            let value = try wireCodec.decodeRemoteTab(
                record,
                plaintext: plaintext,
                devices: devices,
                workspaces: workspaces
            )
            try validate(record, identity: value.id.rawValue, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .historyVisit:
            let value = try wireCodec.decodeHistory(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version)
            let merged = try await repository.append(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .remoteCommand:
            let value = try wireCodec.decodeRemoteCommand(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            guard let commandSigner,
                  value.envelope.payload.sourceDeviceID == commandSigner.sourceDeviceID else {
                return
            }
            if let existing = commandStates[value.id],
               existing.envelope != value.envelope {
                throw CompanionSyncBridgeError.envelopeMismatch
            }
            if commandStates[value.id].map({ $0.version < value.version }) ?? true {
                commandStates[value.id] = value
            }
        case .appearance:
            let value = try wireCodec.decodeAppearance(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .permittedSetting:
            let value = try wireCodec.decodePermittedSetting(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .extensionInventory:
            let value = try wireCodec.decodeExtensionInventory(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .developerAsset:
            let value = try wireCodec.decodeDeveloperAsset(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            let merged = try await repository.upsert(value)
            if merged.version > value.version { try await enqueue(merged) }
        case .orderKey, .tombstone, .recoveryMetadata, .history:
            return
        case .cookie, .password, .autofill, .siteData, .cache, .permission,
             .extensionStorage, .incognito, .keychainSecret, .headerSecret,
             .httpAuthSecret:
            throw SyncBoundaryError.dataClassDenied(record.dataClass)
        }
    }

    private func validate(
        _ record: SyncRecord,
        identity: UUID,
        version: SyncVersion
    ) throws {
        guard record.entityID == identity,
              record.schemaVersion == version.schemaVersion,
              record.modifiedAt == version.modifiedAt,
              record.originatingDevice == version.modifiedBy else {
            throw CompanionSyncBridgeError.envelopeMismatch
        }
    }
}
