import Foundation

#if canImport(CloudKit)
import CloudKit

public struct AppleCloudKitConfiguration: Hashable, Sendable {
    public let containerIdentifier: String
    public let zoneName: String

    public init(
        containerIdentifier: String,
        zoneName: String = "AhoiBrowserSyncZone"
    ) {
        self.containerIdentifier = containerIdentifier
        self.zoneName = zoneName
    }
}

public enum AppleCloudKitAdapterError: Error, Equatable {
    case invalidContainerIdentifier
    case invalidZoneName
}

/// A compile-checked private-database/custom-zone adapter skeleton. Its codec
/// stores only the opaque value through CKRecord.encryptedValues; queryable
/// conflict metadata remains in ordinary CKRecord fields. Calling it
/// requires a real iCloud container entitlement and provisioned bundle. Unit
/// tests deliberately use a fake transport and make no network claim.
public final class AppleCloudKitAdapter: CloudRecordTransport, @unchecked Sendable {
    private let database: CKDatabase
    private let zoneID: CKRecordZone.ID
    private let codec = AppleCloudKitRecordCodec()

    public init(configuration: AppleCloudKitConfiguration) throws {
        guard configuration.containerIdentifier.hasPrefix("iCloud."),
              configuration.containerIdentifier.count > "iCloud.".count else {
            throw AppleCloudKitAdapterError.invalidContainerIdentifier
        }
        guard !configuration.zoneName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw AppleCloudKitAdapterError.invalidZoneName
        }

        let container = CKContainer(identifier: configuration.containerIdentifier)
        self.database = container.privateCloudDatabase
        self.zoneID = CKRecordZone.ID(
            zoneName: configuration.zoneName,
            ownerName: CKCurrentUserDefaultName
        )
    }

    public func ensureCustomZone() async throws {
        _ = try await database.save(CKRecordZone(zoneID: zoneID))
    }

    public func save(_ records: [SyncRecord]) async throws {
        for value in records {
            let record = try codec.encode(value, zoneID: zoneID)
            _ = try await database.save(record)
        }
    }

    public func fetch(recordIDs: [UUID]) async throws -> [SyncRecord] {
        var result: [SyncRecord] = []
        result.reserveCapacity(recordIDs.count)

        for identifier in recordIDs {
            let cloudID = CKRecord.ID(
                recordName: identifier.uuidString,
                zoneID: zoneID
            )
            let record = try await database.record(for: cloudID)
            result.append(try codec.decode(record))
        }
        return result
    }
}
#endif
