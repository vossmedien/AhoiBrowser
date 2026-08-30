import Foundation
import AhoiCloudKitSpike

public enum CompanionDeviceRevocationError: Error, Equatable, Sendable {
    case deviceNotFound
    case authorIdentityUnavailable
    case localSignerUnavailable
    case timestampOverflow
}

extension CompanionDeviceRevocationError: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .deviceNotFound:
            CompanionL10n.string(
                "settings.devices.error.not_found",
                fallback: "The device is no longer available."
            )
        case .authorIdentityUnavailable:
            CompanionL10n.string(
                "settings.devices.error.identity",
                fallback: "Device removal requires this device's local identity."
            )
        case .localSignerUnavailable:
            CompanionL10n.string(
                "settings.devices.error.signer",
                fallback: "The local signing identity could not be revoked safely."
            )
        case .timestampOverflow:
            CompanionL10n.string(
                "settings.devices.error.clock",
                fallback: "The device removal timestamp could not be created safely."
            )
        }
    }
}

public struct CompanionDeviceRevocationResult: Equatable, Sendable {
    public let device: Device
    public let didMutate: Bool

    public init(device: Device, didMutate: Bool) {
        self.device = device
        self.didMutate = didMutate
    }
}

public struct CompanionDeviceRevocationOutcome: Equatable, Sendable {
    public let deviceID: DeviceID
    public let removedCurrentDevice: Bool
    public let didMutate: Bool
    public let transportQueued: Bool

    public init(
        deviceID: DeviceID,
        removedCurrentDevice: Bool,
        didMutate: Bool,
        transportQueued: Bool
    ) {
        self.deviceID = deviceID
        self.removedCurrentDevice = removedCurrentDevice
        self.didMutate = didMutate
        self.transportQueued = transportQueued
    }
}

public extension LocalFirstRepository {
    /// Commits an irreversible local device tombstone through the repository's
    /// normal durable merge path. A repeated request returns the existing
    /// tombstone byte-for-byte and performs no additional write.
    func revokeAndRemoveDevice(
        _ deviceID: DeviceID,
        revokedBy authorDeviceID: DeviceID,
        atMilliseconds suppliedNow: UInt64? = nil
    ) async throws -> CompanionDeviceRevocationResult {
        let current = try await currentSnapshot()
        guard let existing = current.devices.first(where: { $0.id == deviceID }) else {
            throw CompanionDeviceRevocationError.deviceNotFound
        }
        guard !existing.isDeleted else {
            return .init(device: existing, didMutate: false)
        }

        let now = suppliedNow ?? UInt64(Date().timeIntervalSince1970 * 1_000)
        let clock = try CompanionDeviceRevocationPolicy.nextClock(
            for: existing,
            authorDeviceID: authorDeviceID,
            nowMilliseconds: now
        )
        let purgeAfter = try CompanionDeviceRevocationPolicy.purgeAfter(clock)
        let tombstone = Tombstone(
            entityID: existing.id.rawValue,
            deletedAt: clock,
            deletedBy: authorDeviceID,
            originalParentID: nil,
            originalOrderKey: nil,
            purgeAfterMilliseconds: purgeAfter
        )
        let revoked = Device(
            deviceID: existing.id,
            name: existing.name,
            kind: existing.kind,
            createdAt: existing.createdAt,
            lastSeenAt: existing.lastSeenAt,
            isOnline: false,
            isRevoked: true,
            version: CompanionDeviceRevocationPolicy.version(
                for: existing,
                clock: clock,
                authorDeviceID: authorDeviceID
            ),
            tombstone: tombstone
        )
        return .init(device: try await upsert(revoked), didMutate: true)
    }
}

private enum CompanionDeviceRevocationPolicy {
    private static let deviceFields: Set<String> = [
        "type", "display_name", "created_at", "last_seen", "retired", "tombstone",
    ]
    private static let tombstoneRetentionMilliseconds: UInt64 =
        30 * 24 * 60 * 60 * 1_000

    static func nextClock(
        for device: Device,
        authorDeviceID: DeviceID,
        nowMilliseconds: UInt64
    ) throws -> HybridLogicalClock {
        let observed = ([device.version.modifiedAt] +
            Array(device.version.fieldVersions.values)).max() ?? device.version.modifiedAt
        let local = HybridLogicalClock(
            physicalMilliseconds: nowMilliseconds,
            nodeID: authorDeviceID
        )
        return try local.merging(observed, at: nowMilliseconds)
    }

    static func version(
        for device: Device,
        clock: HybridLogicalClock,
        authorDeviceID: DeviceID
    ) -> SyncVersion {
        var fieldVersions = device.version.normalized(for: deviceFields).fieldVersions
        fieldVersions["retired"] = clock
        fieldVersions["tombstone"] = clock
        return SyncVersion(
            schemaVersion: max(device.version.schemaVersion, 2),
            modifiedAt: clock,
            modifiedBy: authorDeviceID,
            fieldVersions: fieldVersions
        )
    }

    static func purgeAfter(_ clock: HybridLogicalClock) throws -> UInt64 {
        let (result, overflow) = clock.physicalMilliseconds.addingReportingOverflow(
            tombstoneRetentionMilliseconds
        )
        guard !overflow else { throw CompanionDeviceRevocationError.timestampOverflow }
        return result
    }
}

extension CompanionSyncBridge {
    public func remoteControlSourceDeviceID() throws -> DeviceID {
        guard let commandSigner else {
            throw CompanionDeviceRevocationError.localSignerUnavailable
        }
        return commandSigner.sourceDeviceID
    }

    public func remoteControlIdentityIsRevoked() throws -> Bool {
        guard let commandSigner else {
            throw CompanionDeviceRevocationError.localSignerUnavailable
        }
        return try commandSigner.identityIsRevoked()
    }

    @discardableResult
    public func deleteRemoteControlIdentity() throws -> RemoteControlProvisioningIdentity {
        guard let commandSigner else {
            throw CompanionDeviceRevocationError.localSignerUnavailable
        }
        let archived = try commandSigner.deleteIdentity()
        commandStates.removeAll(keepingCapacity: false)
        return archived
    }

    public func rotateRemoteControlIdentity() throws -> RemoteControlProvisioningIdentity {
        guard let commandSigner else {
            throw CompanionDeviceRevocationError.localSignerUnavailable
        }
        let identity = try commandSigner.rotateIdentity()
        commandStates.removeAll(keepingCapacity: false)
        return identity
    }
}

@MainActor
public extension CompanionAppModel {
    var canManageSyncedDevices: Bool {
        remoteControlIdentity != nil || syncBridge != nil
    }

    func isCurrentCommandDevice(_ deviceID: DeviceID) -> Bool {
        remoteControlIdentity?.sourceDeviceID == deviceID
    }

    func canTargetRemoteCommand(_ deviceID: DeviceID) -> Bool {
        snapshot.devices.contains {
            $0.id == deviceID && !$0.isDeleted && !$0.isRevoked
        }
    }

    @discardableResult
    func revokeAndRemoveDevice(
        _ deviceID: DeviceID
    ) async -> CompanionDeviceRevocationOutcome? {
        let bridge = syncBridge
        var authorDeviceID = remoteControlIdentity?.sourceDeviceID
        if authorDeviceID == nil, let bridge {
            authorDeviceID = try? await bridge.remoteControlSourceDeviceID()
        }
        guard let authorDeviceID else {
            loadError = CompanionDeviceRevocationError.authorIdentityUnavailable
                .localizedDescription
            return nil
        }
        let removesCurrentDevice = authorDeviceID == deviceID

        if removesCurrentDevice {
            guard let bridge else {
                loadError = CompanionDeviceRevocationError.localSignerUnavailable
                    .localizedDescription
                return nil
            }
            do {
                _ = try await bridge.deleteRemoteControlIdentity()
                remoteControlIdentity = nil
            } catch {
                loadError = error.localizedDescription
                return nil
            }
        }

        let mutation: CompanionDeviceRevocationResult
        do {
            mutation = try await repository.revokeAndRemoveDevice(
                deviceID,
                revokedBy: authorDeviceID
            )
            snapshot = try await repository.currentSnapshot()
            cancelRemoteCommandFollowUp(targeting: deviceID)
        } catch {
            loadError = error.localizedDescription
            return nil
        }

        var transportQueued = false
        if mutation.didMutate, let bridge {
            do {
                try await bridge.enqueue(mutation.device)
                transportQueued = true
                if syncProvider != nil { await sync() }
            } catch {
                // The local tombstone is already durable and will seed the
                // encrypted outbox on a later healthy sync activation.
                loadError = error.localizedDescription
            }
        }
        snapshot = (try? await repository.currentSnapshot()) ?? snapshot
        remoteCommandStatus = CompanionL10n.string(
            transportQueued
                ? "settings.devices.removed"
                : "settings.devices.removed_local",
            fallback: transportQueued
                ? "The device was revoked and queued for sync."
                : "The device was revoked locally; sync is still pending."
        )
        if transportQueued { loadError = nil }
        return .init(
            deviceID: deviceID,
            removedCurrentDevice: removesCurrentDevice,
            didMutate: mutation.didMutate,
            transportQueued: transportQueued
        )
    }

    func deleteRemoteControlSigningIdentity() async {
        guard let bridge = syncBridge else {
            loadError = CompanionDeviceRevocationError.localSignerUnavailable
                .localizedDescription
            return
        }
        do {
            _ = try await bridge.deleteRemoteControlIdentity()
            remoteControlIdentity = nil
            resetRemoteCommandPresentation()
            remoteCommandStatus = CompanionL10n.string(
                "settings.remote.deleted",
                fallback: "The local signing key was deleted. Remote commands are blocked."
            )
            loadError = nil
        } catch {
            loadError = error.localizedDescription
        }
    }

    func rotateRemoteControlSigningIdentity() async {
        guard let bridge = syncBridge else {
            loadError = CompanionDeviceRevocationError.localSignerUnavailable
                .localizedDescription
            return
        }
        do {
            remoteControlIdentity = try await bridge.rotateRemoteControlIdentity()
            resetRemoteCommandPresentation()
            remoteCommandStatus = CompanionL10n.string(
                "settings.remote.rotated",
                fallback: "A new local signing key is ready. Macs must approve it again."
            )
            loadError = nil
        } catch {
            remoteControlIdentity = nil
            loadError = error.localizedDescription
        }
    }

    private func cancelRemoteCommandFollowUp(targeting deviceID: DeviceID) {
        let commandIDs = recentRemoteCommands.lazy
            .filter { $0.targetDeviceID == deviceID }
            .map(\.id)
        for commandID in commandIDs {
            commandFollowUpTasks[commandID]?.cancel()
            commandFollowUpTasks[commandID] = nil
        }
    }

    private func resetRemoteCommandPresentation() {
        commandFollowUpTasks.values.forEach { $0.cancel() }
        commandFollowUpTasks.removeAll()
        commandLabels.removeAll()
        recentRemoteCommands.removeAll()
    }
}

@MainActor
extension CompanionAppModel {
#if DEBUG
    static let deviceRevocationFixtureLocalID = DeviceID(rawValue: UUID(
        uuidString: "71000000-0000-4000-8000-000000000001"
    )!)
    static let deviceRevocationFixtureRemoteID = DeviceID(rawValue: UUID(
        uuidString: "72000000-0000-4000-8000-000000000002"
    )!)
#endif

    func loadDeviceRevocationUITestFixtureIfRequested() async {
#if DEBUG
        guard ProcessInfo.processInfo.arguments.contains(
            "-AhoiUITestDeviceRevocation"
        ) else { return }
        let localID = Self.deviceRevocationFixtureLocalID
        let remoteID = Self.deviceRevocationFixtureRemoteID
        let createdAt = HybridLogicalClock(
            physicalMilliseconds: 1_700_000_000_000,
            nodeID: localID
        )
        let now = UInt64(Date().timeIntervalSince1970 * 1_000)
        do {
            try await repository.upsert(Device(
                deviceID: localID,
                name: "Fixture iPhone",
                kind: .iPhone,
                createdAt: createdAt,
                lastSeenAt: .init(physicalMilliseconds: now, nodeID: localID),
                isOnline: true,
                version: fixtureDeviceVersion(now: now, author: localID)
            ))
            try await repository.upsert(Device(
                deviceID: remoteID,
                name: "Fixture Mac",
                kind: .mac,
                createdAt: .init(
                    physicalMilliseconds: 1_700_000_000_000,
                    nodeID: remoteID
                ),
                lastSeenAt: .init(physicalMilliseconds: now, nodeID: remoteID),
                isOnline: true,
                version: fixtureDeviceVersion(now: now, author: localID)
            ))
            remoteControlIdentity = .init(
                sourceDeviceID: localID,
                publicKey: Data(repeating: 0x71, count: 32)
            )
            snapshot = try await repository.currentSnapshot()
            loadError = nil
        } catch {
            loadError = error.localizedDescription
        }
#endif
    }

#if DEBUG
    private func fixtureDeviceVersion(
        now: UInt64,
        author: DeviceID
    ) -> SyncVersion {
        let clock = HybridLogicalClock(
            physicalMilliseconds: now,
            nodeID: author
        )
        return SyncVersion(
            modifiedAt: clock,
            modifiedBy: author,
            fieldVersions: Dictionary(
                uniqueKeysWithValues: [
                    "type", "display_name", "created_at", "last_seen", "retired", "tombstone",
                ].map { ($0, clock) }
            )
        )
    }
#endif
}
