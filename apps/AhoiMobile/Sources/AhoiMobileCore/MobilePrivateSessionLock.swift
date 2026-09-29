import Combine
import Foundation
import LocalAuthentication

@MainActor
protocol MobilePrivateAuthenticating: AnyObject {
    func isAvailable() -> Bool
    func evaluate(reason: String, completion: @escaping @MainActor @Sendable (Bool) -> Void)
    func cancel()
}

@MainActor
final class MobileSystemPrivateAuthenticator: MobilePrivateAuthenticating {
    private let context = LAContext()
    func isAvailable() -> Bool { context.canEvaluatePolicy(.deviceOwnerAuthentication, error: nil) }
    func evaluate(reason: String, completion: @escaping @MainActor @Sendable (Bool) -> Void) {
        context.touchIDAuthenticationAllowableReuseDuration = 0
        context.evaluatePolicy(.deviceOwnerAuthentication, localizedReason: reason) { success, _ in
            Task { @MainActor in completion(success) }
        }
    }
    func cancel() { context.invalidate() }
}

/// Local access/visibility protection for the existing ephemeral private session.
/// No tabs, WebKit stores, keys or synchronized settings are created/deleted here.
@MainActor
public final class MobilePrivateSessionLock: ObservableObject {
    public static let preferenceKey = "AhoiMobile.Private.RequireDeviceAuthentication"
    @Published public private(set) var isEnabled: Bool
    @Published public private(set) var isLocked = false
    @Published public private(set) var isAuthenticating = false
    @Published public private(set) var authenticationFailed = false
    // Synchronous, post-state-change notification for the UIKit snapshot shield.
    let changes = PassthroughSubject<Void, Never>()
    var sessionGeneration: @MainActor () -> UInt64? = { nil }
    private let defaults: UserDefaults
    private let makeAuthenticator: @MainActor () -> any MobilePrivateAuthenticating
    private var authenticator: (any MobilePrivateAuthenticating)?
    private var activeScenes: Set<String> = []
    private var epoch: UInt64 = 0
    private var attempt: Attempt?
    private var successfulAttempt: Attempt?
    private struct Attempt {
        let epoch: UInt64
        let scene: String
        let session: UInt64
        let disableAfter: Bool
    }

    init(defaults: UserDefaults,
         makeAuthenticator: @escaping @MainActor () -> any MobilePrivateAuthenticating = { MobileSystemPrivateAuthenticator() }) {
        self.defaults = defaults
        self.makeAuthenticator = makeAuthenticator
        isEnabled = defaults.bool(forKey: Self.preferenceKey)
    }

    func setEnabled(_ enabled: Bool) {
        guard enabled != isEnabled, !isAuthenticating else { return }
        if !enabled, isLocked, sessionGeneration() != nil {
            authenticate(disableAfter: true)
            return
        }
        if enabled {
            let probe = makeAuthenticator()
            let available = probe.isAvailable()
            probe.cancel()
            guard available else { authenticationFailed = true; changes.send(); return }
        }
        invalidateAttempt()
        isEnabled = enabled
        isLocked = enabled && sessionGeneration() != nil
        defaults.set(enabled, forKey: Self.preferenceKey)
        authenticationFailed = false
        changes.send()
    }

    func sceneBecameActive(_ scene: String) {
        activeScenes.insert(scene)
        if let successfulAttempt, successfulAttempt.scene == scene { finish(successfulAttempt) }
        changes.send()
    }

    func sceneBecameInactive(_ scene: String) {
        activeScenes.remove(scene)
        if isEnabled, sessionGeneration() != nil { isLocked = true }
        // The system authentication prompt itself can make the scene inactive.
        // Keep the shield; a real background/disconnect invalidates the attempt.
        changes.send()
    }

    func sceneEnteredBackground(_ scene: String) {
        activeScenes.remove(scene)
        invalidateAttempt()
        if isEnabled, sessionGeneration() != nil { isLocked = true }
        changes.send()
    }

    func sceneDisconnected(_ scene: String) {
        sceneEnteredBackground(scene)
    }

    func privateSessionChanged() {
        if sessionGeneration() == nil {
            invalidateAttempt()
            isLocked = false
            authenticationFailed = false
        }
        changes.send()
    }

    func authenticate(scene: String? = nil, disableAfter: Bool = false) {
        guard isEnabled, isLocked, !isAuthenticating, let session = sessionGeneration(),
              let scene = scene ?? activeScenes.sorted().first, activeScenes.contains(scene) else { return }
        invalidateAttempt()
        let context = makeAuthenticator()
        guard context.isAvailable() else {
            context.cancel(); authenticationFailed = true; changes.send(); return
        }
        let request = Attempt(epoch: epoch, scene: scene, session: session, disableAfter: disableAfter)
        attempt = request; authenticator = context; isAuthenticating = true; authenticationFailed = false
        changes.send()
        context.evaluate(reason: CompanionL10n.string("browser.private.lock.reason", fallback: "Unlock private browsing")) { [weak self] success in
            guard let self, self.attempt?.epoch == request.epoch, self.epoch == request.epoch else { return }
            self.authenticator = nil; self.attempt = nil; self.isAuthenticating = false
            guard success, self.sessionGeneration() == request.session else {
                self.authenticationFailed = true; self.changes.send(); return
            }
            // Never uncover an inactive scene while LocalAuthentication returns.
            self.successfulAttempt = request
            if self.activeScenes.contains(request.scene) { self.finish(request) }
            self.changes.send()
        }
    }

    private func finish(_ request: Attempt) {
        guard epoch == request.epoch, activeScenes.contains(request.scene),
              sessionGeneration() == request.session, isEnabled else { return }
        successfulAttempt = nil
        isLocked = false
        if request.disableAfter {
            isEnabled = false
            defaults.set(false, forKey: Self.preferenceKey)
        }
        authenticationFailed = false
    }

    private func invalidateAttempt() {
        epoch &+= 1
        authenticator?.cancel(); authenticator = nil
        attempt = nil; successfulAttempt = nil; isAuthenticating = false
    }
}
