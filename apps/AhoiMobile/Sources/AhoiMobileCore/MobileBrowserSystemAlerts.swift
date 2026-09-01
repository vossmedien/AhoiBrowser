import SwiftUI
import UIKit

@MainActor
struct MobileBrowserSystemAlertsModifier: ViewModifier {
    @ObservedObject var browser: MobileBrowserController
    @ObservedObject var permissions: MobilePermissionCoordinator

    func body(content: Content) -> some View {
        content
            .alert(
                CompanionL10n.string(
                    "browser.permission.title",
                    fallback: "Website permission"
                ),
                isPresented: permissionRequestPresented,
                presenting: permissions.pendingRequest
            ) { request in
                Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                    resolvePermission(request, allow: false)
                }
                .accessibilityIdentifier("browser.permission.cancel")

                Button(CompanionL10n.string("action.deny", fallback: "Don't Allow")) {
                    resolvePermission(request, allow: false)
                }
                .accessibilityIdentifier("browser.permission.deny")

                Button(CompanionL10n.string("action.allow", fallback: "Allow")) {
                    resolvePermission(request, allow: true)
                }
                .accessibilityIdentifier("browser.permission.allow")
            } message: { request in
                Text(CompanionL10n.format(
                    "browser.permission.message",
                    fallback: "%@ wants access to %@.",
                    request.origin,
                    request.kind.localizedLabel
                ))
                .accessibilityIdentifier("browser.permission.message")
                .accessibilityValue(Text(
                    "\(request.origin), \(request.kind.localizedLabel)"
                ))
            }
            .alert(
                CompanionL10n.string(
                    "browser.external.title",
                    fallback: "Open another app?"
                ),
                isPresented: externalOpenPresented,
                presenting: browser.pendingExternalOpen
            ) { request in
                Button(
                    CompanionL10n.string("action.cancel", fallback: "Cancel"),
                    role: .cancel
                ) {
                    browser.cancelPendingExternalOpen(requestID: request.id)
                }
                .accessibilityIdentifier("browser.external.cancel")

                Button(CompanionL10n.string(
                    "browser.external.open",
                    fallback: "Open App"
                )) {
                    openExternalRequest(request)
                }
                .accessibilityIdentifier("browser.external.open")
            } message: { request in
                Text(CompanionL10n.format(
                    "browser.external.message",
                    fallback: "%@ wants to open %@.",
                    MobileExternalOpenPresentation.originLabel(for: request.origin),
                    MobileExternalOpenPresentation.targetLabel(for: request.url)
                ))
                .accessibilityIdentifier("browser.external.message")
                .accessibilityValue(Text(
                    "\(MobileExternalOpenPresentation.originLabel(for: request.origin)), "
                        + MobileExternalOpenPresentation.targetLabel(for: request.url)
                ))
            }
    }

    private var permissionRequestPresented: Binding<Bool> {
        Binding(
            get: {
                guard let request = permissions.pendingRequest else { return false }
                return request.tabID == browser.selectedTabID
            },
            set: { presented in
                guard !presented,
                      let request = permissions.pendingRequest else { return }
                permissions.deny(requestID: request.id)
            }
        )
    }

    private var externalOpenPresented: Binding<Bool> {
        Binding(
            get: {
                guard let request = browser.pendingExternalOpen else { return false }
                return request.sourceTabID == browser.selectedTabID
            },
            set: { presented in
                guard !presented,
                      let request = browser.pendingExternalOpen else { return }
                browser.cancelPendingExternalOpen(requestID: request.id)
            }
        )
    }

    private func resolvePermission(
        _ request: MobilePermissionRequest,
        allow: Bool
    ) {
        guard request.tabID == browser.selectedTabID else {
            permissions.deny(requestID: request.id)
            return
        }
        if allow {
            permissions.allow(requestID: request.id)
        } else {
            permissions.deny(requestID: request.id)
        }
    }

    private func openExternalRequest(_ request: MobilePendingExternalOpen) {
        guard request.sourceTabID == browser.selectedTabID else {
            browser.cancelPendingExternalOpen(requestID: request.id)
            return
        }
        guard let url = browser.confirmPendingExternalOpen(requestID: request.id) else {
            return
        }
        Task { _ = await UIApplication.shared.open(url) }
    }
}

extension View {
    @MainActor
    func mobileBrowserSystemAlerts(
        browser: MobileBrowserController,
        permissions: MobilePermissionCoordinator
    ) -> some View {
        modifier(MobileBrowserSystemAlertsModifier(
            browser: browser,
            permissions: permissions
        ))
    }
}
