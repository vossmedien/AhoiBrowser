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
    func identityIsRevoked() throws -> Bool
    @discardableResult
    func deleteIdentity() throws -> RemoteControlProvisioningIdentity
    func rotateIdentity() throws -> RemoteControlProvisioningIdentity
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
    case privateKeyReadbackMismatch
    case identityRevoked
}

#if canImport(CryptoKit) && canImport(Security)
private final class RemoteCommandSignerOperationGate: @unchecked Sendable {
    private let lock = NSLock()

    func withLock<Result>(_ operation: () throws -> Result) rethrows -> Result {
        lock.lock()
        defer { lock.unlock() }
        return try operation()
    }
}

/// Per-device Ed25519 signer. The private key is created at most once, remains
/// non-synchronizable and ThisDeviceOnly, and is never exported by the public
/// API. Only the public provisioning identity leaves this boundary.
public struct KeychainRemoteCommandSigner: RemoteCommandSigning {
    public let sourceDeviceID: DeviceID

    private let configuration: RemoteCommandKeyConfiguration
    private let keyLoader: @Sendable () throws -> Data
    private let keyCreator: @Sendable () throws -> Data
    private let revocationLoader: @Sendable () throws -> Data?
    private let keyRevoker: @Sendable () throws -> Data
    private let keyRotator: @Sendable () throws -> Data
    private let nonceLoader: @Sendable () throws -> Data
    private let operationGate = RemoteCommandSignerOperationGate()

    public init(configuration: RemoteCommandKeyConfiguration) {
        self.configuration = configuration
        self.sourceDeviceID = configuration.sourceDeviceID
        self.keyLoader = {
            try Self.loadPrivateKey(configuration: configuration)
        }
        self.keyCreator = {
            try Self.createPrivateKey(configuration: configuration)
        }
        self.revocationLoader = {
            try Self.loadRevokedPublicKey(configuration: configuration)
        }
        self.keyRevoker = {
            try Self.revokeAndDeletePrivateKey(configuration: configuration)
        }
        self.keyRotator = {
            try Self.rotatePrivateKey(configuration: configuration)
        }
        self.nonceLoader = { try Self.secureNonce() }
    }

    init(
        configuration: RemoteCommandKeyConfiguration,
        keyLoader: @escaping @Sendable () throws -> Data,
        keyCreator: (@Sendable () throws -> Data)? = nil,
        revocationLoader: @escaping @Sendable () throws -> Data? = { nil },
        keyRevoker: (@Sendable () throws -> Data)? = nil,
        keyRotator: (@Sendable () throws -> Data)? = nil,
        nonceLoader: @escaping @Sendable () throws -> Data
    ) {
        self.configuration = configuration
        self.sourceDeviceID = configuration.sourceDeviceID
        self.keyLoader = keyLoader
        self.keyCreator = keyCreator ?? keyLoader
        self.revocationLoader = revocationLoader
        self.keyRevoker = keyRevoker ?? {
            let raw = try keyLoader()
            return try Self.publicKeyBytes(fromPrivateKey: raw)
        }
        self.keyRotator = keyRotator ?? keyCreator ?? keyLoader
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
        return try operationGate.withLock {
            let privateKey = try signingKey()
            return SignedRemoteCommand(
                payload: payload,
                signature: try privateKey.signature(for: payload.canonicalData())
            )
        }
    }

    public func provisioningIdentity() throws -> RemoteControlProvisioningIdentity {
        try operationGate.withLock {
            guard try revocationLoader() == nil else {
                throw RemoteCommandSignerError.identityRevoked
            }
            return try activeProvisioningIdentity()
        }
    }

    /// Runtime bootstrap creates a key only for a never-enrolled identity. A
    /// persisted revocation marker always wins, so a restart cannot make an
    /// explicitly deleted identity appear active again. Callers that treat
    /// remote control as optional must catch `identityRevoked` and continue
    /// without a signer; only `rotateIdentity()` may re-enrol this device.
    @discardableResult
    public func ensureIdentity() throws -> RemoteControlProvisioningIdentity {
        try operationGate.withLock {
            guard try revocationLoader() == nil else {
                throw RemoteCommandSignerError.identityRevoked
            }
            return try activeProvisioningIdentity()
        }
    }

    public func identityIsRevoked() throws -> Bool {
        try operationGate.withLock { try revocationLoader() != nil }
    }

    @discardableResult
    public func deleteIdentity() throws -> RemoteControlProvisioningIdentity {
        try operationGate.withLock {
            let archivedPublicKey = try keyRevoker()
            guard archivedPublicKey.count == 32 else {
                throw RemoteCommandSignerError.invalidPrivateKey
            }
            return .init(
                sourceDeviceID: sourceDeviceID,
                publicKey: archivedPublicKey
            )
        }
    }

    public func rotateIdentity() throws -> RemoteControlProvisioningIdentity {
        try operationGate.withLock {
            let raw = try keyRotator()
            let publicKey = try Self.publicKeyBytes(fromPrivateKey: raw)
            return .init(sourceDeviceID: sourceDeviceID, publicKey: publicKey)
        }
    }

    private func signingKey() throws -> Curve25519.Signing.PrivateKey {
        guard try revocationLoader() == nil else {
            throw RemoteCommandSignerError.identityRevoked
        }
        let raw: Data
        do {
            raw = try keyLoader()
        } catch RemoteCommandSignerError.keychainStatus(errSecItemNotFound) {
            raw = try keyCreator()
        }
        guard raw.count == 32 else {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
        do {
            return try Curve25519.Signing.PrivateKey(rawRepresentation: raw)
        } catch {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
    }

    private func activeProvisioningIdentity() throws -> RemoteControlProvisioningIdentity {
        let publicKey = try signingKey().publicKey.rawRepresentation
        return .init(sourceDeviceID: sourceDeviceID, publicKey: publicKey)
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
            kSecAttrSynchronizable as String: kCFBooleanFalse as Any,
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

    private static func createPrivateKey(
        configuration: RemoteCommandKeyConfiguration
    ) throws -> Data {
        guard isResolved(configuration.service),
              isResolved(configuration.account),
              configuration.accessGroup.map(isResolved) ?? true else {
            throw RemoteCommandSignerError.unresolvedConfiguration
        }
        let generated = Curve25519.Signing.PrivateKey().rawRepresentation
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: configuration.service,
            kSecAttrAccount as String: configuration.account,
            kSecAttrSynchronizable as String: kCFBooleanFalse as Any,
            kSecAttrAccessible as String:
                kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
            kSecUseDataProtectionKeychain as String: kCFBooleanTrue as Any,
            kSecValueData as String: generated,
        ]
        if let accessGroup = configuration.accessGroup, !accessGroup.isEmpty {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        let status = SecItemAdd(query as CFDictionary, nil)
        if status == errSecDuplicateItem {
            return try loadPrivateKey(configuration: configuration)
        }
        guard status == errSecSuccess else {
            throw RemoteCommandSignerError.keychainStatus(status)
        }
        let readback = try loadPrivateKey(configuration: configuration)
        guard readback == generated else {
            throw RemoteCommandSignerError.privateKeyReadbackMismatch
        }
        return readback
    }

    private static func loadRevokedPublicKey(
        configuration: RemoteCommandKeyConfiguration
    ) throws -> Data? {
        var query = revocationQuery(configuration: configuration)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else {
            throw RemoteCommandSignerError.keychainStatus(status)
        }
        guard let data = result as? Data, data.count == 32 else {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
        return data
    }

    private static func revokeAndDeletePrivateKey(
        configuration: RemoteCommandKeyConfiguration
    ) throws -> Data {
        if let archived = try loadRevokedPublicKey(configuration: configuration) {
            try deletePrivateKeyIfPresent(configuration: configuration)
            return archived
        }
        let publicKey: Data
        do {
            publicKey = try publicKeyBytes(fromPrivateKey: loadPrivateKey(
                configuration: configuration
            ))
        } catch RemoteCommandSignerError.keychainStatus(errSecItemNotFound) {
            publicKey = Curve25519.Signing.PrivateKey().publicKey.rawRepresentation
        }
        try storeRevokedPublicKey(publicKey, configuration: configuration)
        do {
            try deletePrivateKeyIfPresent(configuration: configuration)
        } catch {
            // The marker was committed first, so any surviving private bytes
            // remain unusable through this signer until explicit rotation.
            throw error
        }
        guard try loadRevokedPublicKey(configuration: configuration) == publicKey else {
            throw RemoteCommandSignerError.privateKeyReadbackMismatch
        }
        return publicKey
    }

    private static func rotatePrivateKey(
        configuration: RemoteCommandKeyConfiguration
    ) throws -> Data {
        let generated = Curve25519.Signing.PrivateKey().rawRepresentation
        let fallbackPublicKey: Data
        do {
            fallbackPublicKey = try publicKeyBytes(fromPrivateKey: loadPrivateKey(
                configuration: configuration
            ))
        } catch RemoteCommandSignerError.keychainStatus(errSecItemNotFound) {
            fallbackPublicKey = try publicKeyBytes(fromPrivateKey: generated)
        }
        try storeRevokedPublicKey(fallbackPublicKey, configuration: configuration)
        try replacePrivateKey(generated, configuration: configuration)
        let readback = try loadPrivateKey(configuration: configuration)
        guard readback == generated else {
            throw RemoteCommandSignerError.privateKeyReadbackMismatch
        }
        try clearRevocationMarker(configuration: configuration)
        return readback
    }

    private static func replacePrivateKey(
        _ raw: Data,
        configuration: RemoteCommandKeyConfiguration
    ) throws {
        let baseQuery = privateKeyQuery(configuration: configuration)
        let updateStatus = SecItemUpdate(
            baseQuery as CFDictionary,
            [kSecValueData as String: raw] as CFDictionary
        )
        if updateStatus == errSecItemNotFound {
            var addQuery = baseQuery
            addQuery[kSecAttrAccessible as String] =
                kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            addQuery[kSecValueData as String] = raw
            let addStatus = SecItemAdd(addQuery as CFDictionary, nil)
            guard addStatus == errSecSuccess else {
                throw RemoteCommandSignerError.keychainStatus(addStatus)
            }
            return
        }
        guard updateStatus == errSecSuccess else {
            throw RemoteCommandSignerError.keychainStatus(updateStatus)
        }
    }

    private static func deletePrivateKeyIfPresent(
        configuration: RemoteCommandKeyConfiguration
    ) throws {
        let status = SecItemDelete(
            privateKeyQuery(configuration: configuration) as CFDictionary
        )
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw RemoteCommandSignerError.keychainStatus(status)
        }
    }

    private static func storeRevokedPublicKey(
        _ publicKey: Data,
        configuration: RemoteCommandKeyConfiguration
    ) throws {
        guard publicKey.count == 32 else {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
        var query = revocationQuery(configuration: configuration)
        query[kSecAttrAccessible as String] =
            kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        query[kSecValueData as String] = publicKey
        let status = SecItemAdd(query as CFDictionary, nil)
        if status == errSecDuplicateItem {
            let updateStatus = SecItemUpdate(
                revocationQuery(configuration: configuration) as CFDictionary,
                [kSecValueData as String: publicKey] as CFDictionary
            )
            guard updateStatus == errSecSuccess else {
                throw RemoteCommandSignerError.keychainStatus(updateStatus)
            }
            return
        }
        guard status == errSecSuccess else {
            throw RemoteCommandSignerError.keychainStatus(status)
        }
    }

    private static func clearRevocationMarker(
        configuration: RemoteCommandKeyConfiguration
    ) throws {
        let status = SecItemDelete(
            revocationQuery(configuration: configuration) as CFDictionary
        )
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw RemoteCommandSignerError.keychainStatus(status)
        }
    }

    private static func privateKeyQuery(
        configuration: RemoteCommandKeyConfiguration
    ) -> [String: Any] {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: configuration.service,
            kSecAttrAccount as String: configuration.account,
            kSecAttrSynchronizable as String: kCFBooleanFalse as Any,
            kSecUseDataProtectionKeychain as String: kCFBooleanTrue as Any,
        ]
        if let accessGroup = configuration.accessGroup, !accessGroup.isEmpty {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        return query
    }

    private static func revocationQuery(
        configuration: RemoteCommandKeyConfiguration
    ) -> [String: Any] {
        var query = privateKeyQuery(configuration: configuration)
        query[kSecAttrAccount as String] = configuration.account + ".revoked"
        return query
    }

    private static func publicKeyBytes(fromPrivateKey raw: Data) throws -> Data {
        guard raw.count == 32 else {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
        do {
            return try Curve25519.Signing.PrivateKey(rawRepresentation: raw)
                .publicKey.rawRepresentation
        } catch {
            throw RemoteCommandSignerError.invalidPrivateKey
        }
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
