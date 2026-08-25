import Foundation
import AhoiCloudKitSpike
#if canImport(CryptoKit)
import CryptoKit
#endif
#if canImport(Security)
import Security
#endif

public struct RemoteControlProvisioningIdentity: Equatable, Sendable {
    public let sourceDeviceID: DeviceID
    public let publicKeyBase64: String
    public let fingerprint: String

    public init(sourceDeviceID: DeviceID, publicKey: Data) {
        self.sourceDeviceID = sourceDeviceID
        self.publicKeyBase64 = publicKey.base64EncodedString()
#if canImport(CryptoKit)
        self.fingerprint = SHA256.hash(data: publicKey)
            .prefix(12)
            .map { String(format: "%02x", $0) }
            .joined(separator: ":")
#else
        self.fingerprint = "unavailable"
#endif
    }
}

public protocol RemoteCommandSigning: Sendable {
    var sourceDeviceID: DeviceID { get }
    func makeNonce() throws -> Data
    func sign(_ payload: RemoteCommandPayload) throws -> SignedRemoteCommand
    func provisioningIdentity() throws -> RemoteControlProvisioningIdentity
}

public struct RemoteCommandKeyConfiguration: Equatable, Sendable {
    public let service: String
    public let account: String
    public let accessGroup: String?
    public let sourceDeviceID: DeviceID

    public init(
        service: String,
        account: String,
        accessGroup: String? = nil,
        sourceDeviceID: DeviceID
    ) {
        self.service = service
        self.account = account
        self.accessGroup = accessGroup
        self.sourceDeviceID = sourceDeviceID
    }

    public init(
        service: String,
        account: String,
        accessGroup: String? = nil,
        sourceDeviceUUID: UUID
    ) {
        self.init(
            service: service,
            account: account,
            accessGroup: accessGroup,
            sourceDeviceID: DeviceID(rawValue: sourceDeviceUUID)
        )
    }
}

public enum RemoteCommandSignerError: Error, Equatable, Sendable {
    case unavailable
    case unresolvedConfiguration
    case keychainStatus(Int32)
    case invalidPrivateKey
    case wrongSourceDevice
    case randomGenerationFailed(Int32)
}

#if canImport(CryptoKit) && canImport(Security)
/// Ed25519 signer backed only by externally provisioned Keychain bytes. It
/// never creates, derives, exports, or silently replaces a private key.
public struct KeychainRemoteCommandSigner: RemoteCommandSigning {
    public let sourceDeviceID: DeviceID

    private let configuration: RemoteCommandKeyConfiguration
    private let keyLoader: @Sendable () throws -> Data
    private let nonceLoader: @Sendable () throws -> Data

    public init(configuration: RemoteCommandKeyConfiguration) {
        self.configuration = configuration
        self.sourceDeviceID = configuration.sourceDeviceID
        self.keyLoader = {
            try Self.loadPrivateKey(configuration: configuration)
        }
        self.nonceLoader = { try Self.secureNonce() }
    }

    init(
        configuration: RemoteCommandKeyConfiguration,
        keyLoader: @escaping @Sendable () throws -> Data,
        nonceLoader: @escaping @Sendable () throws -> Data
    ) {
        self.configuration = configuration
        self.sourceDeviceID = configuration.sourceDeviceID
        self.keyLoader = keyLoader
        self.nonceLoader = nonceLoader
    }

    public func makeNonce() throws -> Data {
        let nonce = try nonceLoader()
        guard (16...64).contains(nonce.count) else {
            throw RemoteCommandSignerError.randomGenerationFailed(errSecParam)
        }
        return nonce
    }

    public func sign(_ payload: RemoteCommandPayload) throws -> SignedRemoteCommand {
        guard payload.sourceDeviceID == sourceDeviceID else {
            throw RemoteCommandSignerError.wrongSourceDevice
        }
        let privateKey = try signingKey()
        return SignedRemoteCommand(
            payload: payload,
            signature: try privateKey.signature(for: payload.canonicalData())
        )
    }

    public func provisioningIdentity() throws -> RemoteControlProvisioningIdentity {
        let publicKey = try signingKey().publicKey.rawRepresentation
        return .init(sourceDeviceID: sourceDeviceID, publicKey: publicKey)
    }

    private func signingKey() throws -> Curve25519.Signing.PrivateKey {
        let raw = try keyLoader()
        guard raw.count == 32,
              let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: raw) else {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
        return key
    }

    private static func loadPrivateKey(
        configuration: RemoteCommandKeyConfiguration
    ) throws -> Data {
        guard isResolved(configuration.service),
              isResolved(configuration.account),
              configuration.accessGroup.map(isResolved) ?? true else {
            throw RemoteCommandSignerError.unresolvedConfiguration
        }
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: configuration.service,
            kSecAttrAccount as String: configuration.account,
            kSecUseDataProtectionKeychain as String: kCFBooleanTrue as Any,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        if let accessGroup = configuration.accessGroup, !accessGroup.isEmpty {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess else {
            throw RemoteCommandSignerError.keychainStatus(status)
        }
        guard let data = result as? Data else {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
        return data
    }

    private static func secureNonce() throws -> Data {
        var bytes = [UInt8](repeating: 0, count: 32)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        guard status == errSecSuccess else {
            throw RemoteCommandSignerError.randomGenerationFailed(status)
        }
        return Data(bytes)
    }

    private static func isResolved(_ value: String) -> Bool {
        !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            && !value.contains("$(")
    }
}
#endif
