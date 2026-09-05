import Foundation
import AhoiCloudKitSpike

enum SharedTabFieldReadMergeError: Error, Equatable {
    case invalidVersion
    case missingFieldClock
    case invalidLegacyField
}

/// Preparation for reading mixed-version state, not a v3 write authorization.
/// Legacy absence never receives the enclosing record's later write clock.
enum SharedTabFieldReadMerge {
    static func field<Value: Equatable>(
        _ name: String, existing: Value, incoming: Value, legacyDefault: Value,
        oldVersion: SyncVersion, newVersion: SyncVersion
    ) throws -> (value: Value, clock: HybridLogicalClock?) {
        let old = try explicitClock(name, value: existing, legacyDefault: legacyDefault, version: oldVersion)
        let new = try explicitClock(name, value: incoming, legacyDefault: legacyDefault, version: newVersion)
        switch (old, new) {
        case (nil, nil): return (legacyDefault, nil)
        case (.some(let old), nil): return (existing, old)
        case (nil, .some(let new)): return (incoming, new)
        case (.some(let old), .some(let new)):
            if old == new && existing != incoming {
                throw CompanionFieldMergeError.equalClockConflict(name)
            }
            return new > old ? (incoming, new) : (existing, old)
        }
    }

    static func version(
        base: SyncVersion, field: String, clock: HybridLogicalClock?
    ) -> SyncVersion {
        guard let clock else { return base }
        var fields = base.fieldVersions
        fields[field] = clock
        return SyncVersion(schemaVersion: 3, modifiedAt: base.modifiedAt,
                           modifiedBy: base.modifiedBy, fieldVersions: fields)
    }

    static func retainingExistingField(
        _ name: String, previous: SyncVersion, candidate: SyncVersion
    ) -> SyncVersion {
        guard previous.schemaVersion == 3, let clock = previous.fieldVersions[name],
              candidate.schemaVersion < 3 else { return candidate }
        let top = max(previous.modifiedAt, candidate.modifiedAt)
        var fields = candidate.fieldVersions
        fields[name] = clock
        return SyncVersion(schemaVersion: 3, modifiedAt: top,
                           modifiedBy: top.nodeID, fieldVersions: fields)
    }

    private static func explicitClock<Value: Equatable>(
        _ field: String, value: Value, legacyDefault: Value, version: SyncVersion
    ) throws -> HybridLogicalClock? {
        guard (1...3).contains(version.schemaVersion) else {
            throw SharedTabFieldReadMergeError.invalidVersion
        }
        if version.schemaVersion < 3 {
            guard value == legacyDefault, version.fieldVersions[field] == nil else {
                throw SharedTabFieldReadMergeError.invalidLegacyField
            }
            return nil
        }
        guard let clock = version.fieldVersions[field], clock <= version.modifiedAt else {
            throw SharedTabFieldReadMergeError.missingFieldClock
        }
        return clock
    }
}
