import CloudKit
import Foundation
import Security

/// A bounded, non-secret reason why the explicitly enabled Sync runtime did
/// not become ready. It retains only stable categories and safe numeric status
/// codes; NSError userInfo, account identifiers, records and key bytes never
/// enter presentation or evidence.
public enum CompanionSyncSetupIssue: Equatable, Sendable {
    case staticConfiguration
    case cloudKitAccountOrPermission(code: Int?)
    case cloudKitTransport(code: Int?)
    case cloudKitPartialFailure(leafCodes: [Int])
    case localAuthorization
    case keychain(status: OSStatus)
    case bootstrapRecovery(CompanionKeyRecoveryReason)
    case waitingForKey(CompanionKeyWaitingReason)
    case migration
    case claiming
    case rotation
    case revoked
    case unknown

    static func classify(_ error: Error) -> Self {
        if let error = error as? CloudKitKeyBootstrapError {
            return classify(error)
        }
        if let error = error as? CompanionPayloadKeyStoreError {
            return classify(error)
        }
        if let error = error as? CompanionKeyLifecycleError {
            return classify(error)
        }
        if let error = error as? CompanionSyncKeyError {
            return classify(error)
        }
        if let error = error as? CompanionCloudKitBootstrapError {
            return classify(error)
        }
        if let error = error as? CKError {
            switch error.code {
            case .notAuthenticated, .permissionFailure:
                return .cloudKitAccountOrPermission(code: error.code.rawValue)
            default:
                return .cloudKitTransport(code: error.code.rawValue)
            }
        }
        let cocoaError = error as NSError
        if cocoaError.domain == NSOSStatusErrorDomain {
            return .keychain(status: OSStatus(cocoaError.code))
        }
        return .unknown
    }

    static func from(_ status: CompanionKeyLifecycleStatus) -> Self? {
        switch status {
        case .disabled, .ready:
            nil
        case .waiting(_, let reason):
            .waitingForKey(reason)
        case .migration:
            .migration
        case .claiming:
            .claiming
        case .rotation:
            .rotation
        case .revoked:
            .revoked
        case .recovery(let reason, _):
            .bootstrapRecovery(reason)
        }
    }

    var keyLifecyclePresentation: CompanionKeyLifecycleStatus {
        switch self {
        case .bootstrapRecovery(let reason):
            .recovery(reason: reason, keyVersion: nil)
        case .waitingForKey(let reason):
            .waiting(keyVersion: 1, reason: reason)
        case .migration:
            .migration(keyVersion: 1)
        case .claiming:
            .claiming(keyVersion: 1)
        case .rotation:
            .recovery(reason: .keyVersionMismatch, keyVersion: nil)
        case .revoked:
            .recovery(reason: .revokedKey, keyVersion: nil)
        case .staticConfiguration, .cloudKitAccountOrPermission,
             .cloudKitTransport, .cloudKitPartialFailure, .localAuthorization,
             .keychain, .unknown:
            .disabled
        }
    }

    public var evidenceValue: String {
        switch self {
        case .staticConfiguration:
            return "static-configuration"
        case .cloudKitAccountOrPermission(let code):
            return code.map { "cloudkit-account-or-permission:\($0)" }
                ?? "cloudkit-account-or-permission"
        case .cloudKitTransport(let code):
            return code.map { "cloudkit-error:\($0)" } ?? "cloudkit-unavailable"
        case .cloudKitPartialFailure(let leafCodes):
            let codes = leafCodes.map(String.init).joined(separator: ",")
            return codes.isEmpty
                ? "cloudkit-error:2"
                : "cloudkit-error:2;leaf-codes:\(codes)"
        case .localAuthorization:
            return "local-authorization-unavailable"
        case .keychain(let status):
            return "keychain-osstatus:\(status)"
        case .bootstrapRecovery(let reason):
            return "bootstrap-recovery:\(reason.rawValue)"
        case .waitingForKey(let reason):
            return "key-waiting:\(reason.rawValue)"
        case .migration:
            return "key-migration"
        case .claiming:
            return "key-claiming"
        case .rotation:
            return "key-rotation"
        case .revoked:
            return "key-revoked"
        case .unknown:
            return "unknown"
        }
    }

    var localizedState: String {
        switch self {
        case .staticConfiguration:
            CompanionL10n.string("sync.setup.state.configuration", fallback: "Setup required")
        case .cloudKitAccountOrPermission:
            CompanionL10n.string("sync.setup.state.cloudkit", fallback: "iCloud access required")
        case .cloudKitTransport, .cloudKitPartialFailure:
            CompanionL10n.string("sync.setup.state.transport", fallback: "CloudKit unavailable")
        case .localAuthorization:
            CompanionL10n.string("sync.setup.state.cancelled", fallback: "Sync setup stopped")
        case .keychain:
            CompanionL10n.string("sync.setup.state.keychain", fallback: "Keychain access required")
        case .bootstrapRecovery, .rotation, .revoked:
            CompanionL10n.string("sync.setup.state.recovery", fallback: "Sync recovery required")
        case .waitingForKey:
            CompanionL10n.string("sync.setup.state.waiting", fallback: "Waiting for sync key")
        case .migration, .claiming:
            CompanionL10n.string("sync.setup.state.preparing", fallback: "Preparing encryption")
        case .unknown:
            CompanionL10n.string("sync.setup.state.unknown", fallback: "Sync setup unavailable")
        }
    }

    var localizedDetail: String {
        switch self {
        case .staticConfiguration:
            CompanionL10n.string(
                "sync.setup.detail.configuration",
                fallback: "This build does not contain a complete CloudKit configuration. Local data remains available."
            )
        case .cloudKitAccountOrPermission:
            CompanionL10n.string(
                "sync.setup.detail.cloudkit",
                fallback: "Check iCloud sign-in and CloudKit permission. Local data remains available."
            )
        case .cloudKitTransport, .cloudKitPartialFailure:
            CompanionL10n.string(
                "sync.setup.detail.transport",
                fallback: "CloudKit is currently unavailable. Local data remains available; try again later."
            )
        case .localAuthorization:
            CompanionL10n.string(
                "sync.setup.detail.cancelled",
                fallback: "Sync setup stopped before completion. Local data remains available; try enabling Sync again."
            )
        case .keychain:
            CompanionL10n.string(
                "sync.setup.detail.keychain",
                fallback: "AhoiBrowser cannot access the protected sync key in iCloud Keychain. Local data remains available."
            )
        case .bootstrapRecovery(let reason):
            bootstrapRecoveryDetail(reason)
        case .waitingForKey(let reason):
            waitingDetail(reason)
        case .migration:
            CompanionL10n.string(
                "sync.setup.detail.migration",
                fallback: "AhoiBrowser is verifying the existing sync key."
            )
        case .claiming:
            CompanionL10n.string(
                "sync.setup.detail.claiming",
                fallback: "AhoiBrowser is securing first-time sync setup."
            )
        case .rotation:
            CompanionL10n.string(
                "sync.setup.detail.rotation",
                fallback: "Complete sync-key rotation before encrypted transport can continue."
            )
        case .revoked:
            CompanionL10n.string(
                "sync.setup.detail.revoked",
                fallback: "The sync key was revoked. Reconnect this device before syncing."
            )
        case .unknown:
            CompanionL10n.string(
                "sync.setup.detail.unknown",
                fallback: "Sync setup could not be completed. Local data remains available."
            )
        }
    }

    private static func classify(_ error: CloudKitKeyBootstrapError) -> Self {
        switch error {
        case .invalidConfiguration:
            .staticConfiguration
        case .accountChanged:
            .bootstrapRecovery(.accountChanged)
        case .cloudKitAccess(let code):
            .cloudKitAccountOrPermission(code: code)
        case .localAuthorizationUnavailable:
            .localAuthorization
        case .zoneCreationFailed(let code), .fetchFailed(let code), .sendFailed(let code):
            .cloudKitTransport(code: code)
        case .fetchPartialFailure(let leafCodes):
            .cloudKitPartialFailure(leafCodes: leafCodes)
        case .corruptClaim, .conflictingClaims, .receiptPersistenceFailed,
             .missingSendResult:
            .bootstrapRecovery(.bootstrapOwnershipUnverified)
        }
    }

    private static func classify(_ error: CompanionPayloadKeyStoreError) -> Self {
        switch error {
        case .invalidConfiguration:
            .staticConfiguration
        case .keychainStatus(let status), .randomGenerationFailed(let status):
            .keychain(status: status)
        case .journalVersionMismatch, .rotationKeyUnchanged:
            .bootstrapRecovery(.keyVersionMismatch)
        case .corruptJournal, .candidateMissing, .receiptMismatch, .splitKeyPrevented,
             .invalidKeyLength:
            .bootstrapRecovery(.bootstrapOwnershipUnverified)
        case .authorizationRevoked:
            .unknown
        }
    }

    private static func classify(_ error: CompanionKeyLifecycleError) -> Self {
        switch error {
        case .invalidKeyVersion:
            .staticConfiguration
        case .remoteClaimChanged, .candidateMissing, .acceptedReceiptMissing,
             .splitKeyPrevented:
            .bootstrapRecovery(.bootstrapOwnershipUnverified)
        case .invalidGeneratedKeyLength:
            .bootstrapRecovery(.keychainFailure)
        case .bootstrapTransportUnavailable:
            .cloudKitTransport(code: nil)
        case .explicitOptInRequired:
            .unknown
        }
    }

    private static func classify(_ error: CompanionSyncKeyError) -> Self {
        switch error {
        case .invalidConfiguration:
            .staticConfiguration
        case .keyUnavailable(let status):
            .keychain(status: status)
        case .unsupportedKeyVersion:
            .bootstrapRecovery(.keyVersionMismatch)
        case .invalidKeyLength, .invalidCiphertext, .keyCommitmentMismatch:
            .bootstrapRecovery(.bootstrapOwnershipUnverified)
        }
    }

    private static func classify(_ error: CompanionCloudKitBootstrapError) -> Self {
        switch error {
        case .invalidContainerIdentifier:
            .staticConfiguration
        case .keyConfigurationMissing:
            .bootstrapRecovery(.bootstrapOwnershipUnverified)
        case .providerUnavailable:
            .cloudKitTransport(code: nil)
        case .recordStoreInitializationFailed, .quarantineStoreInitializationFailed,
             .systemFieldsStoreInitializationFailed,
             .remoteCommandOwnershipStoreInitializationFailed:
            .unknown
        }
    }

    private func bootstrapRecoveryDetail(_ reason: CompanionKeyRecoveryReason) -> String {
        switch reason {
        case .remoteDataWithoutKey:
            CompanionL10n.string(
                "sync.setup.detail.remote_data_without_key",
                fallback: "Encrypted CloudKit data exists, but its sync key is unavailable on this device."
            )
        case .bootstrapOwnershipUnverified:
            CompanionL10n.string(
                "sync.setup.detail.bootstrap_unverified",
                fallback: "AhoiBrowser could not verify ownership of the encrypted sync key."
            )
        case .keyVersionMismatch:
            CompanionL10n.string(
                "sync.setup.detail.key_version",
                fallback: "The available sync-key version does not match the encrypted CloudKit data."
            )
        case .keychainFailure:
            CompanionL10n.string(
                "sync.setup.detail.keychain",
                fallback: "AhoiBrowser cannot access the protected sync key in iCloud Keychain. Local data remains available."
            )
        case .accountChanged:
            CompanionL10n.string(
                "sync.setup.detail.account_changed",
                fallback: "The iCloud account changed. Review the retained local snapshot before uploading."
            )
        case .zoneLost:
            CompanionL10n.string(
                "sync.setup.detail.zone_lost",
                fallback: "The CloudKit zone requires explicit recovery before syncing can continue."
            )
        case .revokedKey:
            CompanionL10n.string(
                "sync.setup.detail.revoked",
                fallback: "The sync key was revoked. Reconnect this device before syncing."
            )
        }
    }

    private func waitingDetail(_ reason: CompanionKeyWaitingReason) -> String {
        switch reason {
        case .pairingKeyPending:
            CompanionL10n.string(
                "sync.setup.detail.waiting_pairing",
                fallback: "Waiting for the approved pairing key."
            )
        case .anotherDeviceWonBootstrap:
            CompanionL10n.string(
                "sync.setup.detail.waiting_other_device",
                fallback: "Another approved device secured first-time setup. Waiting for its sync key."
            )
        case .synchronizableKeyPending:
            CompanionL10n.string(
                "sync.setup.detail.waiting_keychain",
                fallback: "Waiting for the sync key from iCloud Keychain."
            )
        }
    }
}
