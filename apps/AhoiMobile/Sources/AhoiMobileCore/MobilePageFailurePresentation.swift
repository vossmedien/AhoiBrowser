import Foundation
import SwiftUI

extension MobilePageFailureKind {
    var localizedTitle: String {
        switch self {
        case .offline:
            CompanionL10n.string("browser.failure.offline.title", fallback: "You're Offline")
        case .dnsLookupFailed:
            CompanionL10n.string(
                "browser.failure.dns.title",
                fallback: "Website Not Found"
            )
        case .timedOut:
            CompanionL10n.string(
                "browser.failure.timeout.title",
                fallback: "The Page Took Too Long"
            )
        case .transportSecurity:
            CompanionL10n.string(
                "browser.failure.security.title",
                fallback: "Secure Connection Failed"
            )
        case .httpClientError:
            CompanionL10n.string(
                "browser.failure.http_client.title",
                fallback: "Page Not Available"
            )
        case .httpServerError:
            CompanionL10n.string(
                "browser.failure.http_server.title",
                fallback: "Website Error"
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
        case .dnsLookupFailed:
            CompanionL10n.string(
                "browser.failure.dns.message",
                fallback: "Check the website name in the address and try again."
            )
        case .timedOut:
            CompanionL10n.string(
                "browser.failure.timeout.message",
                fallback: "The website did not respond in time."
            )
        case .transportSecurity:
            CompanionL10n.string(
                "browser.failure.security.message",
                fallback: "AhoiBrowser could not verify this website's secure connection."
            )
        case .httpClientError:
            CompanionL10n.string(
                "browser.failure.http_client.message",
                fallback: "The website could not provide this page. Check the address or try again."
            )
        case .httpServerError:
            CompanionL10n.string(
                "browser.failure.http_server.message",
                fallback: "The website encountered a problem. Try again in a moment."
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

    var systemImage: String {
        switch self {
        case .offline:
            "wifi.slash"
        case .dnsLookupFailed:
            "network.slash"
        case .transportSecurity:
            "lock.trianglebadge.exclamationmark"
        case .timedOut, .httpClientError, .httpServerError,
             .webContentTerminated, .invalidURL, .failed:
            "exclamationmark.icloud"
        }
    }
}

struct MobilePageRetryingView: View {
    var body: some View {
        ContentUnavailableView {
            ProgressView()
                .controlSize(.large)
        } description: {
            Text(CompanionL10n.string(
                "browser.retrying",
                fallback: "Trying again…"
            ))
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(uiColor: .systemBackground))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("browser.page-retrying")
    }
}

struct MobilePageFailureView: View {
    let failure: MobilePageFailureKind
    let onRetry: () -> Void

    var body: some View {
        ContentUnavailableView {
            Label(failure.localizedTitle, systemImage: failure.systemImage)
        } description: {
            Text(failure.localizedDescription)
        } actions: {
            Button(action: onRetry) {
                Label(
                    CompanionL10n.string("browser.retry", fallback: "Try Again"),
                    systemImage: "arrow.clockwise"
                )
                .accessibilityIdentifier("browser.retry")
            }
            .buttonStyle(.borderedProminent)
            .accessibilityIdentifier("browser.retry")
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(uiColor: .systemBackground))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("browser.page-failure")
    }
}
