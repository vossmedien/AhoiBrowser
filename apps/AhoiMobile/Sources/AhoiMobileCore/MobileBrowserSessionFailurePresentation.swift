import Foundation

enum MobileBrowserSessionFailurePresentation {
    static var restoreMessage: String {
        CompanionL10n.string(
            "browser.session.restore_failed",
            fallback: "AhoiBrowser couldn't restore the previous session. A new tab was opened, and the saved session was left unchanged."
        )
    }

    static var saveMessage: String {
        CompanionL10n.string(
            "browser.session.save_failed",
            fallback: "AhoiBrowser couldn't save the current session. Your open pages remain available; try another change to save again."
        )
    }
}
