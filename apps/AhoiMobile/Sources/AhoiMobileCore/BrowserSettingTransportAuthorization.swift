import Foundation
import AhoiCloudKitSpike

enum BrowserSettingTransportAuthorizationError: Error, Equatable, Sendable {
    case categoryNotApproved
    case invalidPayload
}

/// Local per-record consent. An off/on cycle cannot revive an old asynchronous
/// request; unapproved cached ciphertext is retained, never treated as corrupt.
final class BrowserSettingTransportAuthorization: @unchecked Sendable {
    final class Lease: @unchecked Sendable {
        private let lock = NSLock()
        private var revoked = false

        func revoke() { lock.withLock { revoked = true } }
        func validate() throws {
            guard !lock.withLock({ revoked }) else {
                throw BrowserSettingTransportAuthorizationError.categoryNotApproved
            }
        }
    }

    typealias Validator = @Sendable (SyncRecord) throws -> Void
    private let lock = NSLock()
    private var leases: [UUID: Lease] = [:]
    private var latestEpoch: UInt64 = 0
    private var validator: Validator?

    func configure(validator: @escaping Validator) {
        lock.withLock { self.validator = validator }
    }

    @discardableResult
    func setApproved(_ ids: Set<UUID>, epoch: UInt64) -> Bool {
        lock.withLock {
            guard epoch >= latestEpoch else { return false }
            latestEpoch = epoch
            var changed = false
            for id in Array(leases.keys) where !ids.contains(id) {
                leases.removeValue(forKey: id)?.revoke()
                changed = true
            }
            for id in ids where leases[id] == nil {
                leases[id] = Lease()
                changed = true
            }
            return changed
        }
    }

    func capture(_ record: SyncRecord) throws -> Lease? {
        guard record.dataClass == .permittedSetting else { return nil }
        let (lease, validate) = lock.withLock { (leases[record.recordID], validator) }
        guard let lease, let validate else {
            throw BrowserSettingTransportAuthorizationError.categoryNotApproved
        }
        try lease.validate()
        try validate(record)
        try lease.validate()
        return lease
    }

    func isApproved(_ id: UUID, epoch: UInt64) -> Bool {
        let lease = lock.withLock { epoch == latestEpoch ? leases[id] : nil }
        guard let lease else { return false }
        return (try? lease.validate()) != nil
    }

    func authorize(_ record: SyncRecord) throws {
        try capture(record)?.validate()
    }
}
