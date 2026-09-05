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
        schemaVersion: UInt32 = 2,
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
}
