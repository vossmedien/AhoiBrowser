import XCTest
@testable import AhoiCloudKitSpike

/// DoD 14: page, Home and archive targets never carry credentials or a local
/// URL into Sync. Desktop pins the same rule in sync_secret_boundary_unittest.
final class SharedTabTargetValidationTests: XCTestCase {
    func testWebTargetsRejectCredentialsAndNonWebSchemes() {
        let refused = [
            "https://user:secret@example.test/",
            "https://token@example.test/",
            "file:///Users/someone/notes.txt",
            "chrome://settings",
            "javascript:alert(1)",
            "data:text/html,secret",
            "about:blank",
            "https:///no-host",
            "https://example.test/\u{0}nul",
        ]
        for url in refused {
            XCTAssertThrowsError(try SharedTabTarget(kind: .web, url: url), url) { error in
                XCTAssertEqual(error as? SharedTabTargetError, .invalidTarget, url)
            }
        }
        XCTAssertNoThrow(try SharedTabTarget(kind: .web, url: "https://example.test/a?b=c"))
        XCTAssertNoThrow(try SharedTabTarget(kind: .web, url: "http://127.0.0.1:8080/"))
    }

    func testLocalTargetsKeepOnlyTheirSchemeName() {
        XCTAssertNoThrow(try SharedTabTarget(kind: .localOnly, url: "", localScheme: .file))
        XCTAssertThrowsError(
            try SharedTabTarget(kind: .localOnly, url: "file:///Users/someone/notes.txt",
                                localScheme: .file))
        XCTAssertThrowsError(try SharedTabTarget(kind: .localOnly, url: ""))
        XCTAssertThrowsError(
            try SharedTabTarget(kind: .web, url: "https://example.test/", localScheme: .file))
    }

    func testHomeTargetRefusesNewTabAndCredentials() throws {
        XCTAssertNoThrow(try SharedWorkspaceValidation.home(nil))
        XCTAssertNoThrow(
            try SharedWorkspaceValidation.home(SharedTabTarget(kind: .web, url: "https://example.test/")))
        XCTAssertThrowsError(
            try SharedWorkspaceValidation.home(SharedTabTarget(kind: .newTab, url: ""))) { error in
            XCTAssertEqual(error as? SharedWorkspaceValidation.Error, .invalidStructure)
        }
        let decoder = JSONDecoder()
        let credentialHome = Data(#"{"kind":0,"url":"https://user:secret@example.test/"}"#.utf8)
        XCTAssertThrowsError(try decoder.decode(SharedTabTarget.self, from: credentialHome))
    }
}
