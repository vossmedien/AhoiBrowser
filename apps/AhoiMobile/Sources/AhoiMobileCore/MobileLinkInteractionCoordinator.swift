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

/// Captures only a user-initiated link context-menu or long-press gesture. It
/// runs in an isolated content world, never reads page text or form data, and
/// validates the URL again before crossing into UI.
@MainActor
final class MobileLinkInteractionCoordinator: NSObject, WKScriptMessageHandler {
    static let handlerName = "ahoiLinkActions"
    private static let contentWorld = WKContentWorld.world(
        name: "AhoiBrowser.LinkActions"
    )

    private weak var userContentController: WKUserContentController?
    private let onLink: (URL, String) -> Void

    init(
        userContentController: WKUserContentController,
        onLink: @escaping (URL, String) -> Void
    ) {
        self.userContentController = userContentController
        self.onLink = onLink
        super.init()
        userContentController.add(
            self,
            contentWorld: Self.contentWorld,
            name: Self.handlerName
        )
        userContentController.addUserScript(WKUserScript(
            source: Self.script,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: false,
            in: Self.contentWorld
        ))
    }

    func invalidate() {
        userContentController?.removeScriptMessageHandler(
            forName: Self.handlerName,
            contentWorld: Self.contentWorld
        )
        userContentController = nil
    }

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage
    ) {
        guard message.name == Self.handlerName,
              message.world === Self.contentWorld,
              let body = message.body as? [String: Any],
              let activation = body["activation"] as? String,
              activation == "contextmenu" || activation == "longpress",
              let value = body["url"] as? String,
              value.utf8.count <= 8_192,
              let url = URL(string: value),
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else {
            return
        }
        onLink(url, Self.sourceOrigin(for: message.frameInfo))
    }

    private static func sourceOrigin(for frame: WKFrameInfo) -> String {
        let originScheme = frame.securityOrigin.protocol.lowercased()
        if (originScheme == "https" || originScheme == "http"),
           !frame.securityOrigin.host.isEmpty {
            return MobileBrowserOriginFormatter.label(
                for: frame.securityOrigin,
                fallbackURL: nil
            )
        }
        if let frameURL = frame.request.url,
           (try? MobileBrowserInputRouter.validateWebURL(frameURL)) != nil {
            return MobileBrowserOriginFormatter.label(for: frameURL)
        }
        return CompanionL10n.string(
            "browser.origin.unknown",
            fallback: "Unknown origin"
        )
    }

    private static let script = #"""
    (() => {
      if (globalThis.__ahoiLinkActionsInstalled) return;
      globalThis.__ahoiLinkActionsInstalled = true;

      const bridge = globalThis.webkit?.messageHandlers?.ahoiLinkActions;
      if (!bridge) return;

      const longPressDelay = 550;
      const movementTolerance = 12;
      const duplicateWindow = 900;
      const clickSuppressionWindow = 1_500;
      let activePress = null;
      let suppressedClick = null;
      let lastEmission = null;

      const linkForEvent = event => {
        const path = typeof event.composedPath === 'function'
          ? event.composedPath()
          : [event.target];
        for (const candidate of path) {
          if (!candidate || candidate.nodeType !== Node.ELEMENT_NODE) continue;
          const anchor = candidate.matches?.('a[href]')
            ? candidate
            : candidate.closest?.('a[href]');
          if (!anchor?.href) continue;
          try {
            const url = new URL(anchor.href, document.baseURI);
            if (url.protocol === 'https:' || url.protocol === 'http:') {
              return url.href;
            }
          } catch (_) {
            return null;
          }
        }
        return null;
      };

      const clearPress = () => {
        if (activePress?.timer) clearTimeout(activePress.timer);
        activePress = null;
      };

      const emit = (url, activation) => {
        const now = performance.now();
        if (lastEmission?.url === url && now - lastEmission.at < duplicateWindow) {
          return false;
        }
        lastEmission = { url, at: now };
        suppressedClick = {
          url,
          expiresAt: Date.now() + clickSuppressionWindow,
        };
        bridge.postMessage({ url, activation });
        return true;
      };

      const startPress = (url, identifier, x, y) => {
        clearPress();
        const press = { url, identifier, x, y, timer: null };
        press.timer = setTimeout(() => {
          if (activePress !== press) return;
          activePress = null;
          emit(url, 'longpress');
        }, longPressDelay);
        activePress = press;
      };

      const movePress = (identifier, x, y) => {
        if (!activePress || activePress.identifier !== identifier) return;
        if (Math.hypot(x - activePress.x, y - activePress.y) > movementTolerance) {
          clearPress();
        }
      };

      window.addEventListener('contextmenu', event => {
        if (!event.isTrusted) return;
        const url = linkForEvent(event);
        if (!url) return;
        event.preventDefault();
        event.stopImmediatePropagation();
        clearPress();
        emit(url, 'contextmenu');
      }, true);

      window.addEventListener('click', event => {
        if (!event.isTrusted || !suppressedClick) return;
        const url = linkForEvent(event);
        const shouldSuppress = url === suppressedClick.url
          && Date.now() <= suppressedClick.expiresAt;
        suppressedClick = null;
        if (!shouldSuppress) return;
        event.preventDefault();
        event.stopImmediatePropagation();
      }, true);

      if ('PointerEvent' in globalThis) {
        window.addEventListener('pointerdown', event => {
          if (!event.isTrusted || !event.isPrimary || event.button !== 0) return;
          if (event.pointerType !== 'touch' && event.pointerType !== 'pen') return;
          const url = linkForEvent(event);
          if (!url) return;
          startPress(url, event.pointerId, event.clientX, event.clientY);
        }, { capture: true, passive: true });
        window.addEventListener('pointermove', event => {
          movePress(event.pointerId, event.clientX, event.clientY);
        }, { capture: true, passive: true });
        window.addEventListener('pointerup', clearPress, {
          capture: true,
          passive: true,
        });
        window.addEventListener('pointercancel', clearPress, {
          capture: true,
          passive: true,
        });
      } else {
        window.addEventListener('touchstart', event => {
          if (!event.isTrusted || event.touches.length !== 1) return;
          const url = linkForEvent(event);
          const touch = event.touches[0];
          if (!url || !touch) return;
          startPress(url, touch.identifier, touch.clientX, touch.clientY);
        }, { capture: true, passive: true });
        window.addEventListener('touchmove', event => {
          if (!activePress) return;
          const touch = Array.from(event.touches).find(
            candidate => candidate.identifier === activePress.identifier
          );
          if (!touch) {
            clearPress();
            return;
          }
          movePress(touch.identifier, touch.clientX, touch.clientY);
        }, { capture: true, passive: true });
        window.addEventListener('touchend', clearPress, {
          capture: true,
          passive: true,
        });
        window.addEventListener('touchcancel', clearPress, {
          capture: true,
          passive: true,
        });
      }

      window.addEventListener('scroll', clearPress, {
        capture: true,
        passive: true,
      });
      window.addEventListener('blur', clearPress, true);
      document.addEventListener('visibilitychange', clearPress, true);
      window.addEventListener('pagehide', clearPress, true);
    })();
    """#
}
