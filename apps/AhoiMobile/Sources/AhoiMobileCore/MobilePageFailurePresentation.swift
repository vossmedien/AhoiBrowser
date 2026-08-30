import Foundation

extension MobilePageFailureKind {
    var localizedTitle: String {
        switch self {
        case .offline:
            CompanionL10n.string("browser.failure.offline.title", fallback: "You're Offline")
        case .timedOut:
            CompanionL10n.string(
                "browser.failure.timeout.title",
                fallback: "The Page Took Too Long"
            )
        case .webContentTerminated:
            CompanionL10n.string(
                "browser.failure.process.title",
                fallback: "Page Reload Required"
            )
        case .invalidURL:
            CompanionL10n.string(
                "browser.failure.invalid.title",
                fallback: "This Address Can't Be Opened"
            )
        case .failed:
            CompanionL10n.string(
                "browser.failure.generic.title",
                fallback: "Page Couldn't Load"
            )
        }
    }

    var localizedDescription: String {
        switch self {
        case .offline:
            CompanionL10n.string(
                "browser.failure.offline.message",
                fallback: "Check your connection and try again."
            )
        case .timedOut:
            CompanionL10n.string(
                "browser.failure.timeout.message",
                fallback: "The website did not respond in time."
            )
        case .webContentTerminated:
            CompanionL10n.string(
                "browser.failure.process.message",
                fallback: "iOS released the page process. Reload it to continue."
            )
        case .invalidURL:
            CompanionL10n.string(
                "browser.failure.invalid.message",
                fallback: "Check the address and try again."
            )
        case .failed:
            CompanionL10n.string(
                "browser.failure.generic.message",
                fallback: "The website could not be reached."
            )
        }
    }
}
