import Foundation

public enum SharedSyncFormatError: Error, Equatable, Sendable {
    case unsupportedVersion
    case invalidClock
    case invalidFieldMap
}

/// The one current wire format shared by every permitted entity. Membership
/// does not grant consent, command authority or a functional capability.
public enum SharedSyncFormat {
    public static let currentVersion: UInt32 = 3
    public static let supportedDataClasses: Set<SyncDataClass> = [
        .device,
        .workspace,
        .treeNode,
        .historyVisit,
        .deviceTab,
        .deviceSession,
        .remoteCommand,
        .appearance,
        .permittedSetting,
        .extensionInventory,
        .developerAsset,
        .bookmark,
        .deviceCapability,
    ]

    /// Domain clocks use Unix milliseconds plus a lossless microsecond part;
    /// the wire uses signed Int64 Windows microseconds. Check before arithmetic.
    public static func isValidClock(_ clock: HybridLogicalClock) -> Bool {
        let maximumUnixMicroseconds = UInt64(Int64.max) - 11_644_473_600_000_000
        guard clock.nodeID.rawValue.uuidString != "00000000-0000-0000-0000-000000000000",
              clock.submillisecondMicroseconds < 1_000,
              clock.physicalMilliseconds <= maximumUnixMicroseconds / 1_000 else { return false }
        return clock.physicalMilliseconds * 1_000 + UInt64(clock.submillisecondMicroseconds)
            <= maximumUnixMicroseconds
    }

    /// Incoming and persisted domain records must already have exact clocks.
    /// Only a new, explicitly local authoring value may have an entirely empty map.
    @discardableResult
    public static func validate(
        _ version: SyncVersion, fields: Set<String>, allowLocalEmpty: Bool = false
    ) throws -> SyncVersion {
        guard version.schemaVersion == currentVersion else {
            throw SharedSyncFormatError.unsupportedVersion
        }
        guard isValidClock(version.modifiedAt), version.modifiedBy == version.modifiedAt.nodeID else {
            throw SharedSyncFormatError.invalidClock
        }
        guard (allowLocalEmpty && version.fieldVersions.isEmpty) || Set(version.fieldVersions.keys) == fields else {
            throw SharedSyncFormatError.invalidFieldMap
        }
        guard version.fieldVersions.values.allSatisfy({ isValidClock($0) && $0 <= version.modifiedAt }) else {
            throw SharedSyncFormatError.invalidClock
        }
        return version
    }
}
