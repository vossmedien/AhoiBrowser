import Foundation
import AhoiCloudKitSpike

/// Field-group merge for remote commands, appearance and permitted settings,
/// mirroring Chromium's `MergeRecordFields` (`sync_field_merge.cc`): each
/// group keeps the value of its newer clock, an equal clock with a different
/// value is rejected, an immutable group may not change, and a true union of
/// both inputs receives the successor of the newer record clock.
public enum CompanionProductFieldMerge {
    static let commandFields: Set<String> = ["request", "status", "tombstone"]

    /// `request` (the signed envelope) is immutable. `status` (status and
    /// result code) never regresses and is frozen once terminal, even against
    /// a newer clock; the kept group also keeps its clock.
    public static func merge(
        _ existing: RemoteCommandState,
        _ incoming: RemoteCommandState
    ) throws -> RemoteCommandState {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.envelope == incoming.envelope else {
            throw CompanionFieldMergeError.immutableFieldConflict("request")
        }
        let oldVersion = try SharedSyncFormat.validate(existing.version, fields: commandFields)
        let newVersion = try SharedSyncFormat.validate(incoming.version, fields: commandFields)
        var result = existing
        var version = CompanionFieldMerge.mergedVersion(oldVersion, newVersion, fields: commandFields)
        let oldStatus = CommandStatus(existing)
        let newStatus = CommandStatus(incoming)
        if try CompanionFieldMerge.incomingWins("status", oldStatus, newStatus, oldVersion, newVersion) {
            if isTerminal(existing.status) || incoming.status.rawValue < existing.status.rawValue {
                version = replacing("status", with: oldVersion, in: version)
            } else {
                result.status = incoming.status
                result.resultCode = incoming.resultCode
            }
        }
        // The Companion model has no command tombstone (the codec rejects a
        // deleted command), so the group is clock-checked for conflicts only.
        _ = try CompanionFieldMerge.incomingWins("tombstone", false, false, oldVersion, newVersion)
        func same(_ other: RemoteCommandState, _ otherVersion: SyncVersion) -> Bool {
            CommandStatus(result) == CommandStatus(other) &&
                version.fieldVersions == otherVersion.fieldVersions
        }
        if !same(existing, oldVersion), !same(incoming, newVersion) {
            version = try CompanionFieldMerge.dominatingMergeVersion(version)
        }
        result.version = version
        return result
    }

    public static func merge(
        _ existing: CompanionAppearanceRecord,
        _ incoming: CompanionAppearanceRecord
    ) throws -> CompanionAppearanceRecord {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        let fields = DesktopWirePayloadCodec.appearanceFields
        let oldVersion = try SharedSyncFormat.validate(existing.version, fields: fields)
        let newVersion = try SharedSyncFormat.validate(incoming.version, fields: fields)
        var colorMode = existing.colorMode
        var accent = existing.accentARGB
        var useSystemAccent = existing.useSystemAccent
        var tombstone = existing.tombstone
        if try CompanionFieldMerge.incomingWins(
            "color_mode", existing.colorMode, incoming.colorMode, oldVersion, newVersion
        ) {
            colorMode = incoming.colorMode
        }
        if try CompanionFieldMerge.incomingWins(
            "accent_argb", existing.accentARGB, incoming.accentARGB, oldVersion, newVersion
        ) {
            accent = incoming.accentARGB
        }
        if try CompanionFieldMerge.incomingWins(
            "use_system_accent", existing.useSystemAccent, incoming.useSystemAccent,
            oldVersion, newVersion
        ) {
            useSystemAccent = incoming.useSystemAccent
        }
        if try CompanionFieldMerge.incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            tombstone = incoming.tombstone
        }
        var version = CompanionFieldMerge.mergedVersion(oldVersion, newVersion, fields: fields)
        func same(_ other: CompanionAppearanceRecord, _ otherVersion: SyncVersion) -> Bool {
            colorMode == other.colorMode && accent == other.accentARGB &&
                useSystemAccent == other.useSystemAccent &&
                (tombstone != nil) == other.isDeleted &&
                version.fieldVersions == otherVersion.fieldVersions
        }
        if !same(existing, oldVersion), !same(incoming, newVersion) {
            version = try CompanionFieldMerge.dominatingMergeVersion(version)
        }
        // The record initializer enforces the accent/system-accent invariant,
        // so a union that would break it is rejected rather than stored.
        return try CompanionAppearanceRecord(
            id: existing.id,
            colorMode: colorMode,
            accentARGB: accent,
            useSystemAccent: useSystemAccent,
            version: version,
            tombstone: reframe(tombstone, id: existing.id, version: version)
        )
    }

    /// `setting_id` is immutable.
    public static func merge(
        _ existing: CompanionPermittedSettingRecord,
        _ incoming: CompanionPermittedSettingRecord
    ) throws -> CompanionPermittedSettingRecord {
        guard existing.id == incoming.id else { throw CompanionFieldMergeError.identityMismatch }
        guard existing.settingID == incoming.settingID else {
            throw CompanionFieldMergeError.immutableFieldConflict("setting_id")
        }
        let fields = DesktopWirePayloadCodec.permittedSettingFields
        let oldVersion = try SharedSyncFormat.validate(existing.version, fields: fields)
        let newVersion = try SharedSyncFormat.validate(incoming.version, fields: fields)
        var valueJSON = existing.valueJSON
        var tombstone = existing.tombstone
        if try CompanionFieldMerge.incomingWins(
            "value_json", existing.valueJSON, incoming.valueJSON, oldVersion, newVersion
        ) {
            valueJSON = incoming.valueJSON
        }
        if try CompanionFieldMerge.incomingWins(
            "tombstone", existing.isDeleted, incoming.isDeleted, oldVersion, newVersion
        ) {
            tombstone = incoming.tombstone
        }
        var version = CompanionFieldMerge.mergedVersion(oldVersion, newVersion, fields: fields)
        func same(_ other: CompanionPermittedSettingRecord, _ otherVersion: SyncVersion) -> Bool {
            valueJSON == other.valueJSON && (tombstone != nil) == other.isDeleted &&
                version.fieldVersions == otherVersion.fieldVersions
        }
        if !same(existing, oldVersion), !same(incoming, newVersion) {
            version = try CompanionFieldMerge.dominatingMergeVersion(version)
        }
        return try CompanionPermittedSettingRecord(
            id: existing.id,
            settingID: existing.settingID,
            valueJSON: valueJSON,
            version: version,
            tombstone: reframe(tombstone, id: existing.id, version: version)
        )
    }

    private struct CommandStatus: Equatable {
        let status: RemoteCommandStatus
        let resultCode: String
        init(_ value: RemoteCommandState) {
            status = value.status
            resultCode = value.resultCode
        }
    }

    private static func isTerminal(_ status: RemoteCommandStatus) -> Bool {
        status == .executed || status == .failed
    }

    private static func replacing(
        _ field: String,
        with source: SyncVersion,
        in version: SyncVersion
    ) -> SyncVersion {
        var fields = version.fieldVersions
        fields[field] = source.fieldVersions[field]
        return SyncVersion(
            schemaVersion: version.schemaVersion, modifiedAt: version.modifiedAt,
            modifiedBy: version.modifiedBy, fieldVersions: fields
        )
    }

    /// Envelope tombstone metadata follows the merged record clock, as the
    /// codec requires when the merged winner is re-published.
    private static func reframe(_ tombstone: Tombstone?, id: UUID, version: SyncVersion) -> Tombstone? {
        guard let tombstone else { return nil }
        if tombstone.deletedAt == version.modifiedAt, tombstone.deletedBy == version.modifiedBy {
            return tombstone
        }
        let (time, overflow) = version.modifiedAt.physicalMilliseconds.addingReportingOverflow(
            2_592_000_000
        )
        return Tombstone(
            entityID: id, deletedAt: version.modifiedAt, deletedBy: version.modifiedBy,
            originalParentID: nil, originalOrderKey: nil,
            purgeAfterMilliseconds: max(tombstone.purgeAfterMilliseconds, overflow ? UInt64.max : time)
        )
    }
}
