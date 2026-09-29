import XCTest
import WebKit
@testable import AhoiMobileCore

/// Regression for the Peek-adopt crash (29 September 2026): WebKit's SwiftUI
/// `WebView` traps (EXC_BREAKPOINT in `makeViewProvider`) when a `WebPage`
/// gets a second `WebView` while the preview's view still shows it. Adoption
/// must therefore close the preview first and only then hand the page to a tab.
final class MobileLinkPreviewAdoptionTests: XCTestCase {
    @MainActor
    private func presentPreview(
        in browser: MobileBrowserController
    ) throws -> (source: UUID, preview: MobileLinkPreviewSession) {
        let source = browser.createTab(mode: .normal)
        browser.selectedTabID = source
        let link = MobilePendingLink(
            url: try XCTUnwrap(URL(string: "https://example.test/peek")),
            sourceTabID: source,
            sourceOrigin: "https://fixture.example.test",
            workspaceID: browser.tabs.first { $0.id == source }?.workspaceID,
            sourceMode: .normal
        )
        browser.pendingLink = link
        XCTAssertTrue(browser.stagePendingLinkPreview(requestID: link.id))
        browser.presentStagedLinkPreview()
        return (source, try XCTUnwrap(browser.linkPreview))
    }

    @MainActor
    func testAdoptionClosesPreviewBeforeTheTabGetsThePage() throws {
        let browser = MobileBrowserController()
        let (source, preview) = try presentPreview(in: browser)
        let tabsBefore = browser.tabs.count

        XCTAssertTrue(browser.adoptLinkPreview(id: preview.id))
        XCTAssertNil(browser.linkPreview, "Adopting must close the preview first.")
        XCTAssertEqual(browser.tabs.count, tabsBefore, "No tab may show the page yet.")
        XCTAssertNil(browser.pages[preview.id], "The page must not reach a tab view yet.")
        XCTAssertEqual(browser.selectedTabID, source)

        // The preview view's own `onDisappear` cleanup must not discard it.
        browser.dismissLinkPreview(id: preview.id)

        let adopted = try XCTUnwrap(browser.completeStagedLinkPreviewAdoption())
        XCTAssertEqual(adopted, preview.id)
        XCTAssertEqual(browser.tabs.count, tabsBefore + 1)
        XCTAssertTrue(browser.pages[adopted] === preview.page, "The loaded page is reused.")
        XCTAssertEqual(browser.selectedTabID, adopted)
        XCTAssertEqual(browser.tabs.first { $0.id == adopted }?.mode, .normal)
        XCTAssertNil(browser.completeStagedLinkPreviewAdoption(), "Completion runs once.")
    }

    @MainActor
    func testClosedSourceTabDropsStagedAdoption() throws {
        let browser = MobileBrowserController()
        let (source, preview) = try presentPreview(in: browser)
        XCTAssertTrue(browser.adoptLinkPreview(id: preview.id))

        // Closing the only tab leaves a fresh replacement tab behind.
        browser.close(source)
        XCTAssertNil(browser.completeStagedLinkPreviewAdoption())
        XCTAssertFalse(browser.tabs.contains { $0.id == preview.id })
        XCTAssertNil(browser.pages[preview.id])
    }

    @MainActor
    func testClosingPreviewWithoutAdoptionCreatesNoTab() throws {
        let browser = MobileBrowserController()
        let (_, preview) = try presentPreview(in: browser)
        let tabsBefore = browser.tabs.count
        browser.dismissLinkPreview(id: preview.id)
        XCTAssertNil(browser.linkPreview)
        XCTAssertNil(browser.completeStagedLinkPreviewAdoption())
        XCTAssertEqual(browser.tabs.count, tabsBefore)
    }
}
