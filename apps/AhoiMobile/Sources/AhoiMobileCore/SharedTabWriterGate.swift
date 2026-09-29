import Foundation
import AhoiCloudKitSpike

/// Mobile side of the ADR 0009 shared-tab writer gate. It mirrors the desktop
/// `ProfileSyncBackend::PublishLocalCapability` / `SharedTabState()` pair in
/// `overlay/chromium/src/ahoi/browser/sync/profile_sync_backend_shared_tabs.cc`:
///
/// - Every enrolled device announces one Format-3 `DeviceCapabilityRecord`
///   (UUIDv5 `ahoi:sync:capability:v1:<device>`, models `[3]`) as control
///   metadata right after its own Device record. `shared-normal-tabs-v3` is
///   declared only once native capture and projection are actually wired.
/// - A declared feature is never withdrawn because local wiring is briefly
///   absent (desktop: closing the last window is not a format downgrade). A
///   retired/tombstoned local Device or a tombstoned declaration is never
///   rewritten or resurrected.
/// - This device writes its own shared-tab Pages/Presences to transport only
///   while every active (non-retired, non-tombstoned) Device declares the
///   feature with a valid record, no declaration belongs to an unknown Device,
///   and the provider acknowledged this device's Device and capability records.
enum SharedTabWriterGate {
    static func peers(_ devices: [Device]) -> [SharedTabCapabilityReadiness.Peer] {
        // Snapshot Devices are authenticated imports or local authoring, i.e.
        // independently known. Desktop skips `retired || tombstone` alike.
        devices.map {
            .init(deviceID: $0.id, independentlyKnown: true,
                  explicitlyRetired: $0.isRevoked || $0.isDeleted)
        }
    }

    static func assess(
        snapshot: CompanionSnapshot, localDevice: DeviceID, syncEnabled: Bool,
        nativeSupportActive: Bool, initialFetchComplete: Bool,
        localDeviceAcknowledged: Bool, localCapabilityAcknowledged: Bool
    ) -> SharedTabCapabilityReadiness.Assessment {
        // Desktop: without native capture there is no local writer at all.
        guard nativeSupportActive else {
            return .init(isReady: false, blockingDevices: [], bootstrapIncomplete: true)
        }
        return SharedTabCapabilityReadiness.evaluate(
            localDevice: localDevice, peers: peers(snapshot.devices),
            capabilities: snapshot.deviceCapabilities,
            globalSyncEnabled: syncEnabled, normalProfile: true,
            initialFetchComplete: initialFetchComplete,
            localDeviceAcknowledged: localDeviceAcknowledged,
            localCapabilityAcknowledged: localCapabilityAcknowledged
        )
    }
}

extension LocalFirstRepository {
    /// Desktop `PublishLocalCapability` parity. Returns the new declaration
    /// when one was durably written and must be queued, otherwise nil.
    func publishLocalSharedTabCapability(nativeSupportActive: Bool) async throws -> DeviceCapabilityRecord? {
        await acquireMutation()
        defer { releaseMutation() }
        try await loadIfNeeded()
        // Declarations follow the local Device record; a retired Device is not
        // re-enrolled by announcing capabilities.
        guard let device = snapshot.devices.first(where: { $0.id == localDeviceID }),
              !device.isDeleted, !device.isRevoked else { return nil }
        let id = SharedTabContract.capabilityID(for: localDeviceID)
        let features = nativeSupportActive ? [SharedTabContract.feature] : []
        let index = snapshot.deviceCapabilities.firstIndex { $0.id == id }
        if let index {
            let old = snapshot.deviceCapabilities[index]
            if old.isDeleted { return nil }
            let valid = (try? old.validate()) != nil
            if valid, old.features.contains(SharedTabContract.feature), !nativeSupportActive {
                return nil // Local wiring gap is not a format downgrade.
            }
            if valid, old.features == features,
               old.readableModels == [SharedSyncFormat.currentVersion],
               old.writableModels == [SharedSyncFormat.currentVersion] {
                return nil
            }
        }
        let version = try nextVersion().normalized(for: DeviceCapabilityRecord.syncFields)
        let record = try DeviceCapabilityRecord(deviceID: localDeviceID, features: features, version: version)
        if let index {
            snapshot.deviceCapabilities[index] = record
        } else {
            snapshot.deviceCapabilities.append(record)
        }
        try await persist()
        return record
    }
}

extension CompanionSyncBridge {
    /// Returns true when the gate is open and previously withheld local
    /// shared-tab records now need a reseed.
    func setSharedTabWriteAllowed(_ allowed: Bool) -> Bool {
        sharedTabWriteAllowed = allowed
        guard allowed, sharedTabOutboundWithheld else { return false }
        sharedTabOutboundWithheld = false
        return true
    }

    func isSharedTabWriteAllowed() -> Bool { sharedTabWriteAllowed }

    func hasWithheldSharedTabRecords() -> Bool { sharedTabOutboundWithheld }

    /// A Page authored by a local shared-tab path (capture, intent, close,
    /// saved-page). Other tree edits keep their existing transport path.
    func enqueueSharedTabPage(_ node: TreeNode) async throws {
        guard sharedTabWriteAllowed else {
            sharedTabOutboundWithheld = true
            return
        }
        try await enqueue(node)
    }

    func enqueueSharedTabPresence(_ tab: RemoteTab) async throws {
        guard sharedTabWriteAllowed || tab.deviceID != repository.localDeviceID else {
            sharedTabOutboundWithheld = true
            return
        }
        try await enqueue(tab)
    }

    func enqueueLocalCapability(_ value: DeviceCapabilityRecord) async throws {
        guard value.deviceID == repository.localDeviceID else {
            throw DeviceCapabilityError.invalidIdentity
        }
        try await provider.enqueue(makeCapabilityRecord(value))
    }

    func localSharedTabAcknowledgements() async -> (device: Bool, capability: Bool) {
        let local = repository.localDeviceID
        let device = await provider.isRecordAcknowledged(local.rawValue)
        let capability = await provider.isRecordAcknowledged(SharedTabContract.capabilityID(for: local))
        return (device, capability)
    }
}

extension CompanionAppModel {
    /// Called when the browser's native shared-tab capture and projection are
    /// installed (`MobileSharedTabIntentBinding`). Idempotent; never reverts.
    func activateSharedTabNativeSupport() {
        guard !sharedTabNativeSupportActive else { return }
        sharedTabNativeSupportActive = true
        guard desiredSyncEnabled, syncBridge != nil else { return }
        Task { [weak self] in await self?.sync() } // Publish the upgraded declaration.
    }

    func publishLocalSharedTabCapability(using bridge: CompanionSyncBridge) async {
        let record: DeviceCapabilityRecord?
        do {
            record = try await repository.publishLocalSharedTabCapability(
                nativeSupportActive: sharedTabNativeSupportActive)
        } catch {
            return // Durable local state unchanged; the next pass retries.
        }
        guard let record else { return }
        do {
            try await bridge.enqueueLocalCapability(record)
        } catch {
            localSnapshotReseedRequired = true // The reseed carries the declaration.
        }
    }

    /// Re-evaluates the gate after a completed bounded pass. Returns true when
    /// another pass is required (withheld backlog reseed or pending own ACK).
    func refreshSharedTabWriterGate(using bridge: CompanionSyncBridge, generation: UInt64) async -> Bool {
        let isCurrent = { self.syncGeneration == generation && self.syncBridge === bridge }
        let acks = await bridge.localSharedTabAcknowledgements()
        guard isCurrent(), let current = try? await repository.currentSnapshot(),
              isCurrent() else { return false }
        let assessment = SharedTabWriterGate.assess(
            snapshot: current, localDevice: repository.localDeviceID,
            syncEnabled: desiredSyncEnabled, nativeSupportActive: sharedTabNativeSupportActive,
            initialFetchComplete: true,
            localDeviceAcknowledged: acks.device, localCapabilityAcknowledged: acks.capability
        )
        sharedTabWriterAssessment = assessment
        let reseed = await bridge.setSharedTabWriteAllowed(assessment.isReady)
        if reseed {
            localSnapshotReseedRequired = true
            return true
        }
        // A just-sent own record may be acknowledged only after this pass;
        // retry once per runtime generation instead of waiting for a user event.
        if !assessment.isReady, !(acks.device && acks.capability),
           sharedTabNativeSupportActive, sharedTabAckRetryGeneration != generation {
            sharedTabAckRetryGeneration = generation
            return true
        }
        return false
    }
}
