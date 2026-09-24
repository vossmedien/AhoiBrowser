import CryptoKit
import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// The shared-tab checks consume the current single Format-3 golden resource.
/// They do not activate Sync, grant consent, or construct a CloudKit account.
final class SharedTabFrozenContractTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()

    func testCurrentGoldenAndTargetKindsKeepLocalOnlyTargetsLocal() throws {
        let (data, fixture) = try UnifiedSyncFixture.load()
        XCTAssertEqual(SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined(),
                       UnifiedSyncFixture.sha256)
        XCTAssertEqual(fixture.model_version, 3)
        XCTAssertEqual(SharedTabWireReadPolicy.defaultWriteVersion, 3)

        let cases: [(String, SharedTabTargetKind, Bool)] = [
            ("tree_saved_web", .web, true),
            ("tree_temporary_new_tab", .newTab, true),
            ("tree_saved_local_only_file", .localOnly, false),
        ]
        for (name, kind, canActivate) in cases {
            let sample = try XCTUnwrap(fixture.records.first { $0.name == name })
            let node = try codec.decodeTreeNode(UnifiedSyncFixture.envelope(sample), plaintext: sample.data)
            let row = try XCTUnwrap(MobileSharedTabProjection(node), name)
            XCTAssertEqual(row.target.kind, kind, name)
            XCTAssertEqual(row.canActivateHere, canActivate, name)
            XCTAssertEqual(try codec.encode(node), sample.data, name)
            if kind == .localOnly {
                XCTAssertNil(node.url)
                XCTAssertEqual(row.target.url, "")
                XCTAssertEqual(row.localRuntimeURL(preservingOwnedTarget: "file:///owned"), "file:///owned")
                XCTAssertNil(row.localRuntimeURL(preservingOwnedTarget: nil))
            }
        }
    }

    func testOldVersionAndMissingCreationClockFailClosed() throws {
        let (_, fixture) = try UnifiedSyncFixture.load()
        let sample = try XCTUnwrap(fixture.records.first { $0.name == "tree_saved_web" })
        var payload = try UnifiedSyncFixture.object(sample.data)
        payload["model_version"] = 2
        payload["version_model"] = 2
        XCTAssertThrowsError(try codec.decodeTreeNode(UnifiedSyncFixture.envelope(sample),
                                                      plaintext: UnifiedSyncFixture.canonical(payload)))

        payload = try UnifiedSyncFixture.object(sample.data)
        var fields = try XCTUnwrap(payload["field_versions"] as? [String: Any])
        fields.removeValue(forKey: "created_at")
        payload["field_versions"] = fields
        XCTAssertThrowsError(try codec.decodeTreeNode(UnifiedSyncFixture.envelope(sample),
                                                      plaintext: UnifiedSyncFixture.canonical(payload)))
    }

    func testPeerCapabilityGateDoesNotIgnoreMissingOrUnverifiedPeers() throws {
        let (_, fixture) = try UnifiedSyncFixture.load()
        func sample(_ name: String) throws -> UnifiedSyncFixture.Sample {
            try XCTUnwrap(fixture.records.first { $0.name == name })
        }
        let macSample = try sample("device_mac")
        let phoneSample = try sample("device_iphone")
        let mac = try codec.decodeDevice(UnifiedSyncFixture.envelope(macSample), plaintext: macSample.data)
        let phone = try codec.decodeDevice(UnifiedSyncFixture.envelope(phoneSample), plaintext: phoneSample.data)
        let devices = [mac.id: mac, phone.id: phone]
        let capabilities = try ["device_capability_mac", "device_capability_iphone"].map { name in
            let value = try sample(name)
            return try codec.decodeCapability(UnifiedSyncFixture.envelope(value),
                                              plaintext: value.data, knownDevices: devices)
        }
        let peers: [SharedTabCapabilityReadiness.Peer] = [
            .init(deviceID: mac.id), .init(deviceID: phone.id),
        ]
        func assess(_ roster: [SharedTabCapabilityReadiness.Peer],
                    _ declarations: [DeviceCapabilityRecord],
                    initialFetch: Bool = true) -> SharedTabCapabilityReadiness.Assessment {
            SharedTabCapabilityReadiness.evaluate(
                localDevice: mac.id, peers: roster, capabilities: declarations,
                globalSyncEnabled: true, normalProfile: true,
                initialFetchComplete: initialFetch, localDeviceAcknowledged: true,
                localCapabilityAcknowledged: true
            )
        }

        XCTAssertTrue(assess(peers, capabilities).isReady)
        let missing = assess(peers, [capabilities[0]])
        XCTAssertFalse(missing.isReady)
        XCTAssertEqual(missing.blockingDevices, [phone.id])
        XCTAssertTrue(assess(peers, capabilities, initialFetch: false).bootstrapIncomplete)

        var unverified = peers
        unverified[1].independentlyKnown = false
        XCTAssertFalse(assess(unverified, capabilities).isReady)
        var retired = peers
        retired[1].explicitlyRetired = true
        XCTAssertTrue(assess(retired, [capabilities[0]]).isReady)
    }
}
