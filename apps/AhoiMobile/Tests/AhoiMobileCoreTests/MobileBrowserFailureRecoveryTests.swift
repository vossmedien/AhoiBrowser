import Foundation
import WebKit
import XCTest
@testable import AhoiMobileCore

final class MobileBrowserFailureRecoveryTests: XCTestCase {
    @MainActor
    func testTransportSecurityFailuresStayFailClosedAndUserVisible() {
        let transportSecurityCodes = [
            NSURLErrorAppTransportSecurityRequiresSecureConnection,
            NSURLErrorSecureConnectionFailed,
            NSURLErrorServerCertificateHasBadDate,
            NSURLErrorServerCertificateUntrusted,
            NSURLErrorServerCertificateHasUnknownRoot,
            NSURLErrorServerCertificateNotYetValid,
            NSURLErrorClientCertificateRejected,
            NSURLErrorClientCertificateRequired,
        ]

        for code in transportSecurityCodes {
            let error = NSError(domain: NSURLErrorDomain, code: code)
            XCTAssertEqual(
                MobileBrowserController.navigationFailureClassification(error),
                .transportSecurity,
                "Expected transport-security classification for URL error \(code)"
            )
            XCTAssertEqual(
                MobileBrowserController.classifyNavigationFailure(error),
                .transportSecurity,
                "TLS failures must reach the fail-closed page state"
            )
        }
    }

    @MainActor
    func testFailureClassificationRequiresTheURLErrorDomain() {
        let unrelatedTimeoutCode = NSError(
            domain: NSCocoaErrorDomain,
            code: NSURLErrorTimedOut
        )

        XCTAssertEqual(
            MobileBrowserController.navigationFailureClassification(
                unrelatedTimeoutCode
            ),
            .failed
        )
    }

    @MainActor
    func testDNSFailuresRemainDistinctThroughNestedErrors() {
        for code in [NSURLErrorCannotFindHost, NSURLErrorDNSLookupFailed] {
            let underlying = NSError(
                domain: NSURLErrorDomain,
                code: code,
                userInfo: [NSLocalizedDescriptionKey: "secret internal resolver detail"]
            )
            let wrapped = NSError(
                domain: "app.ahoibrowser.test-wrapper",
                code: 2,
                userInfo: [NSUnderlyingErrorKey: underlying]
            )

            XCTAssertEqual(
                MobileBrowserController.navigationFailureClassification(wrapped),
                .dnsLookupFailed
            )
            XCTAssertEqual(
                MobileBrowserController.classifyNavigationFailure(wrapped),
                .dnsLookupFailed
            )
            XCTAssertFalse(
                MobilePageFailureKind.dnsLookupFailed.localizedDescription
                    .contains("secret internal resolver detail")
            )
        }
    }

    @MainActor
    func testWrappedTransportAndCancellationFailuresAreRecognized() {
        let wrappedTLS = NSError(
            domain: "app.ahoibrowser.test-wrapper",
            code: 1,
            userInfo: [
                NSUnderlyingErrorKey: URLError(.serverCertificateUntrusted),
            ]
        )
        let wrappedCancellation = WebPage.NavigationError.failedProvisionalNavigation(
            URLError(.cancelled)
        )

        XCTAssertEqual(
            MobileBrowserController.navigationFailureClassification(wrappedTLS),
            .transportSecurity
        )
        XCTAssertTrue(
            MobileBrowserController.isNavigationCancellation(wrappedCancellation)
        )
    }

    @MainActor
    func testWebContentRecoveryCanRunOnceUntilNavigationFinishes() {
        var gate = MobileWebContentRecoveryGate()

        XCTAssertFalse(gate.claim(for: .transportSecurity))
        XCTAssertFalse(gate.didAttemptRecovery)
        XCTAssertTrue(gate.claim(for: .webContentTerminated))
        XCTAssertFalse(gate.claim(for: .webContentTerminated))
        XCTAssertFalse(gate.claim(for: .transportSecurity))

        gate.resetAfterFinishedNavigation()

        XCTAssertTrue(gate.claim(for: .webContentTerminated))
    }

    @MainActor
    func testWebContentTerminationMapsToRecoverablePageFailure() {
        let error = WebPage.NavigationError.webContentProcessTerminated

        XCTAssertEqual(
            MobileBrowserController.navigationFailureClassification(error),
            .webContentTerminated
        )
        XCTAssertEqual(
            MobileBrowserController.classifyNavigationFailure(error),
            .webContentTerminated
        )
    }

    @MainActor
    func testRecoveryURLAllowsOnlyValidatedWebLocations() throws {
        var storedTab = MobileTabRecord(url: "https://restore.example/path")
        let liveURL = try XCTUnwrap(URL(string: "https://live.example/current"))
        let unsafeLiveURL = try XCTUnwrap(URL(string: "file:///tmp/private"))

        XCTAssertEqual(
            MobileBrowserController.validatedRecoveryURL(for: storedTab),
            URL(string: "https://restore.example/path")
        )
        XCTAssertEqual(
            MobileBrowserController.validatedRecoveryURL(
                for: storedTab,
                preferredURL: liveURL
            ),
            liveURL
        )
        XCTAssertNil(
            MobileBrowserController.validatedRecoveryURL(
                for: storedTab,
                preferredURL: unsafeLiveURL
            ),
            "An unsafe current page must not silently fall back to stale tab state"
        )

        storedTab.url = "https://user:secret@restore.example"
        XCTAssertNil(MobileBrowserController.validatedRecoveryURL(for: storedTab))
    }

    func testNavigationCallbackValidityRejectsStaleOrDetachedCallbacks() {
        XCTAssertTrue(MobileNavigationCallbackValidity.accepts(
            expectedGeneration: 4,
            currentGeneration: 4,
            tabExists: true,
            pageIsCurrent: true
        ))
        XCTAssertFalse(MobileNavigationCallbackValidity.accepts(
            expectedGeneration: 4,
            currentGeneration: 5,
            tabExists: true,
            pageIsCurrent: true
        ))
        XCTAssertFalse(MobileNavigationCallbackValidity.accepts(
            expectedGeneration: 4,
            currentGeneration: 4,
            tabExists: false,
            pageIsCurrent: true
        ))
        XCTAssertFalse(MobileNavigationCallbackValidity.accepts(
            expectedGeneration: 4,
            currentGeneration: 4,
            tabExists: true,
            pageIsCurrent: false
        ))
    }

    func testExpectedPolicyCancellationResubscribesNavigationObservation() {
        XCTAssertEqual(
            MobileNavigationObservationFailurePolicy.action(
                expectedPolicyCancellation: true,
                isNavigationCancellation: true
            ),
            .resubscribeAfterCancellation
        )
        XCTAssertEqual(
            MobileNavigationObservationFailurePolicy.action(
                expectedPolicyCancellation: false,
                isNavigationCancellation: true
            ),
            .resubscribeAfterCancellation,
            "A superseded or stopped in-page navigation must not orphan observation."
        )
        XCTAssertEqual(
            MobileNavigationObservationFailurePolicy.action(
                expectedPolicyCancellation: false,
                isNavigationCancellation: false
            ),
            .classifyFailure
        )
    }

    func testHTTPFailurePolicyClassifiesOnlyErrorStatusRanges() {
        XCTAssertNil(MobileHTTPFailurePolicy.pageFailureKind(
            for: 200,
            isMainFrame: true
        ))
        XCTAssertNil(MobileHTTPFailurePolicy.pageFailureKind(
            for: 304,
            isMainFrame: true
        ))
        XCTAssertEqual(
            MobileHTTPFailurePolicy.pageFailureKind(
                for: 404,
                isMainFrame: true
            ),
            .httpClientError
        )
        XCTAssertEqual(
            MobileHTTPFailurePolicy.pageFailureKind(
                for: 503,
                isMainFrame: true
            ),
            .httpServerError
        )
        XCTAssertNil(MobileHTTPFailurePolicy.pageFailureKind(
            for: 600,
            isMainFrame: true
        ))
        XCTAssertNil(MobileHTTPFailurePolicy.pageFailureKind(
            for: 404,
            isMainFrame: false
        ))
        XCTAssertNil(MobileHTTPFailurePolicy.pageFailureKind(
            for: 503,
            isMainFrame: false
        ))
    }

    @MainActor
    func testProvisionalDNSFailureUsesDeepestValidatedFailingURL() throws {
        let redirectSource = try XCTUnwrap(URL(
            string: "https://fixture.example/redirect-to-missing"
        ))
        let failedDestination = try XCTUnwrap(URL(
            string: "https://missing-redirect.ahoibrowser.invalid/final"
        ))
        let dnsFailure = NSError(
            domain: NSURLErrorDomain,
            code: NSURLErrorCannotFindHost,
            userInfo: [NSURLErrorFailingURLErrorKey: failedDestination]
        )
        let wrapper = NSError(
            domain: "app.ahoibrowser.test-wrapper",
            code: 9,
            userInfo: [
                NSURLErrorFailingURLErrorKey: redirectSource,
                NSUnderlyingErrorKey: dnsFailure,
            ]
        )
        let provisional = WebPage.NavigationError.failedProvisionalNavigation(wrapper)

        XCTAssertEqual(
            MobileBrowserController.validatedNavigationFailureURL(provisional),
            failedDestination,
            "The redirect destination, not its source or the previous page, must drive recovery."
        )
    }

    @MainActor
    func testNavigationFailureURLRejectsUnsafeLocations() throws {
        let unsafeURL = try XCTUnwrap(URL(string: "file:///private/fixture-secret"))
        let error = NSError(
            domain: NSURLErrorDomain,
            code: NSURLErrorCannotOpenFile,
            userInfo: [NSURLErrorFailingURLErrorKey: unsafeURL]
        )

        XCTAssertNil(MobileBrowserController.validatedNavigationFailureURL(error))
    }

    func testFailedDestinationClearsDocumentMetadataButPreservesCustomTitle() throws {
        let failedDestination = try XCTUnwrap(URL(
            string: "https://missing-link.ahoibrowser.invalid/path"
        ))
        var customNamedTab = MobileTabRecord(
            customTitle: "Named voyage",
            title: "Previous document",
            url: "https://fixture.example/navigation",
            faviconData: Data([0x01]),
            websiteTintARGB: 0xFF12_3456
        )

        XCTAssertTrue(MobileNavigationFailureDestinationPolicy.apply(
            failedDestination,
            to: &customNamedTab
        ))
        XCTAssertEqual(customNamedTab.url, failedDestination.absoluteString)
        XCTAssertEqual(customNamedTab.title, "")
        XCTAssertEqual(customNamedTab.customTitle, "Named voyage")
        XCTAssertEqual(customNamedTab.displayTitle, "Named voyage")
        XCTAssertNil(customNamedTab.faviconData)
        XCTAssertNil(customNamedTab.websiteTintARGB)

        var ordinaryTab = MobileTabRecord(
            title: "Previous document",
            url: "https://fixture.example/navigation"
        )
        XCTAssertTrue(MobileNavigationFailureDestinationPolicy.apply(
            failedDestination,
            to: &ordinaryTab
        ))
        XCTAssertEqual(ordinaryTab.title, "")
        XCTAssertNil(ordinaryTab.customTitle)
        XCTAssertEqual(ordinaryTab.displayTitle, "missing-link.ahoibrowser.invalid")
    }
}
