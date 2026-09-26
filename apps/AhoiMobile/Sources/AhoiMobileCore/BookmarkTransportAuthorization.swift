import Foundation
import AhoiCloudKitSpike

enum BookmarkTransportAuthorizationError: Error, Equatable {
    case categoryNotApproved
    /// The record's class is outside the shared Format-3 catalogue.
    case dataClassNotShared
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
        guard record.schemaVersion == SharedSyncFormat.currentVersion else {
            throw SharedSyncFormatError.unsupportedVersion
        }
        guard SharedSyncFormat.supportedDataClasses.contains(record.dataClass) else {
            throw BookmarkTransportAuthorizationError.dataClassNotShared
        }
        if record.dataClass == .bookmark, !lock.withLock({ approved }) {
            throw BookmarkTransportAuthorizationError.categoryNotApproved
        }
    }
}
