import Foundation
import XCTest

/// Real rendered Library controls and durable relaunch, not a domain injection.
final class MobileBookmarkRealE2EUITests: MobileBrowserRealE2ETestCase {
    @MainActor
    func testBookmarkFolderPersistsAndOpensWebPage() async throws {
        let fixture = try await requireReachableFixture()
        let token = String(UUID().uuidString.lowercased().prefix(8))
        let folderName = "Bookmark QA \(token)"
        let pageName = "Webpage \(token)"
        let pageURL = fixture.url(path: "/navigation?bookmark=\(token)")
        let app = launchExactCandidate(arguments: ["-AhoiUITestFixture"])
        var folderIdentifier: String?
        defer {
            if let folderIdentifier { removeTestFolder(folderIdentifier, in: app) }
            app.terminate()
        }

        openBookmarks(in: app)
        let enable = app.buttons["bookmark.sync.enable"]
        XCTAssertTrue(enable.waitForExistence(timeout: 4))
        XCTAssertFalse(enable.isEnabled,
                       "DebugLocal must not fabricate an entitled global Sync configuration.")
        openMobileRoot(in: app)
        openCreation("bookmark.add-folder", in: app)
        enter(folderName, field: "bookmark.editor.title", in: app)
        saveEditor(in: app)
        let folder = bookmarkRow(named: folderName, in: app)
        XCTAssertTrue(folder.waitForExistence(timeout: 5))
        folderIdentifier = folder.identifier
        folder.tap()

        openCreation("bookmark.add", in: app)
        enter(pageName, field: "bookmark.editor.title", in: app)
        enter(pageURL.absoluteString, field: "bookmark.editor.url", in: app)
        saveEditor(in: app)
        let page = bookmarkRow(named: pageName, in: app)
        XCTAssertTrue(page.waitForExistence(timeout: 5))
        let pageIdentifier = page.identifier
        attachScreenshot(named: "Bookmark stored in separate native Library", of: app)

        relaunchExactCandidate(app)
        openBookmarks(in: app)
        openMobileRoot(in: app)
        let restoredFolder = app.buttons[try XCTUnwrap(folderIdentifier)]
        XCTAssertTrue(restoredFolder.waitForExistence(timeout: 5))
        restoredFolder.tap()
        let restoredPage = app.buttons[pageIdentifier]
        XCTAssertTrue(restoredPage.waitForExistence(timeout: 5),
                       "Both bookmark identities and the folder link survive relaunch.")
        restoredPage.tap()
        XCTAssertTrue(app.buttons["bookmark.done"].waitForNonExistence(timeout: 5))
        assertAddress(pageURL, containsOrigin: fixture.origin, in: app)
        XCTAssertTrue(app.webViews.staticTexts["Redirect and popup controls"]
            .waitForExistence(timeout: 8))
        attachScreenshot(named: "Bookmark activated through the real WebKit page", of: app)
    }

    @MainActor
    private func openBookmarks(in app: XCUIApplication) {
        if app.buttons["bookmark.done"].exists { return }
        if !app.buttons["bookmark.library.open"].exists {
            let more = app.buttons["browser.more"]
            XCTAssertTrue(more.waitForExistence(timeout: 5))
            more.tap()
            let library = app.buttons["browser.actions.workspaces"]
            XCTAssertTrue(library.waitForExistence(timeout: 4))
            library.tap()
        }
        let bookmarks = app.buttons["bookmark.library.open"]
        XCTAssertTrue(bookmarks.waitForExistence(timeout: 5))
        bookmarks.tap()
        XCTAssertTrue(app.buttons["bookmark.done"].waitForExistence(timeout: 5))
    }

    @MainActor
    private func openMobileRoot(in app: XCUIApplication) {
        let root = app.buttons["bookmark.root.mobile"]
        XCTAssertTrue(root.waitForExistence(timeout: 5))
        root.tap()
        XCTAssertTrue(app.buttons["bookmark.add-menu"].waitForExistence(timeout: 5))
    }

    @MainActor
    private func openCreation(_ identifier: String, in app: XCUIApplication) {
        let menu = app.buttons["bookmark.add-menu"]
        XCTAssertTrue(menu.waitForExistence(timeout: 5))
        menu.tap()
        let action = app.buttons[identifier]
        XCTAssertTrue(action.waitForExistence(timeout: 4))
        action.tap()
        XCTAssertTrue(app.textFields["bookmark.editor.title"].waitForExistence(timeout: 5))
    }

    @MainActor
    private func enter(_ value: String, field identifier: String, in app: XCUIApplication) {
        let field = app.textFields[identifier]
        XCTAssertTrue(field.waitForExistence(timeout: 4))
        field.tap()
        field.typeText(value)
        XCTAssertEqual(field.value as? String, value,
                       "The visible editor must contain exactly the requested metadata.")
    }

    @MainActor
    private func saveEditor(in app: XCUIApplication) {
        let save = app.buttons["bookmark.editor.save"]
        XCTAssertTrue(save.waitForExistence(timeout: 4))
        XCTAssertTrue(save.isEnabled)
        save.tap()
        XCTAssertTrue(save.waitForNonExistence(timeout: 5))
    }

    @MainActor
    private func bookmarkRow(named title: String, in app: XCUIApplication) -> XCUIElement {
        app.buttons.matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS %@", "bookmark.row.", title
        )).firstMatch
    }

    @MainActor
    private func attachScreenshot(named name: String, of app: XCUIApplication) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    @MainActor
    private func removeTestFolder(_ identifier: String, in app: XCUIApplication) {
        // Restart resets only presentation, not the user's/local Library data.
        relaunchExactCandidate(app)
        openBookmarks(in: app)
        openMobileRoot(in: app)
        let folder = app.buttons[identifier]
        guard folder.waitForExistence(timeout: 4) else { return }
        folder.press(forDuration: 1.1)
        let id = identifier.replacingOccurrences(of: "bookmark.row.", with: "")
        let delete = app.buttons["bookmark.action.delete.\(id)"]
        XCTAssertTrue(delete.waitForExistence(timeout: 4))
        delete.tap()
        // iOS 26 exposes the one rendered SwiftUI confirmation button as
        // parent+child AX buttons. Scope to its visible sheet, then use the
        // first match for that one action instead of requiring a unique node.
        let confirm = app.sheets.buttons.matching(identifier: "bookmark.delete.confirm").firstMatch
        XCTAssertTrue(confirm.waitForExistence(timeout: 4))
        confirm.tap()
        XCTAssertTrue(folder.waitForNonExistence(timeout: 5))
    }
}
