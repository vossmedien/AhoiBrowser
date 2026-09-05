import Foundation
import AhoiCloudKitSpike

enum BookmarkTransportAuthorizationError: Error, Equatable {
    case categoryNotApproved
}

/// Runtime consent belongs at the final outbound boundary as well as the
/// domain bridge: rehydrated pending records must not bypass an absent opt-in.
final class BookmarkTransportAuthorization: @unchecked Sendable {
    private let lock = NSLock()
    private var approved = false

    @discardableResult
    func setApproved(_ value: Bool) -> Bool {
        lock.withLock {
            guard value != approved else { return false }
            approved = value
            return true
        }
    }

    func authorize(_ record: SyncRecord) throws {
        guard record.schemaVersion <= SharedTabWireReadPolicy.defaultWriteVersion else {
            throw SharedTabWirePreparationError.writerNotActivated
        }
        if record.dataClass == .bookmark, !lock.withLock({ approved }) {
            throw BookmarkTransportAuthorizationError.categoryNotApproved
        }
    }
}
