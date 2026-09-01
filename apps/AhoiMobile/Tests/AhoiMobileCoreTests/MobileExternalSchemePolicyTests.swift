import XCTest
@testable import AhoiMobileCore

final class MobileExternalSchemePolicyTests: XCTestCase {
    func testNavigationTargetPolicyAllowsSafeWebAndExplicitAppSchemes() throws {
        let webURL = try XCTUnwrap(URL(string: "https://example.com/path"))
        XCTAssertEqual(MobileNavigationTargetPolicy.decide(webURL), .web(webURL))

        for value in [
            "mailto:browser-test@example.com?subject=Ahoi",
            "tel:+49123456789",
            "sms:+49123456789",
            "facetime:browser-test@example.com",
            "facetime-audio:browser-test@example.com",
        ] {
            let url = try XCTUnwrap(URL(string: value))
            XCTAssertEqual(
                MobileNavigationTargetPolicy.decide(url),
                .externalApp(url),
                value
            )
        }
    }

    func testNavigationTargetPolicyRejectsLocalScriptCredentialAndUnknownSchemes() throws {
        for value in [
            "file:///tmp/private",
            "javascript:alert(1)",
            "data:text/html,hello",
            "https://user:secret@example.com/path",
            "unknown-app:payload",
            "shell:whoami",
        ] {
            let url = try XCTUnwrap(URL(string: value))
            XCTAssertEqual(MobileNavigationTargetPolicy.decide(url), .blocked, value)
        }
    }

    @MainActor
    func testControllerRevalidatesPendingExternalAppBeforeHandoff() throws {
        let browser = MobileBrowserController()
        let tabID = browser.createTab(url: try XCTUnwrap(URL(string: "https://example.com")))
        let unsafe = MobilePendingExternalOpen(
            url: try XCTUnwrap(URL(string: "unknown-app:payload")),
            origin: "https://example.com",
            sourceTabID: tabID
        )
        browser.pendingExternalOpen = unsafe

        XCTAssertNil(browser.confirmPendingExternalOpen(requestID: unsafe.id))
        XCTAssertNil(browser.pendingExternalOpen)

        let safe = MobilePendingExternalOpen(
            url: try XCTUnwrap(URL(string: "mailto:browser-test@example.com")),
            origin: "https://example.com",
            sourceTabID: tabID
        )
        browser.pendingExternalOpen = safe

        XCTAssertEqual(browser.confirmPendingExternalOpen(requestID: safe.id), safe.url)
        XCTAssertNil(browser.pendingExternalOpen)
    }

}
