#if DEBUG
import WebKit
#endif
import SwiftUI
import UIKit
import Combine

#if DEBUG

/// Step 1 only: public default popup, without a WKWebExtensionTab adapter.
@MainActor
final class MobileWebExtensionHost: NSObject, WKWebExtensionControllerDelegate,
                                    UIAdaptivePresentationControllerDelegate {
    private weak var runtime: MobileWebExtensionRuntime?
    private weak var browser: MobileBrowserController?
    private weak var anchor: UIViewController?
    private var source: Source?
    private var generation = 0
    // Keep a cancelled loading request outstanding until its delegate arrives.
    // WebKit supplies no request ID; starting a replacement sooner is ambiguous.
    private var awaitingPopup = false
    private var pendingPopupAction: WKWebExtension.Action?
    private var pendingPopupContext: ObjectIdentifier?
    private var action: WKWebExtension.Action?
    private var presentation: UIViewController?
    private var completion: (((any Error)?) -> Void)?
    private var browserObservation: AnyCancellable?
    private var consentTask: Task<Void, Never>?

    private struct Source: Equatable {
        let tab: UUID
        let workspace: String
        let site: String
        let page: ObjectIdentifier
        let scene: String
        let context: ObjectIdentifier
    }

    init(runtime: MobileWebExtensionRuntime) {
        self.runtime = runtime
        super.init()
        NotificationCenter.default.addObserver(self, selector: #selector(sceneDidDeactivate(_:)),
                                               name: UIScene.willDeactivateNotification, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(sceneDidActivate(_:)),
                                               name: UIScene.didActivateNotification, object: nil)
    }

    @objc private func sceneDidDeactivate(_ notification: Notification) {
        guard let scene = notification.object as? UIScene,
              scene === anchor?.view.window?.windowScene else { return }
        cancel()
        runtime?.suspendRuleControl()
    }

    @objc private func sceneDidActivate(_ notification: Notification) {
        guard let scene = notification.object as? UIScene,
              scene === anchor?.view.window?.windowScene else { return }
        runtime?.reconcileExpiredRules()
    }

    func bind(anchor: UIViewController, browser: MobileBrowserController) {
        if self.anchor !== anchor || self.browser !== browser { cancel() }
        if self.browser !== browser {
            browserObservation = browser.objectWillChange.sink { [weak self] in
                // Conservatively invalidate before any browser mutation, including
                // a switch away and back within one SwiftUI rendering transaction.
                MainActor.assumeIsolated { if self?.source != nil { self?.cancel() } }
            }
        }
        self.anchor = anchor
        self.browser = browser
        if let source, currentSource() != source { cancel() }
    }

    func unbind(anchor: UIViewController) {
        guard self.anchor === anchor else { return }
        cancel()
        self.anchor = nil
        browser = nil
        browserObservation = nil
    }

    private func currentSource() -> Source? {
        guard let browser, let tab = browser.selectedTab, tab.mode == .normal,
              !browser.isSeparatedWorkspace(tab.workspaceID),
              browser.websiteDataStores[tab.id] === browser.normalWebsiteDataStore,
              let page = browser.pages[tab.id], let url = page.url,
              let pattern = try? MobileWebExtensionRuntime.sitePattern(for: url),
              let scene = anchor?.view.window?.windowScene,
              scene.activationState == .foregroundActive,
              let context = runtime?.context else { return nil }
        return Source(tab: tab.id, workspace: String(describing: tab.workspaceID),
                      site: pattern.string, page: ObjectIdentifier(page),
                      scene: scene.session.persistentIdentifier,
                      context: ObjectIdentifier(context))
    }

    /// Called at the PageActions tap, before its sheet dismisses. The closure
    /// revalidates the original source after onDismiss, not a replacement tab.
    func request(popup: Bool) -> (@MainActor () -> Void)? {
        guard !awaitingPopup, presentation == nil, source == nil,
              let expected = currentSource(),
              !popup || runtime?.context?.isLoaded == true else { return nil }
        let token = generation
        source = expected
        return { [weak self] in
            guard let self, self.generation == token else { return }
            guard self.currentSource() == expected, self.presentation == nil,
                  !self.awaitingPopup, self.source == expected else { self.cancel(); return }
            if popup {
                guard let context = self.runtime?.context,
                      let action = context.action(for: nil) else { self.cancel(); return }
                self.awaitingPopup = true
                self.pendingPopupAction = action
                self.pendingPopupContext = ObjectIdentifier(context)
                context.performAction(for: nil)
            } else {
                self.presentConsent(token: token)
            }
        }
    }

    private var presenter: UIViewController? {
        guard let anchor, anchor.view.window != nil else { return nil }
        var parent = anchor
        while let next = parent.parent { parent = next }
        guard parent.presentedViewController == nil, !parent.isBeingDismissed else { return nil }
        return parent
    }

    private func presentConsent(token: Int) {
        guard let source, currentSource() == source, let presenter,
              let runtime, let context = runtime.context,
              let url = browser?.selectedPage?.url else { cancel(); return }
        let expiration = Date().addingTimeInterval(180)
        let alert = UIAlertController(title: "Ahoi Spike · Site permission", message:
            "\(source.site)\nEffective host status: \(context.permissionStatus(for: url).rawValue)\n" +
            "Allow content access on this HTTPS host (all paths/ports), plus context-wide " +
            "storage, declarativeNetRequestWithHostAccess and nativeMessaging APIs until " +
            "\(expiration.formatted(date: .omitted, time: .standard)). " +
            "Rules stop up to " +
            "10 seconds before expiry or when this scene deactivates. " +
            "180 seconds is a DEBUG probe, not a Step-2 setting. Files contexts stay ungranted.",
            preferredStyle: .alert)
        for (title, choice) in [("Allow this site for 180 seconds", MobileWebExtensionRuntime.SiteChoice.allow),
                                ("Deny this site", .deny), ("Revoke this site", .revoke)] {
            alert.addAction(UIAlertAction(title: title, style: choice == .allow ? .default : .destructive) {
                [weak self] _ in
                guard let self, self.generation == token else { return }
                guard self.currentSource() == source else { self.cancel(); return }
                self.presentation = nil
                self.consentTask = Task { [weak self] in
                    do {
                        try await runtime.setSiteChoice(choice, for: url, expiration: expiration) { [weak self] in
                            self?.generation == token && self?.currentSource() == source
                        }
                    } catch {
                        NSLog("Ahoi Spike permission change failed: %@", String(describing: error))
                    }
                    guard let self, self.generation == token else { return }
                    self.consentTask = nil
                    self.source = nil
                    self.generation += 1
                }
            })
        }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel) { [weak self] _ in
            guard let self, self.generation == token else { return }
            self.cancel()
        })
        presentation = alert
        presenter.present(alert, animated: true)
    }

    func webExtensionController(_ controller: WKWebExtensionController,
                                presentActionPopup action: WKWebExtension.Action,
                                for context: WKWebExtensionContext,
                                completionHandler: @escaping ((any Error)?) -> Void) {
        // Unsolicited/foreign callbacks must not consume a current request or
        // dismiss a newer consent/popup. WebKit's already-owned action stays open.
        guard awaitingPopup, runtime?.context === context,
              controller === runtime?.controller, action.associatedTab == nil,
              action === pendingPopupAction else {
            if self.action !== action { action.closePopup() }
            completionHandler(CancellationError())
            return
        }
        awaitingPopup = false
        pendingPopupAction = nil
        pendingPopupContext = nil
        guard let source, currentSource() == source,
              presentation == nil, let presenter, action.presentsPopup,
              action.associatedTab == nil, let popup = action.popupViewController else {
            action.closePopup()
            completionHandler(CancellationError())
            cancel()
            return
        }
        self.action = action
        completion = completionHandler
        // Public containment gives the real WebKit controller a native Close.
        popup.title = "Ahoi Spike · Default popup"
        popup.navigationItem.rightBarButtonItem = UIBarButtonItem(
            title: "Close", style: .done, target: self, action: #selector(closePopup))
        popup.navigationItem.rightBarButtonItem?.accessibilityIdentifier = "spike.popup.close"
        popup.navigationItem.leftBarButtonItem = UIBarButtonItem(
            title: "Unload spike", style: .plain, target: self, action: #selector(unloadSpike))
        popup.navigationItem.leftBarButtonItem?.accessibilityIdentifier = "spike.popup.unload"
        let navigation = UINavigationController(rootViewController: popup)
        navigation.modalPresentationStyle = .popover
        navigation.preferredContentSize = CGSize(width: 340, height: 220)
        if let popover = navigation.popoverPresentationController, let view = anchor?.view {
            popover.sourceView = view // This scene's live anchor, never keyWindow.
            popover.sourceRect = view.bounds
            NSLog("Ahoi Spike popup ownSceneAnchor=%d standardAction=%d",
                  view.window?.windowScene?.session.persistentIdentifier == source.scene,
                  action.associatedTab == nil)
        }
        presentation = navigation
        let token = generation
        presenter.present(navigation, animated: true) { [weak self] in
            guard let self else { return }
            guard self.generation == token else { return }
            guard self.currentSource() == source else { self.cancel(); return }
            navigation.presentationController?.delegate = self
            self.finish(nil)
        }
    }

    private func finish(_ error: (any Error)?) {
        let callback = completion
        completion = nil
        callback?(error)
    }

    @objc private func closePopup() { cancel() }

    @objc private func unloadSpike() {
        do { try runtime?.unloadBundledSpike() }
        catch { cancel(); NSLog("Ahoi Spike unload failed") }
    }

    func presentationControllerDidDismiss(_ presentationController: UIPresentationController) {
        guard presentationController.presentedViewController === presentation else { return }
        cancel()
    }

    func invalidate(context: WKWebExtensionContext) {
        if source?.context == ObjectIdentifier(context) { cancel() }
    }

    func didUnload(context: WKWebExtensionContext) {
        guard !context.isLoaded, pendingPopupContext == ObjectIdentifier(context) else { return }
        // Confirmed unload cancels the SDK load even when no delegate follows.
        // A later reply has the old action identity and cannot consume a new one.
        awaitingPopup = false
        pendingPopupAction = nil
        pendingPopupContext = nil
    }

    func cancel() {
        generation += 1
        consentTask?.cancel()
        consentTask = nil
        source = nil
        let previous = presentation
        presentation = nil
        let previousAction = action
        action = nil
        finish(CancellationError())
        previous?.dismiss(animated: false)
        previousAction?.closePopup() // Exactly once, including public containment.
    }

    // No native tab adapter can authenticate an extension-initiated request.
    // The explicit PageActions consent above is the only granting route.
    func webExtensionController(_ controller: WKWebExtensionController,
                                connectUsing port: WKWebExtension.MessagePort,
                                for context: WKWebExtensionContext,
                                completionHandler: @escaping ((any Error)?) -> Void) {
        do {
            guard let runtime else { throw CancellationError() }
            try runtime.connectRulePort(port, controller: controller, context: context)
            completionHandler(nil)
        } catch {
            port.disconnect()
            completionHandler(error)
        }
    }

    func webExtensionController(_ controller: WKWebExtensionController,
                                promptForPermissions permissions: Set<WKWebExtension.Permission>,
                                in tab: (any WKWebExtensionTab)?,
                                for extensionContext: WKWebExtensionContext,
                                completionHandler: @escaping (Set<WKWebExtension.Permission>, Date?) -> Void) {
        completionHandler([], nil)
    }

    func webExtensionController(_ controller: WKWebExtensionController,
                                promptForPermissionToAccess urls: Set<URL>,
                                in tab: (any WKWebExtensionTab)?,
                                for extensionContext: WKWebExtensionContext,
                                completionHandler: @escaping (Set<URL>, Date?) -> Void) {
        completionHandler([], nil)
    }

    func webExtensionController(_ controller: WKWebExtensionController,
                                promptForPermissionMatchPatterns patterns: Set<WKWebExtension.MatchPattern>,
                                in tab: (any WKWebExtensionTab)?,
                                for extensionContext: WKWebExtensionContext,
                                completionHandler: @escaping (Set<WKWebExtension.MatchPattern>, Date?) -> Void) {
        completionHandler([], nil)
    }
}

#endif

struct MobileWebExtensionPopupAnchor: UIViewControllerRepresentable {
    let browser: MobileBrowserController
    static var reduceMotionProbe: Bool? {
#if DEBUG
        let args = ProcessInfo.processInfo.arguments
        return args.contains("-AhoiUITestFixture") && args.contains(MobileWebExtensionRuntime.launchArgument)
            && args.contains("-AhoiSpikeReduceMotion") ? true : nil
#else
        nil
#endif
    }
    func makeUIViewController(context: Context) -> UIViewController { UIViewController() }
    func updateUIViewController(_ controller: UIViewController, context: Context) {
#if DEBUG
        guard MobileWebExtensionRuntime.shared.controller != nil else { return }
        MobileWebExtensionRuntime.shared.host.bind(anchor: controller, browser: browser)
#endif
    }
    static func dismantleUIViewController(_ controller: UIViewController, coordinator: ()) {
#if DEBUG
        MobileWebExtensionRuntime.shared.host.unbind(anchor: controller)
#endif
    }
#if DEBUG
    /// App-owned measurement, independent of whether extension code was injected.
    static func probe(page: WebPage) async {
        guard page.url?.query?.contains("ahoi-spike-check") == true else { return }
        _ = try? await page.callJavaScript(#"""
        const token = new URL(location.href).searchParams.get('ahoi-spike-check');
        if (document.getElementById('ahoi-page-probe')) return;
        const probe = (name) => new Promise(resolve => {
            const script = document.createElement('script');
            script.src = location.origin + '/?ahoi-spike-probe=' + name;
            script.onload = () => resolve('loaded');
            script.onerror = () => resolve('failed');
            document.documentElement.appendChild(script);
        });
        Promise.all([probe('control.js'), probe('/ahoi-spike-blocked.js')]).then(([control, blocked]) => {
            const report = document.createElement('p');
            report.id = 'ahoi-page-probe';
            report.setAttribute('role', 'status');
            report.textContent = 'Ahoi PageProbe token=' + token
                + ' content=' + (document.documentElement.dataset.ahoiSpike || 'absent')
                + ' visits=' + (document.documentElement.dataset.ahoiSpikeVisits || 'absent')
                + ' control=' + control + ' rule=' + (blocked === 'failed' ? 'blocked' : 'not-blocked');
            report.style.cssText = 'font:600 18px -apple-system,sans-serif;padding:12px;background:#005b60;color:white';
            document.body.prepend(report);
        });
        """#)
    }
#endif
    static func request(popup: Bool) -> (@MainActor () -> Void)? {
#if DEBUG
        MobileWebExtensionRuntime.shared.host.request(popup: popup)
#else
        nil
#endif
    }
}
