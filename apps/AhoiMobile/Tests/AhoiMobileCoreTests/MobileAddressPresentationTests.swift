import Foundation
import XCTest
@testable import AhoiMobileCore

final class MobileAddressPresentationTests: XCTestCase {
    func testFailedLoadPresentsTheTabsCommittedURL() {
        let committed = "https://committed.example/page"
        let live = URL(string: "https://live.example/other")
        XCTAssertEqual(
            MobileAddressPresentation.addressURL(pageFailed: true, tabURL: committed, pageURL: live),
            URL(string: committed)
        )
        XCTAssertEqual(
            MobileAddressPresentation.addressURL(pageFailed: false, tabURL: committed, pageURL: live),
            live
        )
        XCTAssertEqual(
            MobileAddressPresentation.addressURL(pageFailed: false, tabURL: committed, pageURL: nil),
            URL(string: committed)
        )
        XCTAssertNil(MobileAddressPresentation.addressURL(pageFailed: false, tabURL: nil, pageURL: nil))
    }

    func testNormalAddressShowsTheOriginWithItsPort() {
        let presentation = MobileAddressPresentation(
            isPrivate: false,
            url: URL(string: "https://example.test:8443/path?query=1")
        )
        XCTAssertEqual(presentation.originHost, "example.test:8443")
        XCTAssertEqual(presentation.label, "example.test:8443")
        XCTAssertEqual(presentation.accessibilityValue, "https://example.test:8443/path?query=1")
    }

    func testPrivateAddressNamesOnlyTheOrigin() {
        let presentation = MobileAddressPresentation(
            isPrivate: true,
            url: URL(string: "https://private.example/secret/path?token=abc")
        )
        for text in [presentation.label, presentation.accessibilityValue] {
            XCTAssertTrue(text.contains("private.example"), text)
            XCTAssertFalse(text.contains("secret"), text)
            XCTAssertFalse(text.contains("token"), text)
        }
    }

    func testOnlyHTTPSShowsTheLock() {
        XCTAssertEqual(
            MobileAddressPresentation(isPrivate: false, url: URL(string: "https://a.example")).securitySymbol,
            "lock.fill"
        )
        XCTAssertEqual(
            MobileAddressPresentation(isPrivate: false, url: URL(string: "http://a.example")).securitySymbol,
            "globe"
        )
        XCTAssertEqual(MobileAddressPresentation(isPrivate: false, url: nil).securitySymbol, "globe")
    }
}
