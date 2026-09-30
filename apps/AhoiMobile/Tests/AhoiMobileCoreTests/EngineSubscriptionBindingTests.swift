import Foundation
import XCTest
@testable import AhoiMobileCore

#if canImport(CloudKit)
import CloudKit

/// Archives under CloudKit's class name with the same keys as a real
/// CKSyncEngineState.
@objc(AhoiTestEngineState)
private final class TestEngineState: NSObject, NSSecureCoding {
    static var supportsSecureCoding: Bool { true }

    let existing: String?
    let token: String?
    let echo: String?
    let needsSave: Bool

    init(existing: String?, needsSave: Bool, echo: String? = nil) {
        self.existing = existing
        self.token = "database-token-0001"
        self.echo = echo
        self.needsSave = needsSave
    }

    init?(coder: NSCoder) {
        existing = coder.decodeObject(
            of: NSString.self, forKey: "existingDatabaseSubscriptionID"
        ) as String?
        token = coder.decodeObject(
            of: NSString.self, forKey: "serverChangeTokenForDatabase"
        ) as String?
        echo = coder.decodeObject(of: NSString.self, forKey: "echo") as String?
        needsSave = coder.decodeBool(forKey: "needsToSaveDatabaseSubscription")
    }

    func encode(with coder: NSCoder) {
        coder.encode(existing, forKey: "existingDatabaseSubscriptionID")
        coder.encode(token, forKey: "serverChangeTokenForDatabase")
        coder.encode(echo, forKey: "echo")
        coder.encode(needsSave, forKey: "needsToSaveDatabaseSubscription")
    }
}

@available(iOS 17.0, macOS 14.0, *)
final class EngineSubscriptionBindingTests: XCTestCase {
    private let foreign =
        "app.ahoibrowser.AhoiBrowser.cloudkit-e2e.7e6bb1c73e544812ad24e816b486bc25"
    private let configured =
        "AhoiSyncAcceptanceSubscription-23855a90-ee61-499e-abed-bfdc52a881d7"

    private func archive(_ state: TestEngineState) -> Data {
        let archiver = NSKeyedArchiver(requiringSecureCoding: true)
        archiver.setClassName("CKSyncEngineState", for: TestEngineState.self)
        archiver.encode(state, forKey: NSKeyedArchiveRootObjectKey)
        archiver.finishEncoding()
        return archiver.encodedData
    }

    private func serialization(
        _ inner: Data
    ) throws -> CKSyncEngine.State.Serialization {
        let json = try JSONSerialization.data(
            withJSONObject: ["data": inner.base64EncodedString()]
        )
        return try JSONDecoder().decode(
            CKSyncEngine.State.Serialization.self, from: json
        )
    }

    private func state(
        _ existing: String?, needsSave: Bool = false, echo: String? = nil
    ) throws -> CKSyncEngine.State.Serialization {
        try serialization(archive(
            TestEngineState(existing: existing, needsSave: needsSave, echo: echo)
        ))
    }

    private func decode(
        _ serialization: CKSyncEngine.State.Serialization
    ) throws -> TestEngineState? {
        let json = try JSONEncoder().encode(serialization)
        let fields = try XCTUnwrap(
            JSONSerialization.jsonObject(with: json) as? [String: String]
        )
        let inner = try XCTUnwrap(Data(base64Encoded: try XCTUnwrap(fields["data"])))
        let unarchiver = try NSKeyedUnarchiver(forReadingFrom: inner)
        unarchiver.setClass(TestEngineState.self, forClassName: "CKSyncEngineState")
        return unarchiver.decodeObject(
            of: TestEngineState.self, forKey: NSKeyedArchiveRootObjectKey
        )
    }

    func testReadsTheAdoptedSubscription() throws {
        let remembered = try XCTUnwrap(EngineSubscriptionBinding.read(try state(foreign)))
        XCTAssertEqual(remembered, .init(remembered: foreign, needsSave: false))
        XCTAssertTrue(
            EngineSubscriptionBinding.shouldRebind(remembered, configured: configured)
        )
    }

    func testRebindsToConfiguredAndKeepsEverythingElse() throws {
        let rebound = try XCTUnwrap(
            EngineSubscriptionBinding.rebound(try state(foreign), to: configured)
        )
        XCTAssertEqual(
            EngineSubscriptionBinding.read(rebound),
            .init(remembered: configured, needsSave: true)
        )
        // A second load is a no-op, so the migration runs once.
        XCTAssertNil(EngineSubscriptionBinding.rebound(rebound, to: configured))
        let decoded = try XCTUnwrap(decode(rebound))
        XCTAssertEqual(decoded.existing, configured)
        XCTAssertEqual(decoded.token, "database-token-0001")
        XCTAssertTrue(decoded.needsSave)
    }

    func testLeavesMatchingOrUnsetStateAlone() throws {
        XCTAssertNil(EngineSubscriptionBinding.rebound(try state(configured), to: configured))
        // Nothing adopted yet: the engine saves the configured ID by itself.
        let fresh = try state(nil, needsSave: true)
        XCTAssertEqual(
            EngineSubscriptionBinding.read(fresh), .init(remembered: "", needsSave: true)
        )
        XCTAssertNil(EngineSubscriptionBinding.rebound(fresh, to: configured))
        // No configured ID: the engine's own choice stands.
        XCTAssertNil(EngineSubscriptionBinding.rebound(try state(foreign), to: nil))
        XCTAssertNil(EngineSubscriptionBinding.rebound(try state(foreign), to: ""))
    }

    func testRefusesAmbiguousOrUnknownArchives() throws {
        // A second, separately archived copy of the old ID might mean
        // something else; the state is left untouched.
        let ambiguous = try state(
            String(foreign.map { $0 }), echo: String(foreign.map { $0 })
        )
        XCTAssertNotNil(EngineSubscriptionBinding.read(ambiguous))
        XCTAssertNil(EngineSubscriptionBinding.rebound(ambiguous, to: configured))

        let array = try NSKeyedArchiver.archivedData(
            withRootObject: [foreign] as NSArray, requiringSecureCoding: true
        )
        XCTAssertNil(EngineSubscriptionBinding.read(try serialization(array)))
        XCTAssertNil(
            EngineSubscriptionBinding.rebound(try serialization(array), to: configured)
        )
    }
}

#endif
