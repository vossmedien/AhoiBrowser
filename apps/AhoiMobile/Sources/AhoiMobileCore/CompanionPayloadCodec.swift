import Foundation
import AhoiCloudKitSpike

public enum CompanionPayloadSealerError: Error, Equatable, Sendable {
    case notConfigured
    case unsupportedFixtureOperation
}

/// Application-layer crypto is injected at the product boundary. The
/// companion core never invents a cipher, derives a key, or stores key
/// material. The resulting `EncryptedValue` is the only value accepted by the
/// CloudKit record codec.
public protocol CompanionPayloadSealer: Sendable {
    func seal(_ plaintext: Data) throws -> EncryptedValue
    func open(_ value: EncryptedValue) throws -> Data
}

public struct UnconfiguredCompanionPayloadSealer: CompanionPayloadSealer {
    public init() {}

    public func seal(_ plaintext: Data) throws -> EncryptedValue {
        _ = plaintext
        throw CompanionPayloadSealerError.notConfigured
    }

    public func open(_ value: EncryptedValue) throws -> Data {
        _ = value
        throw CompanionPayloadSealerError.notConfigured
    }
}

/// Test-only shape provider. Its bytes are deliberately synthetic and must
/// never receive user data or secrets; its purpose is to exercise boundary and
/// CKRecord encrypted-field plumbing without claiming cryptographic security.
public struct SyntheticCompanionPayloadSealer: CompanionPayloadSealer {
    public init() {}

    public func seal(_ plaintext: Data) throws -> EncryptedValue {
        _ = plaintext
        return .init(
            keyVersion: 1,
            nonce: Data(repeating: 0xA1, count: 12),
            ciphertextAndTag: Data(repeating: 0xB2, count: 32)
        )
    }

    public func open(_ value: EncryptedValue) throws -> Data {
        _ = value
        throw CompanionPayloadSealerError.unsupportedFixtureOperation
    }
}

/// Encodes a shared model to the provider-independent `SyncRecord` envelope.
/// URLs, titles, and other private payload fields are JSON-encoded first and
/// reach CloudKit only through `AppleCloudKitRecordCodec`'s encryptedValues
/// path after a real sealer is injected by the signed product target.
public struct CompanionPayloadCodec: Sendable {
    private let sealer: any CompanionPayloadSealer

    public init(sealer: any CompanionPayloadSealer) {
        self.sealer = sealer
    }

    public func makeRecord<Value: Encodable & Sendable>(
        recordID: UUID,
        entityID: UUID,
        dataClass: SyncDataClass,
        version: SyncVersion,
        value: Value,
        orderKey: OrderKey? = nil,
        tombstone: Tombstone? = nil
    ) throws -> SyncRecord {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        return try makeRecord(
            recordID: recordID,
            entityID: entityID,
            dataClass: dataClass,
            version: version,
            plaintext: encoder.encode(value),
            orderKey: orderKey,
            tombstone: tombstone
        )
    }

    public func makeRecord(
        recordID: UUID,
        entityID: UUID,
        dataClass: SyncDataClass,
        version: SyncVersion,
        plaintext: Data,
        orderKey: OrderKey? = nil,
        tombstone: Tombstone? = nil
    ) throws -> SyncRecord {
        return SyncRecord(
            recordID: recordID,
            entityID: entityID,
            schemaVersion: version.schemaVersion,
            dataClass: dataClass,
            modifiedAt: version.modifiedAt,
            originatingDevice: version.modifiedBy,
            orderKey: orderKey,
            encryptedValue: try sealer.seal(plaintext),
            tombstone: tombstone
        )
    }

    public func open<Value: Decodable & Sendable>(
        _ record: SyncRecord,
        as type: Value.Type
    ) throws -> Value {
        let plaintext = try openData(record)
        return try JSONDecoder().decode(type, from: plaintext)
    }

    public func openData(_ record: SyncRecord) throws -> Data {
        try sealer.open(record.encryptedValue)
    }
}
