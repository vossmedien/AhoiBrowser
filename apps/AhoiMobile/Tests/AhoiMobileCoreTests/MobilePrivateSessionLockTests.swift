import XCTest
import Foundation
@testable import AhoiMobileCore

@MainActor
final class MobilePrivateSessionLockTests: XCTestCase {
    private final class Auth: MobilePrivateAuthenticating {
        // Deliberately allows late replies after cancel to exercise the epoch fence.
        var available = true
        var reply: (@MainActor @Sendable (Bool) -> Void)?
        var cancelled = false
        func isAvailable() -> Bool { available }
        func evaluate(reason: String, completion: @escaping @MainActor @Sendable (Bool) -> Void) { reply = completion }
        func cancel() { cancelled = true }
    }

    func testBackgroundInvalidatesSuccessfulButNotYetActiveAuthentication() async {
        let name = "AhoiPrivateLockTests.\(UUID())"
        let defaults = UserDefaults(suiteName: name)!
        defer { defaults.removePersistentDomain(forName: name) }
        let auth = Auth()
        let lock = MobilePrivateSessionLock(defaults: defaults, makeAuthenticator: { auth })
        lock.sessionGeneration = { 1 }
        lock.sceneBecameActive("own-scene")
        lock.setEnabled(true)
        lock.authenticate(scene: "own-scene")
        lock.sceneBecameInactive("own-scene")
        auth.reply?(true)
        XCTAssertTrue(lock.isLocked)
        lock.sceneEnteredBackground("own-scene")
        lock.sceneBecameActive("own-scene")
        XCTAssertTrue(lock.isLocked)
        XCTAssertTrue(lock.isEnabled)
    }

    func testSystemPromptInactivityAndFailedAttemptRemainProtected() async {
        let name = "AhoiPrivateLockTests.\(UUID())"
        let defaults = UserDefaults(suiteName: name)!
        defer { defaults.removePersistentDomain(forName: name) }
        let auth = Auth()
        let lock = MobilePrivateSessionLock(defaults: defaults, makeAuthenticator: { auth })
        lock.sessionGeneration = { 7 }
        lock.sceneBecameActive("own-scene")
        lock.setEnabled(true)
        lock.authenticate(scene: "own-scene")
        auth.reply?(false)
        XCTAssertTrue(lock.isLocked)
        lock.authenticate(scene: "own-scene")
        lock.sceneBecameInactive("own-scene")
        auth.reply?(true)
        XCTAssertTrue(lock.isLocked)
        lock.sceneBecameActive("own-scene")
        XCTAssertFalse(lock.isLocked)
        XCTAssertTrue(lock.isEnabled)
    }

    func testOtherSceneBackgroundAndNewSessionRejectOldGrant() async {
        let name = "AhoiPrivateLockTests.\(UUID())"
        let defaults = UserDefaults(suiteName: name)!
        defer { defaults.removePersistentDomain(forName: name) }
        let auth = Auth()
        let lock = MobilePrivateSessionLock(defaults: defaults, makeAuthenticator: { auth })
        var session: UInt64? = 1
        lock.sessionGeneration = { session }
        lock.sceneBecameActive("A")
        lock.sceneBecameActive("B")
        lock.setEnabled(true)
        lock.authenticate(scene: "A")
        let oldReply = auth.reply
        lock.sceneEnteredBackground("B")
        oldReply?(true)
        XCTAssertTrue(lock.isLocked)
        lock.authenticate(scene: "A")
        session = 2
        auth.reply?(true)
        XCTAssertTrue(lock.isLocked)
        lock.setEnabled(false)
        XCTAssertTrue(lock.isEnabled)
        auth.reply?(false)
        XCTAssertTrue(lock.isEnabled)
    }
}
