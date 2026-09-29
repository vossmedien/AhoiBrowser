import Foundation

/// One local opt-in attempt. Revocation is synchronous and permanent across
/// actor/CloudKit/Keychain hops; an off/on transition creates a different lease.
public final class CompanionSyncRuntimeAuthorization: @unchecked Sendable {
    private let lock = NSLock()
    private var allowed = true

    public init() {}
    public func isAuthorized() -> Bool { lock.withLock { allowed } }
    public func revoke() { lock.withLock { allowed = false } }
}
