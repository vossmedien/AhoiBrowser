import XCTest
@testable import AhoiMobileCore

final class MobileTabReorderTests: XCTestCase {
    @MainActor
    func testPrivateTabsUseTheSameDeterministicReorderContract() throws {
        let browser = MobileBrowserController()
        let first = browser.createTab(
            url: try XCTUnwrap(URL(string: "https://private-one.example")),
            mode: .privateBrowsing
        )
        let second = browser.createTab(
            url: try XCTUnwrap(URL(string: "https://private-two.example")),
            mode: .privateBrowsing
        )
        let third = browser.createTab(
            url: try XCTUnwrap(URL(string: "https://private-three.example")),
            mode: .privateBrowsing
        )

        browser.reorderTabs(
            browser.privateTabs.map(\.id),
            fromOffsets: IndexSet(integer: 0),
            toOffset: 3
        )

        XCTAssertEqual(browser.privateTabs.map(\.id), [second, third, first])
    }
}
