import Foundation

enum MobileDownloadFailureKind: Sendable {
    case policy
    case destination
    case transfer
}

enum MobileDownloadFailurePresentation {
    static func message(
        for kind: MobileDownloadFailureKind,
        underlyingError: Error? = nil
    ) -> String {
        // Platform errors can contain paths, request metadata and WebKit
        // internals. They are intentionally not projected into product UI.
        _ = underlyingError
        return switch kind {
        case .policy:
            CompanionL10n.string(
                "browser.download.error.policy",
                fallback: "AhoiBrowser could not start this download safely."
            )
        case .destination:
            CompanionL10n.string(
                "browser.download.error.destination",
                fallback: "AhoiBrowser could not save this download. Check available storage and try again."
            )
        case .transfer:
            CompanionL10n.string(
                "browser.download.error.transfer",
                fallback: "The download was interrupted. Try again."
            )
        }
    }
}
