import Foundation
import XCTest
@testable import AhoiMobileCore

final class MobileDevelopmentScopeTests: XCTestCase {
    private let scope = "bba96b17-f044-4923-9d40-67b15014d59e"

    private var info: [String: Any] {
        [
            "AhoiBuildMode": "CloudKitDevelopment",
            "AHOI_CLOUDKIT_CONTAINER_ID": "iCloud.app.ahoibrowser.AhoiBrowser",
            "AHOI_CLOUDKIT_ZONE_NAME": "AhoiSyncAcceptance-\(scope)",
            "AHOI_CLOUDKIT_SUBSCRIPTION_ID": "AhoiSyncAcceptanceSubscription-\(scope)",
            "AHOI_SYNC_KEYCHAIN_ACCOUNT": "payload-key.acceptance-\(scope)",
            "AHOI_SYNC_KEYCHAIN_SERVICE": "app.ahoibrowser.sync.payload-key",
            "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP": "248AJ5BN47.app.ahoibrowser.sync",
            "AHOI_SYNC_KEY_VERSION": "1"
        ]
    }

    func testScopeSeparatesPersistentDefaultsAndEveryProductSidecar() throws {
        let value = try XCTUnwrap(MobileDevelopmentScope.resolve(info: info))
        XCTAssertEqual(value.name, scope)
        XCTAssertEqual(value.defaultsSuite, "app.ahoibrowser.AhoiBrowser.acceptance.\(scope)")
        XCTAssertEqual(value.supportDirectory(under: URL(fileURLWithPath: "/tmp/app-support")).path,
                       "/tmp/app-support/AhoiMobile/DevelopmentAcceptance/\(scope)/SyncFormat3")
    }

    func testPartialMismatchedOrProductionScopeCannotFallBackToProductState() {
        for key in info.keys {
            var broken = info
            broken.removeValue(forKey: key)
            XCTAssertThrowsError(try MobileDevelopmentScope.resolve(info: broken), key)
        }
        var production = info
        production["AhoiBuildMode"] = "TestFlightBootstrap"
        XCTAssertThrowsError(try MobileDevelopmentScope.resolve(info: production))
        var otherKey = info
        otherKey["AHOI_SYNC_KEYCHAIN_ACCOUNT"] = "payload-key"
        XCTAssertThrowsError(try MobileDevelopmentScope.resolve(info: otherKey))
    }

    func testOrdinaryUnconfiguredAndPublicProductRemainOrdinary() throws {
        XCTAssertNil(try MobileDevelopmentScope.resolve(info: [:]))
        XCTAssertNil(try MobileDevelopmentScope.resolve(info: [
            "AHOI_CLOUDKIT_ZONE_NAME": "AhoiBrowserSyncV3",
            "AHOI_SYNC_KEYCHAIN_ACCOUNT": "payload-key",
            "AHOI_CLOUDKIT_SUBSCRIPTION_ID": "AhoiBrowserSyncSubscription"
        ]))
    }
}
