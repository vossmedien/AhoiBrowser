import Foundation
import WebKit
import AhoiCloudKitSpike

public struct MobilePendingLink: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let url: URL
    public let sourceTabID: UUID
    public let sourceOrigin: String
    public let workspaceID: WorkspaceID?
    public let sourceMode: MobileBrowsingMode

    public init(
        id: UUID = UUID(),
        url: URL,
        sourceTabID: UUID,
        sourceOrigin: String,
        workspaceID: WorkspaceID?,
        sourceMode: MobileBrowsingMode
    ) {
        self.id = id
        self.url = url
        self.sourceTabID = sourceTabID
        self.sourceOrigin = sourceOrigin
        self.workspaceID = workspaceID
        self.sourceMode = sourceMode
    }
}

/// Captures only a user-initiated link context-menu gesture. It never reads
/// page text or form data and validates the URL again before crossing into UI.
@MainActor
final class MobileLinkInteractionCoordinator: NSObject, WKScriptMessageHandler {
    static let handlerName = "ahoiLinkActions"

    private weak var userContentController: WKUserContentController?
    private let onLink: (URL) -> Void

    init(
        userContentController: WKUserContentController,
        onLink: @escaping (URL) -> Void
    ) {
        self.userContentController = userContentController
        self.onLink = onLink
        super.init()
        userContentController.add(self, name: Self.handlerName)
        userContentController.addUserScript(WKUserScript(
            source: Self.script,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: false
        ))
    }

    func invalidate() {
        userContentController?.removeScriptMessageHandler(forName: Self.handlerName)
        userContentController = nil
    }

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage
    ) {
        guard message.name == Self.handlerName,
              let value = message.body as? String,
              value.utf8.count <= 8_192,
              let url = URL(string: value),
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else {
            return
        }
        onLink(url)
    }

    private static let script = #"""
    (() => {
      if (window.__ahoiLinkActionsInstalled) return;
      window.__ahoiLinkActionsInstalled = true;
      window.addEventListener('contextmenu', event => {
        const element = event.target instanceof Element ? event.target : null;
        const anchor = element?.closest('a[href]');
        if (!anchor || !anchor.href) return;
        event.preventDefault();
        window.webkit?.messageHandlers?.ahoiLinkActions?.postMessage(anchor.href);
      }, true);
    })();
    """#
}
