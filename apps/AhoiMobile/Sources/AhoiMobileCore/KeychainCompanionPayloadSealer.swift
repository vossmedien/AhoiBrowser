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
}

public enum CompanionSyncKeyError: Error, Equatable, Sendable {
    case invalidConfiguration
    case keyUnavailable(OSStatus)
    case invalidKeyLength
    case invalidCiphertext
}

/// Reads an externally provisioned 256-bit key from the synchronizable data-
/// protection Keychain and delegates AES-GCM to CryptoKit. It never creates,
/// derives, rotates, exports, or recovers a key; those lifecycle operations
/// remain an Apple-Team/product-security gate.
public struct KeychainCompanionPayloadSealer: CompanionPayloadSealer {
    private let configuration: CompanionSyncKeyConfiguration
    private let keyLoader: @Sendable () throws -> Data

    public init(configuration: CompanionSyncKeyConfiguration) throws {
        guard !configuration.service.isEmpty,
              !configuration.account.isEmpty,
              configuration.keyVersion > 0 else {
            throw CompanionSyncKeyError.invalidConfiguration
        }
        self.configuration = configuration
        self.keyLoader = {
            try Self.loadKey(configuration: configuration)
        }
    }

    init(
        configuration: CompanionSyncKeyConfiguration,
        keyLoader: @escaping @Sendable () throws -> Data
    ) {
        self.configuration = configuration
        self.keyLoader = keyLoader
    }

    public func seal(_ plaintext: Data) throws -> EncryptedValue {
        let keyData = try keyLoader()
        guard keyData.count == 32 else { throw CompanionSyncKeyError.invalidKeyLength }
        let sealed = try AES.GCM.seal(plaintext, using: SymmetricKey(data: keyData))
        return .init(
            keyVersion: configuration.keyVersion,
            nonce: sealed.nonce.withUnsafeBytes { Data($0) },
            ciphertextAndTag: sealed.ciphertext + sealed.tag
        )
    }

    public func open(_ value: EncryptedValue) throws -> Data {
        guard value.algorithm == .aes256GCM,
              value.keyVersion == configuration.keyVersion,
              value.nonce.count == 12,
              value.ciphertextAndTag.count >= 16 else {
            throw CompanionSyncKeyError.invalidCiphertext
        }
        let keyData = try keyLoader()
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
