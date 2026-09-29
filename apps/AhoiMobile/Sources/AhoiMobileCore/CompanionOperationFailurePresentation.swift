import SwiftUI

enum CompanionOperationFailurePresentation {
    static func message(for error: Error) -> String {
        // Store, Keychain and CloudKit errors may contain local paths, record
        // payloads or account details. Product UI gets a stable recovery text.
        _ = error
        return CompanionL10n.string(
            "library.operation.failed",
            fallback: "That change could not be saved. Your existing library is unchanged. Try again."
        )
    }

    static func localProjectionMessage(for error: Error) -> String {
        _ = error
        return CompanionL10n.string(
            "library.operation.local_projection_failed",
            fallback: "The change was saved on this device, but the library view could not refresh. Reopen the library and try again."
        )
    }

    static func syncQueueMessage(for error: Error) -> String {
        _ = error
        return CompanionL10n.string(
            "library.operation.sync_queue_failed",
            fallback: "The change was saved on this device, but could not be queued for sync. Use Retry Sync when the connection is available."
        )
    }
}

extension CompanionAppModel {
    func presentOperationFailure(_ error: Error) {
        loadError = CompanionOperationFailurePresentation.message(for: error)
    }

    func presentLocalProjectionFailure(_ error: Error) {
        loadError = CompanionOperationFailurePresentation.localProjectionMessage(for: error)
    }

    func presentSyncQueueFailure(_ error: Error) {
        loadError = CompanionOperationFailurePresentation.syncQueueMessage(for: error)
    }

    public func dismissLoadError() {
        loadError = nil
    }
}

struct CompanionOperationErrorBanner: View {
    let message: String
    let dismiss: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.orange)
            Text(message)
                .font(.callout)
                .frame(maxWidth: .infinity, alignment: .leading)
            Button(action: dismiss) {
                Image(systemName: "xmark.circle.fill")
            }
            .buttonStyle(.plain)
            .accessibilityLabel(CompanionL10n.string(
                "library.operation.dismiss",
                fallback: "Dismiss library error"
            ))
            .accessibilityIdentifier("browser.library.error.dismiss")
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(.regularMaterial)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("browser.library.error")
    }
}
