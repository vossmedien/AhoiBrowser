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
    let repository: LocalFirstRepository
    let provider: any CompanionSyncTransporting
    let codec: CompanionPayloadCodec
    let wireCodec = DesktopWirePayloadCodec()
    let commandSigner: (any RemoteCommandSigning)?
    let commandOwnershipStore: any RemoteCommandOwnershipStoring
    var commandStates: [UUID: RemoteCommandState] = [:]
    var bookmarkSyncEnabled = false
    var bookmarkHydrationRequired = false
    var browserSettingApprovedIDs = Set<UUID>()
    var browserSettingsApprovalEpoch: UInt64 = 0
    var browserSettingsHydrationRequired = false
    var extensionSetupMetadataApproved = false
    var extensionSetupMetadataEpoch: UInt64 = 0
    var extensionSetupHydrationRequired = false
    var extensionStorageMetadataApproved = false
    var extensionStorageMetadataEpoch: UInt64 = 0
    var extensionStorageHydrationRequired = false
    private var syncInProgress = false
    private var syncRequestedWhileInProgress = false
    private var syncWaiters: [CheckedContinuation<Void, any Error>] = []

    public nonisolated let remoteControlConfigured: Bool

    public init(
        repository: LocalFirstRepository,
        provider: CloudKitSyncProvider,
        sealer: any CompanionPayloadSealer,
        commandSigner: (any RemoteCommandSigning)? = nil,
        commandOwnershipStore: any RemoteCommandOwnershipStoring =
            InMemoryRemoteCommandOwnershipStore()
    ) {
        self.repository = repository
        self.provider = provider
        self.codec = CompanionPayloadCodec(sealer: sealer)
        self.commandSigner = commandSigner
        self.commandOwnershipStore = commandOwnershipStore
        self.remoteControlConfigured = commandSigner != nil
        provider.configureBrowserSettingValidation(Self.browserSettingValidator(codec: self.codec))
    }

    init(
        repository: LocalFirstRepository,
        transport: any CompanionSyncTransporting,
        sealer: any CompanionPayloadSealer,
        commandSigner: (any RemoteCommandSigning)? = nil,
        commandOwnershipStore: any RemoteCommandOwnershipStoring =
            InMemoryRemoteCommandOwnershipStore()
    ) {
        self.repository = repository
        self.provider = transport
        self.codec = CompanionPayloadCodec(sealer: sealer)
        self.commandSigner = commandSigner
        self.commandOwnershipStore = commandOwnershipStore
        self.remoteControlConfigured = commandSigner != nil
        transport.configureBrowserSettingValidation(Self.browserSettingValidator(codec: self.codec))
    }


    public func syncNow() async throws {
        if syncInProgress {
            syncRequestedWhileInProgress = true
            return try await withCheckedThrowingContinuation { continuation in
                syncWaiters.append(continuation)
            }
        }
        syncInProgress = true
        var outcome: Result<Void, any Error> = .success(())
        do {
            repeat {
                syncRequestedWhileInProgress = false
                try await performBoundedSyncNow()
            } while syncRequestedWhileInProgress
        } catch {
            outcome = .failure(error)
        }
        syncInProgress = false
        let completedWaiters = syncWaiters
        syncWaiters.removeAll()
        for waiter in completedWaiters {
            switch outcome {
            case .success:
                waiter.resume()
            case .failure(let error):
                waiter.resume(throwing: error)
            }
        }
        try outcome.get()
    }

    private func performBoundedSyncNow() async throws {
        var passID: UInt64?
        do {
            passID = try await provider.fetchChanges()
            try await importFetchedRecords()
            // Import can enqueue a composite record after field-level conflict
            // resolution. Flush it in the same user-visible sync operation. A
            // second bounded import/send round covers a server-record conflict
            // produced by that first post-merge send without polling forever.
            guard let passID else { throw CloudKitSyncProviderError.boundedSyncPassRequired }
            try await provider.sendPendingChanges(passID: passID)
            try await importFetchedRecords()
            try await provider.sendPendingChanges(passID: passID)
            try await provider.finalizeBoundedSync(passID: passID)
        } catch {
            if let passID { provider.abortBoundedSyncPass(passID) }
            throw error
        }
    }

    public func importFetchedRecords() async throws {
        try provider.beginDomainMergeActivity()
        defer { provider.endDomainMergeActivity() }
        let fetchedRecords = try await provider.pendingFetchedRecords()
        let recoveryRecords = try await provider.pendingQuarantineRecoveryRecords()
        let hydrateBookmarks = bookmarkSyncEnabled && bookmarkHydrationRequired
        let settingsEpoch = browserSettingsApprovalEpoch
        let hydrateSettings = browserSettingsHydrationRequired && !browserSettingApprovedIDs.isEmpty
        let extensionEpoch = extensionSetupMetadataEpoch
        let hydrateExtensionSetup = extensionSetupHydrationRequired &&
            provider.isExtensionSetupMetadataApproved(epoch: extensionEpoch)
        let storageEpoch = extensionStorageMetadataEpoch
        let hydrateExtensionStorage = extensionStorageHydrationRequired &&
            provider.isExtensionStorageMetadataApproved(epoch: storageEpoch)
        // One cache read for this hydration pass, not a full store scan per
        // approved category. This does not broaden any category's admission.
        let hydrateAny = hydrateBookmarks || hydrateSettings ||
            hydrateExtensionSetup || hydrateExtensionStorage
        let cachedRecords = hydrateAny ? try await provider.allRecords() : []
        let bookmarkRecords = hydrateBookmarks
            ? cachedRecords.filter { $0.dataClass == .bookmark } : []
        let settingRecords = hydrateSettings ? cachedRecords.filter {
            $0.dataClass == .permittedSetting && browserSettingApprovedIDs.contains($0.entityID)
        } : []
        let extensionRecords = hydrateExtensionSetup ? cachedRecords.filter {
            $0.dataClass == .permittedSetting &&
                !CompanionBrowserSettingCatalog.recordIDs.contains($0.entityID)
        } : []
        let storageRecords = hydrateExtensionStorage ? cachedRecords.filter {
            $0.dataClass == .permittedSetting &&
                CompanionExtensionStorage.recordIDs.contains($0.entityID)
        } : []
        let snapshot = try await repository.currentSnapshot()
        let importContext = ImportContext(snapshot: snapshot)
        let candidates = Self.makeImportCandidates(
            primaryRecords: recoveryRecords + bookmarkRecords + settingRecords +
                extensionRecords + storageRecords,
            fetchedRecords: fetchedRecords
        )
        let ordered = candidates.enumerated().sorted { lhs, rhs in
            let lhsRank = Self.importRank(lhs.element.record.dataClass)
            let rhsRank = Self.importRank(rhs.element.record.dataClass)
            if lhsRank != rhsRank { return lhsRank < rhsRank }
            return lhs.offset < rhs.offset
        }
        var mutations: [CompanionImportMutation] = []
        var acceptedWithoutDomainMutation = Set<Int>()
        var remoteCommandImports: [(token: Int, state: RemoteCommandState)] = []
        for indexed in ordered {
            do {
                switch try decodeImportedCandidate(
                    indexed.element.record,
                    context: importContext
                ) {
                case .domain(let value):
                    mutations.append(.init(token: indexed.offset, value: value))
                case .remoteCommand(let state):
                    remoteCommandImports.append((indexed.offset, state))
                    acceptedWithoutDomainMutation.insert(indexed.offset)
                case .ignored:
                    acceptedWithoutDomainMutation.insert(indexed.offset)
                }
            } catch DesktopWirePayloadCodecError.missingDependency {
                // Provider pages are not whole snapshots. Keep the encrypted
                // inbox unacknowledged until its separately delivered owner exists.
                continue
            } catch DeviceCapabilityError.unknownDevice {
                continue
            } catch SharedTabTargetError.targetMismatch {
                // A Page URL group can arrive after its linked Presence.
                // No invented Page, eager navigation or delete from this gap.
                continue
            } catch {
                try await provider.quarantineImportedRecord(
                    indexed.element.record,
                    reason: "plaintext_or_merge_validation_failed"
                )
            }
        }

        // This is the sole domain persistence point for the complete page. A
        // store failure propagates globally and must not quarantine valid wire
        // records or acknowledge their durable inbox copies.
        let outcomes = try await repository.mergeImportedBatch(mutations)
        var successfulTokens = acceptedWithoutDomainMutation
        var acceptedExtensionSetupTokens = Set<Int>()
        var acceptedExtensionStorageTokens = Set<Int>()
        var localWinnerRecords: [UUID: SyncRecord] = [:]
        var authorizedDeveloperAssetIDs = Set<UUID>()
        var revokedDeveloperAssetIDs = Set<UUID>()
        for outcome in outcomes {
            switch outcome.disposition {
            case .rejected:
                try await provider.quarantineImportedRecord(
                    candidates[outcome.token].record,
                    reason: "plaintext_or_merge_validation_failed"
                )
            case .accepted(let merged, let shouldReenqueue):
                successfulTokens.insert(outcome.token)
                if case .permittedSetting(let setting) = merged,
                   CompanionExtensionSetup.decode(setting) != nil {
                    acceptedExtensionSetupTokens.insert(outcome.token)
                }
                if case .permittedSetting(let setting) = merged,
                   CompanionExtensionStorage.decode(setting) != nil {
                    acceptedExtensionStorageTokens.insert(outcome.token)
                }
                if case .developerAsset(let value) = merged {
                    if value.isDeleted || value.optedIn {
                        authorizedDeveloperAssetIDs.insert(value.id)
                        revokedDeveloperAssetIDs.remove(value.id)
                    } else {
                        authorizedDeveloperAssetIDs.remove(value.id)
                        revokedDeveloperAssetIDs.insert(value.id)
                    }
                }

                let canTransportDeveloperAsset: Bool
                if case .developerAsset(let value) = merged {
                    canTransportDeveloperAsset = value.isDeleted || value.optedIn
                } else {
                    canTransportDeveloperAsset = true
                }
                let canTransportSetting: Bool
                if case .permittedSetting(let setting) = merged {
                    canTransportSetting = settingsEpoch == browserSettingsApprovalEpoch &&
                        browserSettingApprovedIDs.contains(setting.id) &&
                        provider.isBrowserSettingApproved(setting.id, epoch: settingsEpoch)
                } else {
                    canTransportSetting = true
                }
                if shouldReenqueue && canTransportDeveloperAsset && canTransportSetting {
                    let winner = try makeRecord(for: merged)
                    localWinnerRecords[winner.recordID] = winner
                } else {
                    // Outcomes are ordered mutations over one working snapshot.
                    // The last result for an identity is authoritative: it must
                    // be able to cancel an earlier provisional local winner.
                    localWinnerRecords.removeValue(forKey: merged.id)
                }
            }
        }

        try await provider.commitImportedDomainResults(
            records: localWinnerRecords.values.sorted {
                $0.recordID.uuidString < $1.recordID.uuidString
            },
            authorizedDeveloperAssetIDs: authorizedDeveloperAssetIDs,
            revokedDeveloperAssetIDs: revokedDeveloperAssetIDs
        )
        for commandImport in remoteCommandImports {
            let state = commandImport.state
            if commandStates[state.id].map({ $0.version < state.version }) ?? true {
                commandStates[state.id] = state
            }
        }
        try await persistRemoteCommandOwnership(for: remoteCommandImports.map(\.state))
        pruneRemoteCommandReadModel()

        var fetchedToAcknowledge: [SyncRecord] = []
        for token in successfulTokens.sorted() {
            let candidate = candidates[token]
            // No consent means no payload validation. It must not clear an
            // older corruption/quarantine decision merely by acknowledging a
            // newly fetched opaque copy of that same bookmark record.
            let extensionReadApproved = extensionEpoch == extensionSetupMetadataEpoch &&
                provider.isExtensionSetupMetadataApproved(epoch: extensionEpoch) &&
                acceptedExtensionSetupTokens.contains(token)
            let storageReadApproved = storageEpoch == extensionStorageMetadataEpoch &&
                provider.isExtensionStorageMetadataApproved(epoch: storageEpoch) &&
                acceptedExtensionStorageTokens.contains(token)
            if (candidate.record.dataClass != .bookmark || bookmarkSyncEnabled) &&
                (candidate.record.dataClass != .permittedSetting ||
                    extensionReadApproved || storageReadApproved ||
                    (settingsEpoch == browserSettingsApprovalEpoch &&
                        browserSettingApprovedIDs.contains(candidate.record.entityID) &&
                        provider.isBrowserSettingApproved(
                            candidate.record.entityID, epoch: settingsEpoch
                        ))) {
                try await provider.resolveQuarantinedRecord(candidate.record)
            }
            if candidate.acknowledgeOnSuccess {
                fetchedToAcknowledge.append(candidate.record)
            }
        }
        try await provider.acknowledgeFetchedRecords(fetchedToAcknowledge)
        if hydrateBookmarks { bookmarkHydrationRequired = false }
        if hydrateSettings, settingsEpoch == browserSettingsApprovalEpoch {
            browserSettingsHydrationRequired = false
        }
        if hydrateExtensionSetup, extensionEpoch == extensionSetupMetadataEpoch {
            extensionSetupHydrationRequired = false
        }
        if hydrateExtensionStorage, storageEpoch == extensionStorageMetadataEpoch {
            extensionStorageHydrationRequired = false
        }
    }

    private final class ImportContext {
        var devices: [DeviceID: Device]
        var workspaces: [WorkspaceID: Workspace]
        var pages: [TreeNodeID: TreeNode]
        var developerAssets: [UUID: CompanionDeveloperAssetRecord]

        init(snapshot: CompanionSnapshot) {
            devices = snapshot.devices.reduce(into: [:]) { result, value in
                if result[value.id].map({ $0.version >= value.version }) != true {
                    result[value.id] = value
                }
            }
            workspaces = snapshot.workspaces.reduce(into: [:]) { result, value in
                if result[value.id].map({ $0.version >= value.version }) != true {
                    result[value.id] = value
                }
            }
            pages = snapshot.treeNodes.reduce(into: [:]) { result, value in
                if result[value.id].map({ $0.version >= value.version }) != true {
                    result[value.id] = value
                }
            }
            developerAssets = snapshot.productRecords.developerAssets.reduce(into: [:]) {
                result, value in
                if result[value.id].map({ $0.version >= value.version }) != true {
                    result[value.id] = value
                }
            }
        }
    }

    struct ImportCandidate: Equatable, Sendable {
        let record: SyncRecord
        let acknowledgeOnSuccess: Bool
    }

    static func makeImportCandidates(
        primaryRecords: [SyncRecord],
        fetchedRecords: [SyncRecord]
    ) -> [ImportCandidate] {
        var candidates = primaryRecords.map {
            ImportCandidate(record: $0, acknowledgeOnSuccess: false)
        }
        var candidateIndex: [SyncRecord: Int] = [:]
        for (index, candidate) in candidates.enumerated() {
            // Duplicate recovery candidates are benign; retain the first stable
            // ordering slot and upgrade its acknowledgement bit below if the
            // same exact envelope also exists in the fetched inbox.
            if candidateIndex[candidate.record] == nil {
                candidateIndex[candidate.record] = index
            }
        }
        for record in fetchedRecords {
            if let index = candidateIndex[record] {
                // A fetched envelope may also be the selected transport
                // snapshot. Import it once and acknowledge its inbox copy.
                candidates[index] = ImportCandidate(
                    record: record,
                    acknowledgeOnSuccess: true
                )
            } else {
                candidateIndex[record] = candidates.count
                candidates.append(ImportCandidate(
                    record: record,
                    acknowledgeOnSuccess: true
                ))
            }
        }
        return candidates
    }

    public nonisolated func status() -> CloudKitSyncStatus {
        provider.status()
    }

    /// A physical CloudKit deletion is restored only after this bridge opens
    /// the retained encrypted envelope and revalidates its complete wire/domain
    /// contract. Transport metadata alone is never treated as payload proof.
    public func restorePhysicallyDeletedRecords() async throws {
        try provider.beginDomainMergeActivity()
        defer { provider.endDomainMergeActivity() }
        let context = ImportContext(snapshot: try await repository.currentSnapshot())
        let candidates = try await provider.physicalDeletionRecoveryCandidates()
        for candidate in candidates {
            do {
                let decision = try validatePhysicalDeletionRecoveryRecord(
                    candidate.record,
                    context: context
                )
                switch decision {
                case .restore:
                    _ = try await provider.restorePhysicallyDeletedRecord(
                        candidate.record,
                        expectedGeneration: candidate.generation
                    )
                case .acceptDeletion:
                    _ = try await provider.acceptPhysicalDeletion(
                        recordID: candidate.record.recordID,
                        expectedGeneration: candidate.generation
                    )
                }
            } catch {
                // Preserve the physical-delete generation for an explicit later
                // retry. In particular, never replace an earlier domain failure
                // with a weaker transport-only recovery claim.
                continue
            }
        }
        if await provider.hasPhysicalDeletionQuarantine() {
            throw CloudKitSyncProviderError.physicalDeletionRecoveryIncomplete
        }
    }

    private enum DecodedImport {
        case domain(CompanionImportedValue)
        case remoteCommand(RemoteCommandState)
        case ignored
    }

    private static func importRank(_ dataClass: SyncDataClass) -> Int {
        switch dataClass {
        case .device: 0
        case .workspace: 1
        case .deviceSession: 3
        case .deviceTab: 4
        default: 2
        }
    }

    private func decodeImportedCandidate(
        _ record: SyncRecord,
        context: ImportContext
    ) throws -> DecodedImport {
        if record.dataClass == .bookmark && !bookmarkSyncEnabled { return .ignored }
        if record.dataClass == .permittedSetting &&
            (record.recordID != record.entityID ||
                !browserSettingApprovedIDs.contains(record.entityID) ||
                !provider.isBrowserSettingApproved(
                    record.entityID, epoch: browserSettingsApprovalEpoch
                )) && !canReadExtensionSetup(record) &&
                !canReadExtensionStorage(record) { return .ignored }
        // One live format. Unsupported development input remains in recovery;
        // it is never normalized into a current authoritative snapshot.
        guard record.schemaVersion == SharedSyncFormat.currentVersion else {
            throw SharedTabWirePreparationError.unsupportedVersion
        }
        let plaintext = try codec.openData(record)
        switch record.dataClass {
        case .device:
            let value = try wireCodec.decodeDevice(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
            if let existing = context.devices[value.id] {
                context.devices[value.id] = try CompanionReadModelFieldMerge.merge(
                    existing, value
                )
            } else {
                context.devices[value.id] = value
            }
            return .domain(.device(value))
        case .workspace:
            let value = try wireCodec.decodeWorkspace(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
            if let existing = context.workspaces[value.id] {
                context.workspaces[value.id] = try CompanionFieldMerge.merge(existing, value)
            } else {
                context.workspaces[value.id] = value
            }
            return .domain(.workspace(value))
        case .deviceCapability:
            return .domain(.deviceCapability(try wireCodec.decodeCapability(
                record, plaintext: plaintext, knownDevices: context.devices
            )))
        case .bookmark:
            return .domain(.bookmark(try decodeBookmarkRecord(record, plaintext: plaintext)))
        case .treeNode:
            let value = try wireCodec.decodeTreeNode(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         orderKey: value.orderKey, tombstone: value.tombstone)
            context.pages[value.id] = try context.pages[value.id].map {
                try CompanionFieldMerge.merge($0, value)
            } ?? value
            return .domain(.treeNode(value))
        case .deviceSession:
            let value = try wireCodec.decodeSession(
                record,
                plaintext: plaintext,
                devices: context.devices
            )
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
            return .domain(.session(value))
        case .deviceTab:
            let value = try wireCodec.decodeRemoteTab(
                record,
                plaintext: plaintext,
                devices: context.devices,
                workspaces: context.workspaces
            )
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
            try wireCodec.validatePresenceTarget(value, pages: context.pages)
            return .domain(.tab(value))
        case .historyVisit:
            let value = try wireCodec.decodeHistory(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
            return .domain(.history(value))
        case .remoteCommand:
            let value = try wireCodec.decodeRemoteCommand(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            guard let commandSigner,
                  value.envelope.payload.sourceDeviceID == commandSigner.sourceDeviceID else {
                return .ignored
            }
            try validateLocallyOwnedRemoteCommand(value)
            if let existing = commandStates[value.id], existing.envelope != value.envelope {
                throw CompanionSyncBridgeError.envelopeMismatch
            }
            return .remoteCommand(value)
        case .appearance:
            let value = try wireCodec.decodeAppearance(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
            return .domain(.appearance(value))
        case .permittedSetting:
            let value = try wireCodec.decodePermittedSetting(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
            if !browserSettingApprovedIDs.contains(value.id) &&
                !(canReadExtensionSetup(record) && CompanionExtensionSetup.decode(value) != nil) &&
                !(canReadExtensionStorage(record) && CompanionExtensionStorage.decode(value) != nil) {
                return .ignored
            }
            return .domain(.permittedSetting(value))
        case .extensionInventory:
            let value = try wireCodec.decodeExtensionInventory(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
            return .domain(.extensionInventory(value))
        case .developerAsset:
            let value = try wireCodec.decodeDeveloperAsset(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
            guard value.isDeleted || value.optedIn else {
                throw CompanionProductRecordError.developerAssetNotOptedIn
            }
            return .domain(.developerAsset(value))
        case .orderKey, .tombstone, .recoveryMetadata, .history:
            return .ignored
        case .cookie, .password, .autofill, .siteData, .cache, .permission,
             .extensionStorage, .incognito, .keychainSecret, .headerSecret,
             .httpAuthSecret:
            throw SyncBoundaryError.dataClassDenied(record.dataClass)
        }
    }

    private func makeRecord(for value: CompanionImportedValue) throws -> SyncRecord {
        switch value {
        case .deviceCapability(let value):
            return try makeCapabilityRecord(value)
        case .bookmark(let value):
            return try makeBookmarkRecord(value)
        case .device(let value):
            return try codec.makeRecord(
                recordID: value.id.rawValue, entityID: value.id.rawValue,
                dataClass: .device, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .workspace(let value):
            return try codec.makeRecord(
                recordID: value.id.rawValue, entityID: value.id.rawValue,
                dataClass: .workspace, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .treeNode(let value):
            return try codec.makeRecord(
                recordID: value.id.rawValue, entityID: value.id.rawValue,
                dataClass: .treeNode, version: value.version,
                plaintext: wireCodec.encode(value), orderKey: value.orderKey,
                tombstone: value.tombstone
            )
        case .session(let value):
            return try codec.makeRecord(
                recordID: value.id.rawValue, entityID: value.id.rawValue,
                dataClass: .deviceSession, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .tab(let value):
            return try codec.makeRecord(
                recordID: value.id.rawValue, entityID: value.id.rawValue,
                dataClass: .deviceTab, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .history(let value):
            return try codec.makeRecord(
                recordID: value.id.rawValue, entityID: value.id.rawValue,
                dataClass: .historyVisit, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .appearance(let value):
            return try codec.makeRecord(
                recordID: value.id, entityID: value.id,
                dataClass: .appearance, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .permittedSetting(let value):
            guard CompanionBrowserSettingCatalog.isPortable(value) else {
                throw CompanionProductRecordError.invalidPermittedSetting
            }
            return try codec.makeRecord(
                recordID: value.id, entityID: value.id,
                dataClass: .permittedSetting, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .extensionInventory(let value):
            return try codec.makeRecord(
                recordID: value.id, entityID: value.id,
                dataClass: .extensionInventory, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        case .developerAsset(let value):
            return try codec.makeRecord(
                recordID: value.id, entityID: value.id,
                dataClass: .developerAsset, version: value.version,
                plaintext: wireCodec.encode(value), tombstone: value.tombstone
            )
        }
    }

    private enum PhysicalDeletionRecoveryDecision {
        case restore
        case acceptDeletion
    }

    private func validatePhysicalDeletionRecoveryRecord(
        _ record: SyncRecord,
        context: ImportContext
    ) throws -> PhysicalDeletionRecoveryDecision {
        guard record.schemaVersion == SharedSyncFormat.currentVersion else {
            throw SharedTabWirePreparationError.unsupportedVersion
        }
        let plaintext = try codec.openData(record)
        switch record.dataClass {
        case .deviceCapability:
            _ = try wireCodec.decodeCapability(record, plaintext: plaintext, knownDevices: context.devices)
        case .bookmark:
            guard bookmarkSyncEnabled else {
                throw CompanionSyncBridgeError.unsupportedDataClass(.bookmark)
            }
            _ = try decodeBookmarkRecord(record, plaintext: plaintext)
        case .device:
            let value = try wireCodec.decodeDevice(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
        case .workspace:
            let value = try wireCodec.decodeWorkspace(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
        case .treeNode:
            let value = try wireCodec.decodeTreeNode(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         orderKey: value.orderKey, tombstone: value.tombstone)
        case .deviceSession:
            let value = try wireCodec.decodeSession(
                record,
                plaintext: plaintext,
                devices: context.devices
            )
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
        case .deviceTab:
            let value = try wireCodec.decodeRemoteTab(
                record,
                plaintext: plaintext,
                devices: context.devices,
                workspaces: context.workspaces
            )
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
            try wireCodec.validatePresenceTarget(value, pages: context.pages)
        case .historyVisit:
            let value = try wireCodec.decodeHistory(record, plaintext: plaintext)
            try validate(record, identity: value.id.rawValue, version: value.version,
                         tombstone: value.tombstone)
        case .remoteCommand:
            let value = try wireCodec.decodeRemoteCommand(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version)
            guard let commandSigner,
                  value.envelope.payload.sourceDeviceID == commandSigner.sourceDeviceID else {
                throw CompanionSyncBridgeError.remoteCommandSigningUnavailable
            }
            try validateLocallyOwnedRemoteCommand(value)
        case .appearance:
            let value = try wireCodec.decodeAppearance(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
        case .permittedSetting:
            let value = try wireCodec.decodePermittedSetting(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
        case .extensionInventory:
            let value = try wireCodec.decodeExtensionInventory(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
        case .developerAsset:
            let value = try wireCodec.decodeDeveloperAsset(record, plaintext: plaintext)
            try validate(record, identity: value.id, version: value.version,
                         tombstone: value.tombstone)
            guard value.isDeleted || value.optedIn else {
                throw CompanionProductRecordError.developerAssetNotOptedIn
            }
            guard let current = context.developerAssets[value.id] else {
                return .acceptDeletion
            }
            guard current.isDeleted || current.optedIn else {
                return .acceptDeletion
            }
            guard current == value else {
                throw CompanionSyncBridgeError.envelopeMismatch
            }
        case .orderKey, .tombstone, .recoveryMetadata, .history:
            throw CompanionSyncBridgeError.unsupportedDataClass(record.dataClass)
        case .cookie, .password, .autofill, .siteData, .cache, .permission,
             .extensionStorage, .incognito, .keychainSecret, .headerSecret,
             .httpAuthSecret:
            throw SyncBoundaryError.dataClassDenied(record.dataClass)
        }
        return .restore
    }

    func validate(
        _ record: SyncRecord,
        identity: UUID,
        version: SyncVersion,
        orderKey: OrderKey? = nil,
        tombstone: Tombstone? = nil
    ) throws {
        guard record.recordID == identity,
              record.entityID == identity,
              record.schemaVersion == version.schemaVersion,
              record.modifiedAt == version.modifiedAt,
              record.originatingDevice == version.modifiedBy,
              record.orderKey == orderKey,
              record.tombstone == tombstone else {
            throw CompanionSyncBridgeError.envelopeMismatch
        }
    }
}
