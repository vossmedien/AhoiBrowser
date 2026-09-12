import SwiftUI

struct MobilePrivateLockSettingsSection: View {
    @ObservedObject var lock: MobilePrivateSessionLock
    var body: some View {
        Section {
            Toggle(CompanionL10n.string("settings.private.lock", fallback: "Lock private browsing"),
                   isOn: Binding(get: { lock.isEnabled }, set: { lock.setEnabled($0) }))
                .disabled(lock.isAuthenticating)
                .accessibilityIdentifier("settings.private.lock")
            if lock.authenticationFailed {
                Text(CompanionL10n.string("browser.private.lock.failed", fallback: "Device authentication was cancelled or unavailable. Try again."))
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("settings.private.lock.error")
            }
        } header: {
            Text(CompanionL10n.string("settings.private.section", fallback: "Private browsing"))
        } footer: {
            Text(CompanionL10n.string("settings.private.lock.detail", fallback: "Require device authentication after leaving the app. Locking keeps private tabs open. This setting stays on this device."))
        }
    }
}
