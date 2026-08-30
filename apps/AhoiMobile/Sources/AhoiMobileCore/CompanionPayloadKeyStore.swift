import Foundation
import Security

public enum CompanionPayloadKeyStoreError: Error, Equatable, Sendable {
    case invalidConfiguration
    case keychainStatus(OSStatus)
    case invalidKeyLength
    case corruptJournal
    case journalVersionMismatch
    case candidateMissing
    case receiptMismatch
    case splitKeyPrevented
    case randomGenerationFailed(OSStatus)
}

public enum CompanionSecureKeyGenerator {
    public static func aes256() throws -> Data {
        var bytes = [UInt8](repeating: 0, count: 32)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        guard status == errSecSuccess else {
            throw CompanionPayloadKeyStoreError.randomGenerationFailed(status)
        }
        return Data(bytes)
    }
}

public actor KeychainCompanionPayloadKeyStore: CompanionPayloadKeyLifecycleStoring {
    private struct Journal: Codable, Equatable, Sendable {
        let keyVersion: UInt32
        let origin: CompanionPendingKeyOrigin
        var acceptedReceipt: CompanionBootstrapClaimReceipt?
    }

    private enum ItemKind {
        case canonical
        case pending
        case journal
    }

    private let configuration: CompanionSyncKeyConfiguration
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(configuration: CompanionSyncKeyConfiguration) throws {
        guard !configuration.service.isEmpty,
              !configuration.account.isEmpty,
              configuration.keyVersion > 0 else {
            throw CompanionPayloadKeyStoreError.invalidConfiguration
        }
        self.configuration = configuration
        self.encoder = JSONEncoder()
        self.encoder.outputFormatting = [.sortedKeys]
        self.decoder = JSONDecoder()
    }

    public func hasCanonicalKey(version: UInt32) throws -> Bool {
        guard let key = try readItem(kind: .canonical, version: version) else {
            return false
        }
        guard key.count == 32 else {
            throw CompanionPayloadKeyStoreError.invalidKeyLength
        }
        return true
    }

    public func knownCanonicalVersions() throws -> Set<UInt32> {
        var query = baseQuery(account: nil)
        query[kSecAttrSynchronizable as String] = kSecAttrSynchronizableAny
        query[kSecReturnAttributes as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitAll
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecItemNotFound { return [] }
        guard status == errSecSuccess else {
            throw CompanionPayloadKeyStoreError.keychainStatus(status)
        }
        let attributes = (result as? [[String: Any]]) ?? []
        return Set(attributes.compactMap { entry in
            guard let account = entry[kSecAttrAccount as String] as? String else {
                return nil
            }
            return version(forCanonicalAccount: account)
        })
    }

    public func pendingState(
        version: UInt32
    ) throws -> CompanionPendingKeyState? {
        guard let journal = try readJournal(version: version) else {
            return nil
        }
        if journal.origin == .generated {
            guard let candidate = try readItem(kind: .pending, version: version) else {
                throw CompanionPayloadKeyStoreError.candidateMissing
            }
            guard candidate.count == 32 else {
                throw CompanionPayloadKeyStoreError.invalidKeyLength
            }
        }
        return .init(
            keyVersion: journal.keyVersion,
            origin: journal.origin,
            acceptedReceipt: journal.acceptedReceipt
        )
    }

    public func prepareCandidate(
        version: UInt32,
        generator: @escaping @Sendable () throws -> Data
    ) throws -> CompanionPendingKeyState {
        guard version > 0 else {
            throw CompanionPayloadKeyStoreError.invalidConfiguration
        }
        if let journal = try readJournal(version: version) {
            if journal.origin == .generated {
                guard let key = try readItem(kind: .pending, version: version) else {
                    throw CompanionPayloadKeyStoreError.candidateMissing
                }
                guard key.count == 32 else {
                    throw CompanionPayloadKeyStoreError.invalidKeyLength
                }
            }
            return state(journal)
        }
        if try hasCanonicalKey(version: version) {
            let journal = Journal(
                keyVersion: version,
                origin: .externallyProvisioned,
                acceptedReceipt: nil
            )
            try writeJournal(journal)
            return state(journal)
        }
        if let existingPending = try readItem(kind: .pending, version: version) {
            guard existingPending.count == 32 else {
                throw CompanionPayloadKeyStoreError.invalidKeyLength
            }
            let journal = Journal(
                keyVersion: version,
                origin: .generated,
                acceptedReceipt: nil
            )
            try writeJournal(journal)
            return state(journal)
        }

        let generated = try generator()
        guard generated.count == 32 else {
            throw CompanionPayloadKeyStoreError.invalidKeyLength
        }
        try addItem(generated, kind: .pending, version: version)
        let readback = try readRequiredItem(kind: .pending, version: version)
        guard constantTimeEqual(generated, readback) else {
            throw CompanionPayloadKeyStoreError.splitKeyPrevented
        }
        let journal = Journal(
            keyVersion: version,
            origin: .generated,
            acceptedReceipt: nil
        )
        try writeJournal(journal)
        return state(journal)
    }

    public func markClaimAccepted(
        _ receipt: CompanionBootstrapClaimReceipt
    ) throws {
        guard var journal = try readJournal(version: receipt.keyVersion) else {
            throw CompanionPayloadKeyStoreError.candidateMissing
        }
        guard journal.keyVersion == receipt.keyVersion else {
            throw CompanionPayloadKeyStoreError.journalVersionMismatch
        }
        journal.acceptedReceipt = receipt
        try writeJournal(journal)
        guard try readJournal(version: receipt.keyVersion)?.acceptedReceipt == receipt else {
            throw CompanionPayloadKeyStoreError.receiptMismatch
        }
    }

    public func promoteAcceptedCandidate(
        version: UInt32,
        matching claim: CompanionBootstrapClaim
    ) throws {
        guard let journal = try readJournal(version: version) else {
            throw CompanionPayloadKeyStoreError.candidateMissing
        }
        guard let receipt = journal.acceptedReceipt,
              receipt.matches(claim) else {
            throw CompanionPayloadKeyStoreError.receiptMismatch
        }
        if journal.origin == .externallyProvisioned {
            guard try hasCanonicalKey(version: version) else {
                throw CompanionPayloadKeyStoreError.candidateMissing
            }
            try deleteItem(kind: .journal, version: version)
            return
        }

        let pending = try readRequiredItem(kind: .pending, version: version)
        guard pending.count == 32 else {
            throw CompanionPayloadKeyStoreError.invalidKeyLength
        }
        do {
            try addItem(pending, kind: .canonical, version: version)
        } catch CompanionPayloadKeyStoreError.keychainStatus(errSecDuplicateItem) {
            let existing = try readRequiredItem(kind: .canonical, version: version)
            guard constantTimeEqual(existing, pending) else {
                throw CompanionPayloadKeyStoreError.splitKeyPrevented
            }
        }
        let readback = try readRequiredItem(kind: .canonical, version: version)
        guard constantTimeEqual(readback, pending) else {
            throw CompanionPayloadKeyStoreError.splitKeyPrevented
        }
        try deleteItem(kind: .pending, version: version)
        try deleteItem(kind: .journal, version: version)
    }

    public func discardGeneratedCandidate(version: UInt32) throws {
        guard let journal = try readJournal(version: version) else { return }
        if journal.origin == .generated {
            try deleteItem(kind: .pending, version: version)
        }
        try deleteItem(kind: .journal, version: version)
    }

    private func state(_ journal: Journal) -> CompanionPendingKeyState {
        .init(
            keyVersion: journal.keyVersion,
            origin: journal.origin,
            acceptedReceipt: journal.acceptedReceipt
        )
    }

    private func readJournal(version: UInt32) throws -> Journal? {
        guard let data = try readItem(kind: .journal, version: version) else {
            return nil
        }
        do {
            let journal = try decoder.decode(Journal.self, from: data)
            guard journal.keyVersion == version else {
                throw CompanionPayloadKeyStoreError.journalVersionMismatch
            }
            return journal
        } catch let error as CompanionPayloadKeyStoreError {
            throw error
        } catch {
            throw CompanionPayloadKeyStoreError.corruptJournal
        }
    }

    private func writeJournal(_ journal: Journal) throws {
        let data: Data
        do {
            data = try encoder.encode(journal)
        } catch {
            throw CompanionPayloadKeyStoreError.corruptJournal
        }
        let query = itemQuery(kind: .journal, version: journal.keyVersion)
        let attributes = [kSecValueData as String: data]
        let status = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if status == errSecItemNotFound {
            try addItem(data, kind: .journal, version: journal.keyVersion)
            return
        }
        guard status == errSecSuccess else {
            throw CompanionPayloadKeyStoreError.keychainStatus(status)
        }
    }

    private func readRequiredItem(kind: ItemKind, version: UInt32) throws -> Data {
        guard let data = try readItem(kind: kind, version: version) else {
            throw CompanionPayloadKeyStoreError.candidateMissing
        }
        return data
    }

    private func readItem(kind: ItemKind, version: UInt32) throws -> Data? {
        var query = itemQuery(kind: kind, version: version)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else {
            throw CompanionPayloadKeyStoreError.keychainStatus(status)
        }
        guard let data = result as? Data else {
            throw CompanionPayloadKeyStoreError.invalidKeyLength
        }
        return data
    }

    private func addItem(_ data: Data, kind: ItemKind, version: UInt32) throws {
        var query = itemQuery(kind: kind, version: version)
        query[kSecValueData as String] = data
        switch kind {
        case .canonical:
            query[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
        case .pending, .journal:
            query[kSecAttrAccessible as String] =
                kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        }
        let status = SecItemAdd(query as CFDictionary, nil)
        guard status == errSecSuccess else {
            throw CompanionPayloadKeyStoreError.keychainStatus(status)
        }
    }

    private func deleteItem(kind: ItemKind, version: UInt32) throws {
        let status = SecItemDelete(
            itemQuery(kind: kind, version: version) as CFDictionary
        )
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw CompanionPayloadKeyStoreError.keychainStatus(status)
        }
    }

    private func itemQuery(kind: ItemKind, version: UInt32) -> [String: Any] {
        var query = baseQuery(account: account(kind: kind, version: version))
        query[kSecAttrSynchronizable as String] = kind == .canonical
            ? kCFBooleanTrue
            : kCFBooleanFalse
        return query
    }

    private func baseQuery(account: String?) -> [String: Any] {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: configuration.service,
            kSecUseDataProtectionKeychain as String: kCFBooleanTrue as Any,
        ]
        if let account { query[kSecAttrAccount as String] = account }
        if let accessGroup = configuration.accessGroup, !accessGroup.isEmpty {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        return query
    }

    private func account(kind: ItemKind, version: UInt32) -> String {
        switch kind {
        case .canonical:
            version == configuration.keyVersion
                ? configuration.account
                : "\(configuration.account).v\(version)"
        case .pending:
            "\(configuration.account).bootstrap-pending.v\(version)"
        case .journal:
            "\(configuration.account).bootstrap-journal.v\(version)"
        }
    }

    private func version(forCanonicalAccount account: String) -> UInt32? {
        if account == configuration.account { return configuration.keyVersion }
        let prefix = "\(configuration.account).v"
        guard account.hasPrefix(prefix) else { return nil }
        return UInt32(account.dropFirst(prefix.count))
    }

    private func constantTimeEqual(_ lhs: Data, _ rhs: Data) -> Bool {
        guard lhs.count == rhs.count else { return false }
        var difference: UInt8 = 0
        for (left, right) in zip(lhs, rhs) { difference |= left ^ right }
        return difference == 0
    }
}
