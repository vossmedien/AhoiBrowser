import CryptoKit
import Foundation
import Security
import AhoiCloudKitSpike

public struct CompanionSyncKeyConfiguration: Hashable, Sendable {
    public let service: String
    public let account: String
    public let accessGroup: String?
    public let keyVersion: UInt32

    public init(
        service: String,
        account: String,
        accessGroup: String? = nil,
        keyVersion: UInt32
    ) {
        self.service = service
        self.account = account
        self.accessGroup = accessGroup
        self.keyVersion = keyVersion
    }

    /// Resolves one stable key family to the account convention used by
    /// `KeychainCompanionPayloadKeyStore`. Keep the original bootstrap
    /// configuration as the family anchor across later rotations.
    public func canonicalConfiguration(
        for keyVersion: UInt32
    ) -> CompanionSyncKeyConfiguration {
        CompanionSyncKeyConfiguration(
            service: service,
            account: keyVersion == self.keyVersion
                ? account
                : "\(account).v\(keyVersion)",
            accessGroup: accessGroup,
            keyVersion: keyVersion
        )
    }
}

public enum CompanionSyncKeyError: Error, Equatable, Sendable {
    case invalidConfiguration
    case keyUnavailable(OSStatus)
    case invalidKeyLength
    case invalidCiphertext
    case unsupportedKeyVersion(UInt32)
}

/// Reads lifecycle-approved 256-bit keys from the synchronizable data-
/// protection Keychain and delegates AES-GCM to CryptoKit. Sealing always uses
/// the primary version. Explicit rotation windows may supply older read-only
/// configurations; revoked or unknown versions fail closed.
public struct KeychainCompanionPayloadSealer: CompanionPayloadSealer {
    private let primaryConfiguration: CompanionSyncKeyConfiguration
    private let configurations: [UInt32: CompanionSyncKeyConfiguration]
    private let keyLoader: @Sendable (CompanionSyncKeyConfiguration) throws -> Data

    public init(
        configuration: CompanionSyncKeyConfiguration,
        acceptedPreviousConfigurations: [CompanionSyncKeyConfiguration] = []
    ) throws {
        let allConfigurations = [configuration] + acceptedPreviousConfigurations
        var byVersion: [UInt32: CompanionSyncKeyConfiguration] = [:]
        for candidate in allConfigurations {
            guard !candidate.service.isEmpty,
                  !candidate.account.isEmpty,
                  candidate.keyVersion > 0,
                  byVersion[candidate.keyVersion] == nil else {
                throw CompanionSyncKeyError.invalidConfiguration
            }
            byVersion[candidate.keyVersion] = candidate
        }
        self.primaryConfiguration = configuration
        self.configurations = byVersion
        self.keyLoader = { candidate in
            try Self.loadKey(configuration: candidate)
        }
    }

    init(
        configuration: CompanionSyncKeyConfiguration,
        keyLoader: @escaping @Sendable () throws -> Data
    ) {
        self.primaryConfiguration = configuration
        self.configurations = [configuration.keyVersion: configuration]
        self.keyLoader = { _ in try keyLoader() }
    }

    init(
        configuration: CompanionSyncKeyConfiguration,
        acceptedPreviousConfigurations: [CompanionSyncKeyConfiguration],
        keyLoader: @escaping @Sendable (CompanionSyncKeyConfiguration) throws -> Data
    ) {
        self.primaryConfiguration = configuration
        self.configurations = Dictionary(
            uniqueKeysWithValues: ([configuration] + acceptedPreviousConfigurations).map {
                ($0.keyVersion, $0)
            }
        )
        self.keyLoader = keyLoader
    }

    public func seal(_ plaintext: Data) throws -> EncryptedValue {
        let keyData = try keyLoader(primaryConfiguration)
        guard keyData.count == 32 else { throw CompanionSyncKeyError.invalidKeyLength }
        let sealed = try AES.GCM.seal(plaintext, using: SymmetricKey(data: keyData))
        return .init(
            keyVersion: primaryConfiguration.keyVersion,
            nonce: sealed.nonce.withUnsafeBytes { Data($0) },
            ciphertextAndTag: sealed.ciphertext + sealed.tag
        )
    }

    public func open(_ value: EncryptedValue) throws -> Data {
        guard value.algorithm == .aes256GCM,
              value.nonce.count == 12,
              value.ciphertextAndTag.count >= 16 else {
            throw CompanionSyncKeyError.invalidCiphertext
        }
        guard let configuration = configurations[value.keyVersion] else {
            throw CompanionSyncKeyError.unsupportedKeyVersion(value.keyVersion)
        }
        let keyData = try keyLoader(configuration)
        guard keyData.count == 32 else { throw CompanionSyncKeyError.invalidKeyLength }
        let tagOffset = value.ciphertextAndTag.count - 16
        let box = try AES.GCM.SealedBox(
            nonce: AES.GCM.Nonce(data: value.nonce),
            ciphertext: value.ciphertextAndTag.prefix(tagOffset),
            tag: value.ciphertextAndTag.suffix(16)
        )
        return try AES.GCM.open(box, using: SymmetricKey(data: keyData))
    }

    private static func loadKey(
        configuration: CompanionSyncKeyConfiguration
    ) throws -> Data {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: configuration.service,
            kSecAttrAccount as String: configuration.account,
            kSecAttrSynchronizable as String: kCFBooleanTrue as Any,
            kSecUseDataProtectionKeychain as String: kCFBooleanTrue as Any,
            kSecReturnData as String: kCFBooleanTrue as Any,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        if let accessGroup = configuration.accessGroup, !accessGroup.isEmpty {
            query[kSecAttrAccessGroup as String] = accessGroup
        }

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else {
            throw CompanionSyncKeyError.keyUnavailable(status)
        }
        guard data.count == 32 else { throw CompanionSyncKeyError.invalidKeyLength }
        return data
    }
}

/// A narrowly scoped dual-read/single-write adapter for one rotation window.
/// Old envelopes are authenticated with the previous key and then sealed with
/// the next key. Already-rotated envelopes are authenticated and returned byte
/// for byte so crash recovery is idempotent.
public struct KeychainCompanionKeyRotationSealer:
    CompanionKeyRotationSealing {
    private let currentVersion: UInt32
    private let nextVersion: UInt32
    private let sealer: KeychainCompanionPayloadSealer

    public init(
        familyAnchorConfiguration: CompanionSyncKeyConfiguration,
        currentVersion: UInt32,
        nextVersion: UInt32
    ) throws {
        guard currentVersion > 0,
              nextVersion > 0,
              nextVersion > currentVersion else {
            throw CompanionKeyRotationError.invalidVersions
        }
        let current = familyAnchorConfiguration.canonicalConfiguration(
            for: currentVersion
        )
        let next = familyAnchorConfiguration.canonicalConfiguration(
            for: nextVersion
        )
        self.currentVersion = currentVersion
        self.nextVersion = nextVersion
        self.sealer = try KeychainCompanionPayloadSealer(
            configuration: next,
            acceptedPreviousConfigurations: [current]
        )
    }

    init(
        familyAnchorConfiguration: CompanionSyncKeyConfiguration,
        currentVersion: UInt32,
        nextVersion: UInt32,
        keyLoader: @escaping @Sendable (CompanionSyncKeyConfiguration) throws -> Data
    ) {
        let current = familyAnchorConfiguration.canonicalConfiguration(
            for: currentVersion
        )
        let next = familyAnchorConfiguration.canonicalConfiguration(
            for: nextVersion
        )
        self.currentVersion = currentVersion
        self.nextVersion = nextVersion
        self.sealer = KeychainCompanionPayloadSealer(
            configuration: next,
            acceptedPreviousConfigurations: [current],
            keyLoader: keyLoader
        )
    }

    public func reseal(
        _ value: EncryptedValue,
        currentVersion: UInt32,
        nextVersion: UInt32
    ) throws -> EncryptedValue {
        guard currentVersion == self.currentVersion,
              nextVersion == self.nextVersion,
              value.keyVersion == currentVersion ||
                value.keyVersion == nextVersion else {
            throw CompanionSyncKeyError.unsupportedKeyVersion(value.keyVersion)
        }
        let plaintext = try sealer.open(value)
        if value.keyVersion == nextVersion {
            return value
        }
        let replacement = try sealer.seal(plaintext)
        guard replacement.keyVersion == nextVersion else {
            throw CompanionSyncKeyError.unsupportedKeyVersion(
                replacement.keyVersion
            )
        }
        return replacement
    }
}
