import Foundation

public enum SyncDataClass: String, Codable, CaseIterable, Sendable {
    case workspace
    case treeNode
    case orderKey
    case tombstone
    case recoveryMetadata
    case device
    case deviceSession
    case deviceTab
    case history
    case historyVisit
    case remoteCommand
    case appearance
    case permittedSetting
    case extensionInventory
    case developerAsset
    case bookmark
    case deviceCapability

    case cookie
    case password
    case autofill
    case siteData
    case cache
    case permission
    case extensionStorage
    case incognito
    case keychainSecret
    case headerSecret
    case httpAuthSecret
}

/// Schema-only carrier for an opaque value produced by an approved crypto
/// provider. This spike implements no cipher, KDF, key storage, or recovery.
/// Tests use synthetic bytes and prove only transport-boundary validation.
public struct EncryptedValue: Codable, Hashable, Sendable {
    public enum Algorithm: String, Codable, Sendable {
        case aes256GCM = "AES-256-GCM"
    }

    public let algorithm: Algorithm
    public let keyVersion: UInt32
    public let nonce: Data
    public let ciphertextAndTag: Data

    public init(
        algorithm: Algorithm = .aes256GCM,
        keyVersion: UInt32,
        nonce: Data,
        ciphertextAndTag: Data
    ) {
        self.algorithm = algorithm
        self.keyVersion = keyVersion
        self.nonce = nonce
        self.ciphertextAndTag = ciphertextAndTag
    }
}

public struct Tombstone: Codable, Hashable, Sendable {
    public let entityID: UUID
    public let deletedAt: HybridLogicalClock
    public let deletedBy: DeviceID
    public let originalParentID: UUID?
    public let originalOrderKey: OrderKey?
    public let purgeAfterMilliseconds: UInt64

    public init(
        entityID: UUID,
        deletedAt: HybridLogicalClock,
        deletedBy: DeviceID,
        originalParentID: UUID?,
        originalOrderKey: OrderKey?,
        purgeAfterMilliseconds: UInt64
    ) {
        self.entityID = entityID
        self.deletedAt = deletedAt
        self.deletedBy = deletedBy
        self.originalParentID = originalParentID
        self.originalOrderKey = originalOrderKey
        self.purgeAfterMilliseconds = purgeAfterMilliseconds
    }
}

/// Metadata needed to surface a recoverable deletion. It intentionally carries
/// no plaintext page content or credential-like data.
public struct RecoveryMetadata: Codable, Hashable, Sendable {
    public let entityID: UUID
    public let tombstoneID: UUID
    public let recoveryParentID: UUID?
    public let recoveryOrderKey: OrderKey
    public let recoveredAt: HybridLogicalClock

    public init(
        entityID: UUID,
        tombstoneID: UUID,
        recoveryParentID: UUID?,
        recoveryOrderKey: OrderKey,
        recoveredAt: HybridLogicalClock
    ) {
        self.entityID = entityID
        self.tombstoneID = tombstoneID
        self.recoveryParentID = recoveryParentID
        self.recoveryOrderKey = recoveryOrderKey
        self.recoveredAt = recoveredAt
    }
}

public struct SyncRecord: Codable, Hashable, Sendable {
    public let recordID: UUID
    public let entityID: UUID
    public let schemaVersion: UInt32
    public let dataClass: SyncDataClass
    public let modifiedAt: HybridLogicalClock
    public let originatingDevice: DeviceID
    public let orderKey: OrderKey?
    public let encryptedValue: EncryptedValue
    public let tombstone: Tombstone?

    public init(
        recordID: UUID = UUID(),
        entityID: UUID,
        schemaVersion: UInt32 = SharedSyncFormat.currentVersion,
        dataClass: SyncDataClass,
        modifiedAt: HybridLogicalClock,
        originatingDevice: DeviceID,
        orderKey: OrderKey? = nil,
        encryptedValue: EncryptedValue,
        tombstone: Tombstone? = nil
    ) {
        self.recordID = recordID
        self.entityID = entityID
        self.schemaVersion = schemaVersion
        self.dataClass = dataClass
        self.modifiedAt = modifiedAt
        self.originatingDevice = originatingDevice
        self.orderKey = orderKey
        self.encryptedValue = encryptedValue
        self.tombstone = tombstone
    }

    private enum CodingKeys: String, CodingKey {
        case recordID, entityID, schemaVersion, dataClass, modifiedAt
        case originatingDevice, orderKey, encryptedValue, tombstone
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let schema = try container.decode(UInt32.self, forKey: .schemaVersion)
        guard schema == SharedSyncFormat.currentVersion else {
            throw DecodingError.dataCorruptedError(
                forKey: .schemaVersion, in: container,
                debugDescription: "Only the current shared sync format is supported."
            )
        }
        self.init(
            recordID: try container.decode(UUID.self, forKey: .recordID),
            entityID: try container.decode(UUID.self, forKey: .entityID),
            schemaVersion: schema,
            dataClass: try container.decode(SyncDataClass.self, forKey: .dataClass),
            modifiedAt: try container.decode(HybridLogicalClock.self, forKey: .modifiedAt),
            originatingDevice: try container.decode(DeviceID.self, forKey: .originatingDevice),
            orderKey: try container.decodeIfPresent(OrderKey.self, forKey: .orderKey),
            encryptedValue: try container.decode(EncryptedValue.self, forKey: .encryptedValue),
            tombstone: try container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }

    public func encode(to encoder: Encoder) throws {
        guard schemaVersion == SharedSyncFormat.currentVersion else {
            throw EncodingError.invalidValue(
                schemaVersion,
                .init(codingPath: encoder.codingPath,
                      debugDescription: "Only the current shared sync format can be encoded.")
            )
        }
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(recordID, forKey: .recordID)
        try container.encode(entityID, forKey: .entityID)
        try container.encode(schemaVersion, forKey: .schemaVersion)
        try container.encode(dataClass, forKey: .dataClass)
        try container.encode(modifiedAt, forKey: .modifiedAt)
        try container.encode(originatingDevice, forKey: .originatingDevice)
        try container.encodeIfPresent(orderKey, forKey: .orderKey)
        try container.encode(encryptedValue, forKey: .encryptedValue)
        try container.encodeIfPresent(tombstone, forKey: .tombstone)
    }
}
