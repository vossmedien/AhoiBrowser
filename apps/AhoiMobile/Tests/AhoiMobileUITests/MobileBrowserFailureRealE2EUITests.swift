import Foundation
import XCTest

final class MobileBrowserFailureRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testTLSHostnameMismatchFailsClosedAndRetriesTheExactDestination() async throws {
        let fixture = try await requireReachableFixture()
        let failureURL = try XCTUnwrap(tlsHostnameMismatchURL(from: fixture.baseURL))
        let app = coldLaunchApplication()

        navigate(to: failureURL, in: app)

        let failure = app.descendants(matching: .any)["browser.page-failure"]
        XCTAssertTrue(
            failure.waitForExistence(timeout: 12),
            "A real hostname mismatch must replace web content with Ahoi's TLS failure surface."
        )
        let title = failure.descendants(matching: .any).matching(NSPredicate(
            format: "label IN %@",
            ["Secure Connection Failed", "Sichere Verbindung fehlgeschlagen"]
        )).firstMatch
        XCTAssertTrue(title.waitForExistence(timeout: 3))
        assertAddress(
            failureURL,
            containsOrigin: origin(of: failureURL),
            in: app
        )

        let retry = app.buttons["browser.retry"]
        XCTAssertTrue(retry.waitForExistence(timeout: 3))
        XCTAssertTrue(retry.isHittable)
        retry.tap()

        let retrying = app.descendants(matching: .any)["browser.page-retrying"]
        XCTAssertTrue(
            retrying.exists,
            "Retry must visibly acknowledge the renewed TLS navigation attempt."
        )
        XCTAssertTrue(retrying.waitForNonExistence(timeout: 5))
        XCTAssertTrue(
            failure.waitForExistence(timeout: 12),
            "Retry must fail closed again instead of bypassing the certificate mismatch."
        )
        assertAddress(
            failureURL,
            containsOrigin: origin(of: failureURL),
            in: app
        )
    }

    private func tlsHostnameMismatchURL(from trustedBaseURL: URL) -> URL? {
        guard var components = URLComponents(
            url: trustedBaseURL,
            resolvingAgainstBaseURL: false
        ) else {
            return nil
        }
        components.host = "wrong-host.localhost"
        components.path = "/"
        components.query = nil
        components.fragment = nil
        return components.url
    }
}
