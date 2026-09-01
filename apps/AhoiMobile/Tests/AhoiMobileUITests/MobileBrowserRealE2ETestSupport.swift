import CryptoKit
import Foundation
import UIKit
import XCTest

class MobileBrowserRealE2ETestCase: XCTestCase {
    private static let optInEnvironmentKey = "AHOI_MOBILE_REAL_E2E"
    private static let baseURLEnvironmentKey = "AHOI_MOBILE_E2E_BASE_URL"
    private static let sourceCommitEnvironmentKey = "AHOI_MOBILE_EXPECTED_SOURCE_COMMIT"
    private static let buildModeEnvironmentKey = "AHOI_MOBILE_EXPECTED_BUILD_MODE"
    private static let receiptSHAEnvironmentKey = "AHOI_MOBILE_CANDIDATE_RECEIPT_SHA256"
    private static let receiptPayloadEnvironmentKey =
        "AHOI_MOBILE_CANDIDATE_RECEIPT_BASE64"
    private static let requiredFixtureContractVersion = 2
    private var candidateBinding: MobileRealE2ECandidateBinding?

    override func setUpWithError() throws {
        try super.setUpWithError()
        continueAfterFailure = false
        guard ProcessInfo.processInfo.environment[Self.optInEnvironmentKey] == "1" else {
            throw XCTSkip(
                "Set AHOI_MOBILE_REAL_E2E=1 to run visible journeys against the local HTTPS fixture."
            )
        }
        do {
            candidateBinding = try MobileRealE2ECandidateBinding(
                environment: ProcessInfo.processInfo.environment,
                sourceCommitKey: Self.sourceCommitEnvironmentKey,
                buildModeKey: Self.buildModeEnvironmentKey,
                receiptSHAKey: Self.receiptSHAEnvironmentKey,
                receiptPayloadKey: Self.receiptPayloadEnvironmentKey
            )
        } catch {
            XCTFail(
                "Visible E2E opt-in requires an exact clean-candidate runner binding: "
                    + error.localizedDescription
            )
            throw error
        }
    }

    @MainActor
    func coldLaunchApplication() -> XCUIApplication {
        launchExactCandidate(arguments: [])
    }

    @MainActor
    func launchExactCandidate(arguments: [String]) -> XCUIApplication {
        let app = exactCandidateApplication(arguments: arguments)
        app.terminate()
        app.launch()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))
        assertExactCandidateBinding(in: app)
        return app
    }

    @MainActor
    func relaunchExactCandidate(
        _ app: XCUIApplication,
        arguments: [String] = []
    ) {
        app.terminate()
        app.launchArguments = arguments
        app.launch()
        XCTAssertTrue(app.buttons["browser.address"].waitForExistence(timeout: 8))
        assertExactCandidateBinding(in: app)
    }

    @MainActor
    func exactCandidateApplication(arguments: [String]) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = arguments
        guard let candidateBinding else {
            XCTFail("The exact-candidate binding must be validated before launching the app.")
            return app
        }
        app.launchEnvironment["AHOI_MOBILE_E2E_EXPECTED_SOURCE_COMMIT"] =
            candidateBinding.sourceCommit
        app.launchEnvironment["AHOI_MOBILE_E2E_EXPECTED_BUILD_MODE"] =
            candidateBinding.buildMode
        app.launchEnvironment["AHOI_MOBILE_E2E_CANDIDATE_RECEIPT_SHA256"] =
            candidateBinding.receiptSHA256
        app.launchEnvironment["AHOI_MOBILE_E2E_EXPECTED_EXECUTABLE_SHA256"] =
            candidateBinding.executableSHA256
        return app
    }

    @MainActor
    func assertExactCandidateBinding(
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        guard let candidateBinding else {
            XCTFail("The runner did not retain its exact-candidate binding.", file: file, line: line)
            return
        }
        let evidence = app.descendants(matching: .any)["browser.e2e.candidate-binding"]
        XCTAssertTrue(
            evidence.waitForExistence(timeout: 5),
            "The launched app must visibly publish its intrinsic Info.plist candidate identity.",
            file: file,
            line: line
        )
        XCTAssertEqual(
            evidence.value as? String,
            candidateBinding.accessibilityValue,
            "The runner receipt binding and the launched app's embedded identity must agree.",
            file: file,
            line: line
        )
    }

    @MainActor
    func navigate(to url: URL, in app: XCUIApplication) {
        openAddressEditor(in: app)
        let field = app.textFields["browser.address.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 3))
        clearAddressEditor(field, in: app)
        enterExactAddress(url.absoluteString, into: field, in: app)
        let navigate = app.buttons["browser.search.navigate"]
        XCTAssertTrue(navigate.waitForExistence(timeout: 3))
        navigate.tap()
        XCTAssertTrue(field.waitForNonExistence(timeout: 5))
        assertAddress(url, containsOrigin: origin(of: url), in: app)
    }

    @MainActor
    func openAddressEditor(in app: XCUIApplication) {
        let address = app.buttons["browser.address"]
        XCTAssertTrue(address.waitForExistence(timeout: 8))
        address.tap()
    }

    @MainActor
    func clearAddressEditor(_ field: XCUIElement, in app: XCUIApplication) {
        let clear = app.buttons["browser.address.clear"]
        if clear.waitForExistence(timeout: 1) {
            clear.tap()
            XCTAssertTrue(
                waitForAddressField(field, toEqual: "", timeout: 2),
                "The address field must be empty before the E2E runner types a destination."
            )
        }
    }

    @MainActor
    func enterExactAddress(
        _ expectedValue: String,
        into field: XCUIElement,
        in app: XCUIApplication
    ) {
        field.tap()
        field.typeText(expectedValue)
        if !waitForAddressField(field, toEqual: expectedValue, timeout: 1) {
            // XCUI occasionally coalesces repeated characters when it injects a
            // complete URL into SwiftUI's selection-aware TextField. Retry via
            // distinct key events so a test-runner input loss cannot be
            // mistaken for an application navigation failure.
            clearAddressEditor(field, in: app)
            field.tap()
            for character in expectedValue {
                field.typeText(String(character))
                Thread.sleep(forTimeInterval: 0.02)
            }
        }
        let exactValueWasEntered = waitForAddressField(
            field,
            toEqual: expectedValue,
            timeout: 2
        )
        let actualValue = field.value as? String ?? ""
        XCTAssertTrue(
            exactValueWasEntered,
            "The E2E runner must inject the exact address before navigation. "
                + "Expected \(expectedValue), got \(actualValue)."
        )
    }

    private func waitForAddressField(
        _ field: XCUIElement,
        toEqual expectedValue: String,
        timeout: TimeInterval
    ) -> Bool {
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", expectedValue),
            object: field
        )
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    @MainActor
    func assertAddress(
        _ expectedURL: URL,
        containsOrigin expectedOrigin: String,
        in app: XCUIApplication,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        let address = app.buttons["browser.address"]
        XCTAssertTrue(address.waitForExistence(timeout: 5), file: file, line: line)
        let expectation = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", expectedURL.absoluteString),
            object: address
        )
        let result = XCTWaiter.wait(for: [expectation], timeout: 8)
        let actualValue = address.value as? String ?? ""
        XCTAssertEqual(
            result,
            .completed,
            "The visible address must equal the navigated URL. Expected \(expectedURL.absoluteString), got \(actualValue).",
            file: file,
            line: line
        )
        XCTAssertTrue(
            (address.value as? String ?? "").contains(expectedOrigin),
            "The visible address must expose the strict HTTPS origin.",
            file: file,
            line: line
        )
    }

    @MainActor
    func requireReachableFixture() async throws -> FixtureContext {
        guard let raw = ProcessInfo.processInfo.environment[Self.baseURLEnvironmentKey],
              !raw.isEmpty else {
            XCTFail(
                "AHOI_MOBILE_REAL_E2E=1 requires an explicit AHOI_MOBILE_E2E_BASE_URL."
            )
            throw MobileRealE2EFixtureContractError.missingBaseURL
        }
        guard let baseURL = URL(string: raw),
              baseURL.scheme?.lowercased() == "https",
              let host = baseURL.host?.lowercased(),
              host == "localhost" || host.hasSuffix(".localhost"),
              baseURL.user == nil,
              baseURL.password == nil,
              baseURL.query == nil,
              baseURL.fragment == nil,
              baseURL.path.isEmpty || baseURL.path == "/",
              let canonicalBaseURL = URL(string: "/", relativeTo: baseURL)?.absoluteURL else {
            XCTFail(
                "AHOI_MOBILE_E2E_BASE_URL must be a credential-free local HTTPS origin."
            )
            throw MobileRealE2EFixtureContractError.invalidBaseURL
        }

        let fixture = FixtureContext(baseURL: canonicalBaseURL)
        var request = URLRequest(url: fixture.url(path: "/__fixture/health"))
        request.timeoutInterval = 3
        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await URLSession.shared.data(for: request)
        } catch {
            XCTFail(
                "The local HTTPS fixture or its explicit test CA trust is unavailable: "
                    + error.localizedDescription
            )
            throw MobileRealE2EFixtureContractError.unreachableOrUntrusted
        }
        guard (response as? HTTPURLResponse)?.statusCode == 200 else {
            XCTFail("The local HTTPS fixture health endpoint did not return HTTP 200.")
            throw MobileRealE2EFixtureContractError.unhealthy
        }
        let payload: [String: Any]
        do {
            guard let decoded = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                XCTFail("The local HTTPS fixture health payload must be a JSON object.")
                throw MobileRealE2EFixtureContractError.unhealthy
            }
            payload = decoded
        } catch let contractError as MobileRealE2EFixtureContractError {
            throw contractError
        } catch {
            XCTFail("The local HTTPS fixture health payload is invalid JSON.")
            throw MobileRealE2EFixtureContractError.unhealthy
        }
        guard payload["ready"] as? Bool == true else {
            XCTFail("The local HTTPS fixture health contract is reachable but not ready.")
            throw MobileRealE2EFixtureContractError.unhealthy
        }
        guard let version = payload["mobileRealE2EContractVersion"] as? NSNumber,
              version.intValue == Self.requiredFixtureContractVersion else {
            XCTFail(
                "The reachable HTTPS fixture does not implement mobile E2E contract "
                    + "v\(Self.requiredFixtureContractVersion); refusing stale runtime evidence."
            )
            throw MobileRealE2EFixtureContractError.incompatibleVersion
        }
        return fixture
    }

    func origin(of url: URL) -> String {
        guard let host = url.host else { return url.absoluteString }
        guard let port = url.port else { return host }
        return "\(host):\(port)"
    }
}

private enum MobileRealE2EFixtureContractError: Error {
    case missingBaseURL
    case invalidBaseURL
    case unreachableOrUntrusted
    case unhealthy
    case incompatibleVersion
}

private struct MobileRealE2ECandidateBinding {
    let sourceCommit: String
    let buildMode: String
    let receiptSHA256: String
    let executableSHA256: String

    init(
        environment: [String: String],
        sourceCommitKey: String,
        buildModeKey: String,
        receiptSHAKey: String,
        receiptPayloadKey: String
    ) throws {
        sourceCommit = try Self.requiredValue(sourceCommitKey, in: environment)
        buildMode = try Self.requiredValue(buildModeKey, in: environment)
        receiptSHA256 = try Self.requiredValue(receiptSHAKey, in: environment)
        guard Self.matches(sourceCommit, pattern: #"[0-9a-f]{40}"#) else {
            throw MobileRealE2ECandidateBindingError.invalidSourceCommit
        }
        guard Self.matches(buildMode, pattern: #"[A-Za-z][A-Za-z0-9_-]{0,63}"#) else {
            throw MobileRealE2ECandidateBindingError.invalidBuildMode
        }
        guard Self.matches(receiptSHA256, pattern: #"[0-9a-f]{64}"#) else {
            throw MobileRealE2ECandidateBindingError.invalidReceiptSHA256
        }
        let encodedReceipt = try Self.requiredValue(receiptPayloadKey, in: environment)
        guard let receiptData = Data(base64Encoded: encodedReceipt) else {
            throw MobileRealE2ECandidateBindingError.invalidReceiptPayload
        }
        let actualReceiptSHA = SHA256.hash(data: receiptData)
            .map { String(format: "%02x", $0) }
            .joined()
        guard actualReceiptSHA == receiptSHA256 else {
            throw MobileRealE2ECandidateBindingError.receiptDigestMismatch
        }
        let receipt = try Self.validatedReceipt(receiptData)
        guard receipt.sourceCommit == sourceCommit,
              receipt.buildMode == buildMode else {
            throw MobileRealE2ECandidateBindingError.receiptIdentityMismatch
        }
        executableSHA256 = receipt.executableSHA256
    }

    var accessibilityValue: String {
        "bundleId=app.ahoibrowser.AhoiBrowser;sourceCommit=\(sourceCommit);"
            + "buildMode=\(buildMode);receiptSha256=\(receiptSHA256);"
            + "executableSha256=\(executableSHA256)"
    }

    private static func requiredValue(
        _ key: String,
        in environment: [String: String]
    ) throws -> String {
        guard let value = environment[key],
              !value.isEmpty,
              value == value.trimmingCharacters(in: .whitespacesAndNewlines) else {
            throw MobileRealE2ECandidateBindingError.missingOrUntrimmed(key)
        }
        return value
    }

    private static func matches(_ value: String, pattern: String) -> Bool {
        value.range(of: "^(?:\(pattern))$", options: .regularExpression) != nil
    }

    private static func validatedReceipt(_ data: Data) throws -> ReceiptIdentity {
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              root["schemaVersion"] as? Int == 1,
              root["kind"] as? String == "simulator-candidate-binding",
              root["sourceDirty"] as? Bool == false,
              root["bundleId"] as? String == "app.ahoibrowser.AhoiBrowser",
              root["platform"] as? String == "iphonesimulator",
              let sourceCommit = root["sourceCommit"] as? String,
              let buildMode = root["buildMode"] as? String,
              let hashes = root["hashes"] as? [String: Any],
              let executableSHA256 = hashes["binarySha256"] as? String,
              matches(sourceCommit, pattern: #"[0-9a-f]{40}"#),
              matches(buildMode, pattern: #"[A-Za-z][A-Za-z0-9_-]{0,63}"#),
              matches(executableSHA256, pattern: #"[0-9a-f]{64}"#) else {
            throw MobileRealE2ECandidateBindingError.invalidReceiptPayload
        }
        return ReceiptIdentity(
            sourceCommit: sourceCommit,
            buildMode: buildMode,
            executableSHA256: executableSHA256
        )
    }

    private struct ReceiptIdentity {
        let sourceCommit: String
        let buildMode: String
        let executableSHA256: String
    }
}

private enum MobileRealE2ECandidateBindingError: LocalizedError {
    case missingOrUntrimmed(String)
    case invalidSourceCommit
    case invalidBuildMode
    case invalidReceiptSHA256
    case invalidReceiptPayload
    case receiptDigestMismatch
    case receiptIdentityMismatch

    var errorDescription: String? {
        switch self {
        case .missingOrUntrimmed(let key):
            "Missing or untrimmed runner environment value \(key)."
        case .invalidSourceCommit:
            "Expected source commit must be a lowercase 40-character Git SHA."
        case .invalidBuildMode:
            "Expected build mode has an invalid format."
        case .invalidReceiptSHA256:
            "Candidate receipt hash must be a lowercase 64-character SHA-256."
        case .invalidReceiptPayload:
            "Candidate receipt payload is missing or violates the simulator receipt contract."
        case .receiptDigestMismatch:
            "Candidate receipt payload does not match its declared SHA-256."
        case .receiptIdentityMismatch:
            "Candidate receipt identity differs from the runner's expected source or build mode."
        }
    }
}

enum MobileUIAcceptanceContract {
    private static let requiredIPadEnvironmentKey =
        "AHOI_MOBILE_REQUIRE_IPAD_ACCEPTANCE"

    @MainActor
    static func requireRegularWidthIPad(
        app: XCUIApplication,
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) throws {
        let isRegularWidthIPad = UIDevice.current.userInterfaceIdiom == .pad
            && app.frame.width >= 700
        guard !isRegularWidthIPad else { return }
        guard environment[requiredIPadEnvironmentKey] != "1" else {
            XCTFail(
                "Explicit iPad acceptance requires a regular-width iPad destination; "
                    + "the active destination is not a regular-width iPad."
            )
            throw MobileUIAcceptanceContractError.wrongIPadDestination
        }
        throw XCTSkip(
            "Regular-width Workspace Canvas is iPad-only; set "
                + "\(requiredIPadEnvironmentKey)=1 for the required acceptance gate."
        )
    }

    static func switchIsOn(_ element: XCUIElement) -> Bool? {
        switch element.value {
        case let value as String:
            switch value.lowercased() {
            case "1", "true", "yes", "on": return true
            case "0", "false", "no", "off": return false
            default: return nil
            }
        case let value as NSNumber:
            return value.boolValue
        default:
            return nil
        }
    }

    @MainActor
    static func waitForSwitch(
        _ element: XCUIElement,
        toEqual expected: Bool,
        timeout: TimeInterval
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if switchIsOn(element) == expected { return true }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        return switchIsOn(element) == expected
    }
}

private enum MobileUIAcceptanceContractError: Error {
    case wrongIPadDestination
}

struct FixtureContext {
    let baseURL: URL

    var origin: String {
        guard let host = baseURL.host else { return baseURL.absoluteString }
        guard let port = baseURL.port else { return host }
        return "\(host):\(port)"
    }

    var securityOrigin: String {
        guard let scheme = baseURL.scheme else { return origin }
        return "\(scheme)://\(origin)"
    }

    func url(path: String) -> URL {
        guard let resolved = URL(string: path, relativeTo: baseURL)?.absoluteURL else {
            preconditionFailure(
                "Static E2E fixture path must resolve against its validated base URL."
            )
        }
        return resolved
    }
}
