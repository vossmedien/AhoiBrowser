import SwiftUI
import UIKit
import Combine

@MainActor
final class MobileBackgroundTaskLease {
    private let name: String
    private var identifier: UIBackgroundTaskIdentifier = .invalid

    init(name: String) {
        self.name = name
    }

    func begin() {
        guard identifier == .invalid else { return }
        identifier = UIApplication.shared.beginBackgroundTask(withName: name) { [weak self] in
            Task { @MainActor [weak self] in
                self?.end()
            }
        }
    }

    func end() {
        guard identifier != .invalid else { return }
        let activeIdentifier = identifier
        identifier = .invalid
        UIApplication.shared.endBackgroundTask(activeIdentifier)
    }
}

/// SwiftUI presentations live above their presenting view, so a root overlay
/// alone does not protect an already-open sheet or alert in the app-switcher
/// snapshot. This marker installs a matching opaque shield at the owning
/// window level and removes it with the conditional SwiftUI cover.
@MainActor
struct MobilePrivateSceneShield: UIViewRepresentable {
    let title: String
    let message: String
    let lock: MobilePrivateSessionLock
    let privateContentVisible: @MainActor () -> Bool
    let presentationVisible: @MainActor () -> Bool
    let prepareForInactive: @MainActor () -> Void
    let dismissPrivatePresentations: @MainActor () -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(title: title, message: message, lock: lock,
                    privateContentVisible: privateContentVisible,
                    presentationVisible: presentationVisible, prepareForInactive: prepareForInactive,
                    dismissPrivatePresentations: dismissPrivatePresentations)
    }

    func makeUIView(context: Context) -> MobilePrivateSceneMarkerView {
        let marker = MobilePrivateSceneMarkerView()
        marker.backgroundColor = .clear
        marker.isUserInteractionEnabled = false
        marker.onWindowChange = { [weak coordinator = context.coordinator] window in
            coordinator?.install(in: window)
        }
        return marker
    }

    func updateUIView(_ uiView: MobilePrivateSceneMarkerView, context: Context) {
        context.coordinator.update(title: title, message: message)
        context.coordinator.privateContentVisible = privateContentVisible
        context.coordinator.presentationVisible = presentationVisible
        context.coordinator.install(in: uiView.window)
    }

    static func dismantleUIView(
        _ uiView: MobilePrivateSceneMarkerView,
        coordinator: Coordinator
    ) {
        uiView.onWindowChange = nil
        coordinator.remove()
    }

    @MainActor
    final class Coordinator: NSObject {
        private let shield = MobilePrivateShieldView()
        private let titleLabel = UILabel()
        private let messageLabel = UILabel()
        private let unlockButton = UIButton(type: .system)
        private let lock: MobilePrivateSessionLock
        var privateContentVisible: @MainActor () -> Bool
        var presentationVisible: @MainActor () -> Bool
        private let prepareForInactive: @MainActor () -> Void
        private let dismissPrivatePresentations: @MainActor () -> Void
        private var clearingPrivatePresentation = false
        private weak var window: UIWindow?
        private var sceneID: String?
        private var active = false
        private var privatePresentationLatched = false
        private var baseTitle: String
        private var baseMessage: String
        private var previousAccessibilityElements: [Any]?
        private var subscription: AnyCancellable?

        init(title: String, message: String, lock: MobilePrivateSessionLock,
             privateContentVisible: @escaping @MainActor () -> Bool,
             presentationVisible: @escaping @MainActor () -> Bool,
             prepareForInactive: @escaping @MainActor () -> Void,
             dismissPrivatePresentations: @escaping @MainActor () -> Void) {
            self.lock = lock
            self.privateContentVisible = privateContentVisible
            self.presentationVisible = presentationVisible
            self.prepareForInactive = prepareForInactive
            self.dismissPrivatePresentations = dismissPrivatePresentations
            baseTitle = title; baseMessage = message
            super.init()
            shield.backgroundColor = .systemBackground
            shield.isOpaque = true
            shield.isUserInteractionEnabled = true
            shield.isAccessibilityElement = false
            shield.accessibilityViewIsModal = true
            shield.accessibilityIdentifier = "browser.private-window-shield"
            shield.layer.zPosition = 10_000

            let icon = UIImageView(image: UIImage(
                systemName: "hand.raised.fill",
                withConfiguration: UIImage.SymbolConfiguration(
                    pointSize: 34,
                    weight: .semibold
                )
            ))
            icon.tintColor = .systemPurple
            icon.contentMode = .scaleAspectFit
            icon.isAccessibilityElement = false

            titleLabel.font = .preferredFont(forTextStyle: .headline)
            titleLabel.textAlignment = .center
            titleLabel.adjustsFontForContentSizeCategory = true

            messageLabel.font = .preferredFont(forTextStyle: .subheadline)
            messageLabel.textColor = .secondaryLabel
            messageLabel.textAlignment = .center
            messageLabel.numberOfLines = 0
            messageLabel.adjustsFontForContentSizeCategory = true

            unlockButton.setTitle(CompanionL10n.string("browser.private.lock.unlock", fallback: "Unlock"), for: .normal)
            unlockButton.titleLabel?.font = .preferredFont(forTextStyle: .headline)
            unlockButton.accessibilityIdentifier = "browser.private.lock.unlock"
            unlockButton.addTarget(self, action: #selector(unlock), for: .touchUpInside)
            shield.requestUnlock = { [weak self] in self?.unlock() }
            let stack = UIStackView(arrangedSubviews: [icon, titleLabel, messageLabel, unlockButton])
            stack.axis = .vertical
            stack.alignment = .center
            stack.spacing = 12
            stack.translatesAutoresizingMaskIntoConstraints = false
            shield.addSubview(stack)
            NSLayoutConstraint.activate([
                icon.widthAnchor.constraint(equalToConstant: 48),
                icon.heightAnchor.constraint(equalToConstant: 48),
                stack.centerXAnchor.constraint(equalTo: shield.centerXAnchor),
                stack.centerYAnchor.constraint(equalTo: shield.centerYAnchor),
                stack.leadingAnchor.constraint(
                    greaterThanOrEqualTo: shield.leadingAnchor,
                    constant: 28
                ),
                stack.trailingAnchor.constraint(
                    lessThanOrEqualTo: shield.trailingAnchor,
                    constant: -28
                ),
            ])
            update(title: title, message: message)
            subscription = lock.changes.sink { [weak self] in
                MainActor.assumeIsolated { self?.refresh() }
            }
        }

        func update(title: String, message: String) {
            baseTitle = title; baseMessage = message
            refresh()
        }

        func install(in window: UIWindow?) {
            if self.window !== window {
                unbind()
                self.window = window
                if let scene = window?.windowScene {
                    sceneID = scene.session.persistentIdentifier
                    active = scene.activationState == .foregroundActive
                    let center = NotificationCenter.default
                    center.addObserver(self, selector: #selector(willDeactivate(_:)), name: UIScene.willDeactivateNotification, object: scene)
                    center.addObserver(self, selector: #selector(didActivate(_:)), name: UIScene.didActivateNotification, object: scene)
                    center.addObserver(self, selector: #selector(didBackground(_:)), name: UIScene.didEnterBackgroundNotification, object: scene)
                    center.addObserver(self, selector: #selector(disconnected(_:)), name: UIScene.didDisconnectNotification, object: scene)
                    if active { lock.sceneBecameActive(scene.session.persistentIdentifier) }
                    else { lock.sceneBecameInactive(scene.session.persistentIdentifier) }
                }
            }
            refresh()
        }

        private func refresh() {
            guard let window else { return }
            if privateContentVisible() { privatePresentationLatched = true }
            else if active && !presentationVisible() && !clearingPrivatePresentation { privatePresentationLatched = false }
            if privatePresentationLatched, presentationVisible(), lock.sessionGeneration() == nil,
               !clearingPrivatePresentation {
                // Closing the last private tab must not leave an address/rename
                // draft in a still-presented sheet. Keep the opaque shield until
                // UIKit completes that owned presentation's dismissal.
                clearingPrivatePresentation = true
                Task { @MainActor [weak self] in self?.clearEndedSessionPresentation() }
            }
            let privateVisible = privateContentVisible() || privatePresentationLatched
            guard clearingPrivatePresentation || (privateVisible && (!active || lock.isLocked)) else { hide(); return }
            titleLabel.text = baseTitle
            messageLabel.text = lock.isLocked ? CompanionL10n.string(
                lock.authenticationFailed ? "browser.private.lock.failed" : "browser.private.lock.message",
                fallback: lock.authenticationFailed ? "Device authentication was cancelled or unavailable. Try again." : "Unlock to return to your private tabs.") : baseMessage
            unlockButton.isHidden = clearingPrivatePresentation || !lock.isLocked || !active
            unlockButton.isEnabled = !lock.isAuthenticating
            shield.accessibilityElements = unlockButton.isHidden ? [titleLabel, messageLabel] : [titleLabel, messageLabel, unlockButton]
            window.endEditing(true)
            if shield.superview !== window {
                shield.removeFromSuperview()
                previousAccessibilityElements = window.accessibilityElements
                shield.frame = window.bounds
                shield.autoresizingMask = [.flexibleWidth, .flexibleHeight]
                window.addSubview(shield)
                // A custom window AX list also excludes later sheets/alerts.
                window.accessibilityElements = [shield]
                shield.becomeFirstResponder()
                if active && window.isKeyWindow { UIAccessibility.post(notification: .screenChanged, argument: titleLabel) }
            } else {
                shield.frame = window.bounds
                window.bringSubviewToFront(shield)
            }
        }

        func remove() {
            unbind()
            subscription?.cancel()
        }

        private func hide() {
            if let window, shield.superview === window {
                window.accessibilityElements = previousAccessibilityElements
                previousAccessibilityElements = nil
            }
            shield.resignFirstResponder()
            shield.removeFromSuperview()
        }

        private func unbind() {
            hide()
            clearingPrivatePresentation = false
            NotificationCenter.default.removeObserver(self)
            let oldScene = sceneID
            sceneID = nil; window = nil
            if let oldScene { lock.sceneDisconnected(oldScene) }
        }

        private func clearEndedSessionPresentation() {
            guard clearingPrivatePresentation, let window else { return }
            dismissPrivatePresentations()
            let completed: @MainActor @Sendable () -> Void = { [weak self, weak window] in
                guard let self, let window, self.window === window,
                      self.clearingPrivatePresentation else { return }
                self.clearingPrivatePresentation = false
                self.privatePresentationLatched = false
                self.refresh()
            }
            if let presented = window.rootViewController?.presentedViewController {
                presented.dismiss(animated: false, completion: completed)
            } else { completed() }
        }

        @objc private func unlock() { lock.authenticate(scene: sceneID) }
        @objc private func willDeactivate(_ notification: Notification) {
            active = false
            if privateContentVisible() { privatePresentationLatched = true }
            prepareForInactive()
            if let sceneID { lock.sceneBecameInactive(sceneID) }
            refresh() // Synchronous UIKit protection before its snapshot.
        }
        @objc private func didBackground(_ notification: Notification) {
            active = false
            if let sceneID { lock.sceneEnteredBackground(sceneID) }
            refresh()
        }
        @objc private func didActivate(_ notification: Notification) {
            active = true
            if let sceneID { lock.sceneBecameActive(sceneID) }
            refresh()
        }
        @objc private func disconnected(_ notification: Notification) { unbind() }
    }
}

@MainActor
private final class MobilePrivateShieldView: UIView {
    var requestUnlock: (@MainActor () -> Void)?
    override var canBecomeFirstResponder: Bool { true }
    override var keyCommands: [UIKeyCommand]? {
        let command = UIKeyCommand(input: "\r", modifierFlags: [], action: #selector(unlockFromKeyboard(_:)))
        command.discoverabilityTitle = CompanionL10n.string("browser.private.lock.unlock", fallback: "Unlock")
        return [command]
    }
    @objc private func unlockFromKeyboard(_ command: UIKeyCommand) {
        if window != nil { requestUnlock?() }
    }
    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {}
    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {}
}

@MainActor
final class MobilePrivateSceneMarkerView: UIView {
    var onWindowChange: (@MainActor (UIWindow?) -> Void)?

    override func didMoveToWindow() {
        super.didMoveToWindow()
        onWindowChange?(window)
    }
}
