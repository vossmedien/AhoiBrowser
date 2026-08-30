import Foundation
import Combine
import CoreFoundation
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

struct MobilePageScrollEvent: Equatable, Sendable {
    private static let maximumLayoutExtent = 10_000_000.0
    private static let maximumSourceID = 1_000_000.0

    let sequence: UInt64
    let sourceID: UInt32
    let contentOffsetY: Double
    let contentHeight: Double
    let viewportHeight: Double

    init?(
        messageBody: [String: Any],
        sequence: UInt64
    ) {
        guard let contentOffsetY = Self.validatedNumber(messageBody["offsetY"]),
              let contentHeight = Self.validatedNumber(messageBody["contentHeight"]),
              let viewportHeight = Self.validatedNumber(messageBody["viewportHeight"]),
              let sourceID = Self.validatedSourceID(messageBody["sourceID"]),
              contentHeight > 0,
              viewportHeight > 0 else {
            return nil
        }
        let maximumOffset = max(0, contentHeight - viewportHeight)
        self.sequence = sequence
        self.sourceID = sourceID
        self.contentOffsetY = min(max(0, contentOffsetY), maximumOffset)
        self.contentHeight = contentHeight
        self.viewportHeight = viewportHeight
    }

    func hasStableLayout(comparedTo other: Self) -> Bool {
        sourceID == other.sourceID &&
            abs(contentHeight - other.contentHeight) < 0.5 &&
            abs(viewportHeight - other.viewportHeight) < 0.5
    }

    private static func validatedSourceID(_ value: Any?) -> UInt32? {
        guard let value else { return 0 }
        guard let number = validatedNumber(value),
              number <= maximumSourceID,
              number.rounded(.towardZero) == number else { return nil }
        return UInt32(number)
    }

    private static func validatedNumber(_ value: Any?) -> Double? {
        guard let number = value as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let result = number.doubleValue
        guard result.isFinite,
              result >= 0,
              result <= maximumLayoutExtent else { return nil }
        return result
    }
}

enum MobilePageScrollMessageDecoder {
    static func decode(
        messageBody: [String: Any],
        isMainFrame: Bool,
        sequence: UInt64
    ) -> MobilePageScrollEvent? {
        guard isMainFrame,
              messageBody["kind"] as? String == "scroll" else { return nil }
        return MobilePageScrollEvent(
            messageBody: messageBody,
            sequence: sequence
        )
    }
}

/// Captures user-initiated link actions and bounded numeric page-scroll layout
/// telemetry. It runs in an isolated content world, never reads page text or
/// form data, and validates every value again before crossing into UI.
@MainActor
final class MobileLinkInteractionCoordinator: NSObject, WKScriptMessageHandler {
    static let handlerName = "ahoiLinkActions"
    static let contentWorld = WKContentWorld.world(
        name: "AhoiBrowser.LinkActions"
    )
    private weak var userContentController: WKUserContentController?
    private let onLink: (URL, String) -> Void
    let scrollEvents = PassthroughSubject<MobilePageScrollEvent, Never>()
    private(set) var latestScrollEvent: MobilePageScrollEvent?
    private var scrollSequence: UInt64 = 0

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
              let kind = body["kind"] as? String else {
            return
        }
        if kind == "scroll" {
            receiveScroll(body, frame: message.frameInfo)
            return
        }
        guard kind == "link",
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

    private func receiveScroll(_ body: [String: Any], frame: WKFrameInfo) {
        let nextSequence = scrollSequence &+ 1
        guard let event = MobilePageScrollMessageDecoder.decode(
            messageBody: body,
            isMainFrame: frame.isMainFrame,
            sequence: nextSequence
        ) else {
            return
        }
        scrollSequence = nextSequence
        latestScrollEvent = event
        scrollEvents.send(event)
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
        bridge.postMessage({ kind: 'link', url, activation });
        return true;
      };

      let scrollSourceIDs = new WeakMap();
      let maximumScrollSourceID = 1_000_000;
      let scrollFrame = 0;
      let nextScrollSourceID = 1;
      let pendingScrollTarget = null;

      const sourceIDFor = element => {
        const existing = scrollSourceIDs.get(element);
        if (existing) return existing;
        const sourceID = Math.min(nextScrollSourceID, maximumScrollSourceID);
        nextScrollSourceID = Math.min(sourceID + 1, maximumScrollSourceID);
        scrollSourceIDs.set(element, sourceID);
        return sourceID;
      };

      const documentScrollMetrics = () => {
        const root = document.scrollingElement || document.documentElement;
        const contentHeight = Math.max(
          root?.scrollHeight || 0,
          document.documentElement?.scrollHeight || 0,
          document.body?.scrollHeight || 0
        );
        const viewportHeight = Math.max(
          globalThis.innerHeight || 0,
          document.documentElement?.clientHeight || 0
        );
        return {
          sourceID: 0,
          contentHeight,
          viewportHeight,
          offsetY: globalThis.scrollY || root?.scrollTop || 0,
        };
      };

      const scrollMetricsFor = target => {
        const isDocumentScroller = target === document.scrollingElement
          || target === document.documentElement
          || target === document.body;
        if (target instanceof Element && target.isConnected && !isDocumentScroller) {
          const contentHeight = Math.max(0, target.scrollHeight || 0);
          const viewportHeight = Math.max(0, target.clientHeight || 0);
          if (viewportHeight > 0 && contentHeight > viewportHeight + 0.5) {
            return {
              sourceID: sourceIDFor(target),
              contentHeight,
              viewportHeight,
              offsetY: target.scrollTop || 0,
            };
          }
        }
        return documentScrollMetrics();
      };

      const emitScroll = () => {
        scrollFrame = 0;
        const metrics = scrollMetricsFor(pendingScrollTarget);
        pendingScrollTarget = null;
        const maximumOffset = Math.max(
          0,
          metrics.contentHeight - metrics.viewportHeight
        );
        const rawOffset = metrics.offsetY;
        const offsetY = Math.min(maximumOffset, Math.max(0, rawOffset));
        bridge.postMessage({
          kind: 'scroll',
          sourceID: metrics.sourceID,
          offsetY,
          contentHeight: metrics.contentHeight,
          viewportHeight: metrics.viewportHeight,
        });
      };

      const scheduleScroll = event => {
        clearPress();
        if (event?.target instanceof Element && event.target.isConnected) {
          pendingScrollTarget = event.target;
        } else if (!scrollFrame) {
          pendingScrollTarget = null;
        }
        if (scrollFrame) return;
        scrollFrame = requestAnimationFrame(emitScroll);
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

      if (globalThis.top === globalThis) {
        window.addEventListener('scroll', scheduleScroll, {
          capture: true,
          passive: true,
        });
        document.addEventListener('scroll', scheduleScroll, {
          capture: true,
          passive: true,
        });
        window.addEventListener('resize', scheduleScroll, { passive: true });
        window.addEventListener('pageshow', scheduleScroll, { passive: true });
        window.addEventListener('load', scheduleScroll, {
          once: true,
          passive: true,
        });
        document.addEventListener('DOMContentLoaded', scheduleScroll, {
          once: true,
          passive: true,
        });
        scheduleScroll();
      } else {
        window.addEventListener('scroll', clearPress, {
          capture: true,
          passive: true,
        });
      }
      window.addEventListener('blur', clearPress, true);
      document.addEventListener('visibilitychange', clearPress, true);
      window.addEventListener('pagehide', clearPress, true);
    })();
    """#
}
