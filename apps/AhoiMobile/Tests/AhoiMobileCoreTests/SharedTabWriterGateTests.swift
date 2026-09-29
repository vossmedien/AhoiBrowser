import Foundation
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

/// Mobile capability announcement and shared-tab writer gate, checked against
/// the desktop Format-3 golden (`sync_wire_v3.json`) and the desktop semantics
/// of `ProfileSyncBackend::PublishLocalCapability` / `SharedTabState()`.
final class SharedTabWriterGateTests: XCTestCase {
    private let codec = DesktopWirePayloadCodec()
    private let fixturePhone = DeviceID(rawValue: UUID(uuidString: "a0000000-0000-4000-8000-000000000002")!)

    // MARK: Capability record parity

    func testLocalCapabilityMatchesDesktopGoldenIdentityAndShape() async throws {
        let repository = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: fixturePhone)
        _ = try await repository.publishLocalMobileSession(
            sessionID: DeviceSessionID(), deviceName: "iPhone", deviceKind: .iPhone, workspaceID: nil)
        let published = try await repository.publishLocalSharedTabCapability(nativeSupportActive: true)
        let record = try XCTUnwrap(published)

        let (_, fixture) = try UnifiedSyncFixture.load()
        let golden = try XCTUnwrap(fixture.records.first { $0.name == "device_capability_iphone" })
        XCTAssertEqual(golden.entity_type, 12)
        XCTAssertEqual(golden.data_class, SyncDataClass.deviceCapability.rawValue)
        let goldenObject = try UnifiedSyncFixture.object(golden.data)
        // UUIDv5(URL, "ahoi:sync:capability:v1:<device>") exactly as desktop derives it.
        XCTAssertEqual(record.id.uuidString.lowercased(), goldenObject["id"] as? String)
        XCTAssertEqual(record.id.uuidString.lowercased(), "4544c960-cddb-547f-b640-61563cd6ceeb")

        let encoded = try codec.encode(record)
        let object = try UnifiedSyncFixture.object(encoded)
        XCTAssertEqual(Set(object.keys), Set(goldenObject.keys))
        XCTAssertEqual(Set(try XCTUnwrap(object["field_versions"] as? [String: Any]).keys),
                       Set(try XCTUnwrap(goldenObject["field_versions"] as? [String: Any]).keys))
        XCTAssertEqual(object["features"] as? [String], ["shared-normal-tabs-v3"])
        XCTAssertEqual(object["features"] as? [String], goldenObject["features"] as? [String])
        XCTAssertEqual(object["readable_models"] as? [Int], [3])
        XCTAssertEqual(object["writable_models"] as? [Int], [3])
        XCTAssertEqual(object["model_version"] as? Int, goldenObject["model_version"] as? Int)
        XCTAssertEqual(object["device_id"] as? String, goldenObject["device_id"] as? String)
        XCTAssertEqual(object["tombstone"] as? Bool, false)

        // The desktop reader admits it: decode through the same codec path a
        // peer uses, with the Device independently known.
        let snapshot = try await repository.currentSnapshot()
        let device = try XCTUnwrap(snapshot.devices.first { $0.id == fixturePhone })
        let sample = UnifiedSyncFixture.Sample(
            name: "local", entity_type: 12, data_class: "deviceCapability",
            payload: try XCTUnwrap(String(data: encoded, encoding: .utf8)))
        let decoded = try codec.decodeCapability(UnifiedSyncFixture.envelope(sample), plaintext: encoded,
                                                 knownDevices: [fixturePhone: device])
        XCTAssertEqual(decoded, record)
        XCTAssertEqual(snapshot.deviceCapabilities, [record])
    }

    func testControlAnnouncementUpgradesButNeverDowngrades() async throws {
        let local = DeviceID()
        let repository = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: local)
        // No own Device record yet: nothing to declare for.
        let early = try await repository.publishLocalSharedTabCapability(nativeSupportActive: true)
        XCTAssertNil(early)
        _ = try await repository.publishLocalMobileSession(
            sessionID: DeviceSessionID(), deviceName: "iPhone", deviceKind: .iPhone, workspaceID: nil)

        let control = try await repository.publishLocalSharedTabCapability(nativeSupportActive: false)
        XCTAssertEqual(control?.features, [])
        let unchanged = try await repository.publishLocalSharedTabCapability(nativeSupportActive: false)
        XCTAssertNil(unchanged)
        let upgraded = try await repository.publishLocalSharedTabCapability(nativeSupportActive: true)
        XCTAssertEqual(upgraded?.features, [SharedTabContract.feature])
        XCTAssertGreaterThan(try XCTUnwrap(upgraded).version.modifiedAt, try XCTUnwrap(control).version.modifiedAt)
        // A local wiring gap is not a format downgrade (desktop parity).
        let gap = try await repository.publishLocalSharedTabCapability(nativeSupportActive: false)
        XCTAssertNil(gap)
        let stored = try await repository.currentSnapshot().deviceCapabilities
        XCTAssertEqual(stored.map(\.features), [[SharedTabContract.feature]])
    }

    func testRetiredLocalDeviceNeverAnnounces() async throws {
        let local = DeviceID()
        let repository = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: local)
        _ = try await repository.publishLocalMobileSession(
            sessionID: DeviceSessionID(), deviceName: "iPhone", deviceKind: .iPhone, workspaceID: nil)
        _ = try await repository.revokeAndRemoveDevice(local, revokedBy: local)
        let value = try await repository.publishLocalSharedTabCapability(nativeSupportActive: true)
        XCTAssertNil(value)
        let stored = try await repository.currentSnapshot().deviceCapabilities
        XCTAssertTrue(stored.isEmpty)
    }

    // MARK: Gate semantics

    private struct Roster {
        var mac: Device
        var phone: Device
        var macCapability: DeviceCapabilityRecord
        var phoneCapability: DeviceCapabilityRecord
    }

    private func fixtureRoster() throws -> Roster {
        let (_, fixture) = try UnifiedSyncFixture.load()
        func sample(_ name: String) throws -> UnifiedSyncFixture.Sample {
            try XCTUnwrap(fixture.records.first { $0.name == name })
        }
        let macSample = try sample("device_mac")
        let phoneSample = try sample("device_iphone")
        let mac = try codec.decodeDevice(UnifiedSyncFixture.envelope(macSample), plaintext: macSample.data)
        let phone = try codec.decodeDevice(UnifiedSyncFixture.envelope(phoneSample), plaintext: phoneSample.data)
        let known = [mac.id: mac, phone.id: phone]
        let capabilities = try ["device_capability_mac", "device_capability_iphone"].map { name in
            let value = try sample(name)
            return try codec.decodeCapability(UnifiedSyncFixture.envelope(value), plaintext: value.data,
                                              knownDevices: known)
        }
        return Roster(mac: mac, phone: phone, macCapability: capabilities[0], phoneCapability: capabilities[1])
    }

    private func assess(
        _ devices: [Device], _ capabilities: [DeviceCapabilityRecord], local: DeviceID,
        native: Bool = true, acks: Bool = true, syncEnabled: Bool = true
    ) -> SharedTabCapabilityReadiness.Assessment {
        SharedTabWriterGate.assess(
            snapshot: CompanionSnapshot(devices: devices, deviceCapabilities: capabilities),
            localDevice: local, syncEnabled: syncEnabled, nativeSupportActive: native,
            initialFetchComplete: true, localDeviceAcknowledged: acks, localCapabilityAcknowledged: acks)
    }

    func testGateOpensOnlyWhenEveryActivePeerDeclaresTheFeature() throws {
        let r = try fixtureRoster()
        let local = r.phone.id
        XCTAssertTrue(assess([r.mac, r.phone], [r.macCapability, r.phoneCapability], local: local).isReady)

        // The real-device finding, seen from the Mac: a peer without a declaration blocks.
        let missingPeer = assess([r.mac, r.phone], [r.phoneCapability], local: local)
        XCTAssertFalse(missingPeer.isReady)
        XCTAssertEqual(missingPeer.blockingDevices, [r.mac.id])
        // Own declaration missing blocks too (the iPhone must announce first).
        XCTAssertEqual(assess([r.mac, r.phone], [r.macCapability], local: local).blockingDevices, [local])

        // A control-only declaration without the feature blocks.
        var controlOnly = r.macCapability
        controlOnly.features = []
        XCTAssertEqual(assess([r.mac, r.phone], [controlOnly, r.phoneCapability], local: local).blockingDevices,
                       [r.mac.id])

        // A declaration for a Device that is not independently known blocks.
        let stranger = DeviceID()
        let strangerCapability = try DeviceCapabilityRecord(
            deviceID: stranger, features: [SharedTabContract.feature],
            version: SyncVersion(modifiedAt: HybridLogicalClock(physicalMilliseconds: 1_700_000_000_000, nodeID: stranger),
                                 modifiedBy: stranger).normalized(for: DeviceCapabilityRecord.syncFields))
        let unknown = assess([r.mac, r.phone], [r.macCapability, r.phoneCapability, strangerCapability], local: local)
        XCTAssertFalse(unknown.isReady)
        XCTAssertEqual(unknown.blockingDevices, [stranger])
    }

    func testRetiredOrTombstonedPeerDoesNotBlock() throws {
        let r = try fixtureRoster()
        var retired = r.mac
        retired.isRevoked = true
        XCTAssertTrue(assess([retired, r.phone], [r.phoneCapability], local: r.phone.id).isReady)
        // Its retained declaration is admitted, not a conflict (desktop keeps it in `declared`).
        XCTAssertTrue(assess([retired, r.phone], [r.macCapability, r.phoneCapability], local: r.phone.id).isReady)
        // Offline age is not retirement: an active offline peer without a declaration still blocks.
        var offline = r.mac
        offline.isOnline = false
        XCTAssertFalse(assess([offline, r.phone], [r.phoneCapability], local: r.phone.id).isReady)
    }

    func testGateStaysClosedWithoutOwnAcknowledgementNativeSupportOrSync() throws {
        let r = try fixtureRoster()
        let all = [r.macCapability, r.phoneCapability]
        let noAck = assess([r.mac, r.phone], all, local: r.phone.id, acks: false)
        XCTAssertFalse(noAck.isReady)
        XCTAssertTrue(noAck.bootstrapIncomplete)
        XCTAssertFalse(assess([r.mac, r.phone], all, local: r.phone.id, native: false).isReady)
        XCTAssertFalse(assess([r.mac, r.phone], all, local: r.phone.id, syncEnabled: false).isReady)
        var retiredSelf = r.phone
        retiredSelf.isRevoked = true
        XCTAssertFalse(assess([r.mac, retiredSelf], all, local: r.phone.id).isReady)
    }

    // MARK: Transport wiring

    private struct Peer {
        let repository: LocalFirstRepository
        let records: InMemorySyncRecordStore
        let transport: CompanionSyncVisibleTestTransport
        let bridge: CompanionSyncBridge
    }

    private func makePeer(_ local: DeviceID = DeviceID()) -> Peer {
        let records = InMemorySyncRecordStore()
        let transport = CompanionSyncVisibleTestTransport(recordStore: records)
        let repository = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: local)
        let sealer = KeychainCompanionPayloadSealer(
            configuration: .init(service: "shared-tab-gate-test", account: "fixture", keyVersion: 1),
            keyLoader: { Data(repeating: 0x5A, count: 32) })
        return Peer(repository: repository, records: records, transport: transport,
                    bridge: CompanionSyncBridge(repository: repository, transport: transport, sealer: sealer))
    }

    private func publishLinkedTab(_ peer: Peer) async throws -> (TreeNode, RemoteTab) {
        let workspace = try await peer.repository.createWorkspace(name: "Gate")
        let page = try await peer.repository.createTreeNode(
            workspaceID: workspace.id, kind: .savedPage, title: "Page", url: "https://example.test/gate")
        let tab = try await peer.repository.publishLocalMobileTab(
            tabID: UUID(), sessionID: DeviceSessionID(), deviceName: "iPhone", deviceKind: .iPhone,
            workspaceID: workspace.id, title: "Tab", url: "https://example.test/gate", pinned: false,
            treeNodeID: page.id).tab
        return (page, tab)
    }

    func testClosedGateWithholdsOwnPagesAndPresencesButNotControlRecords() async throws {
        let peer = makePeer()
        let (page, tab) = try await publishLinkedTab(peer)
        let capability = try await peer.repository.publishLocalSharedTabCapability(nativeSupportActive: true)
        XCTAssertNotNil(capability)

        try await peer.bridge.enqueueLocalSnapshot()
        var ids = Set(try await peer.records.allRecords().map(\.recordID))
        XCTAssertFalse(ids.contains(tab.id.rawValue))
        XCTAssertFalse(ids.contains(page.id.rawValue))
        XCTAssertTrue(ids.contains(peer.repository.localDeviceID.rawValue))
        XCTAssertTrue(ids.contains(SharedTabContract.capabilityID(for: peer.repository.localDeviceID)))
        let withheld = await peer.bridge.hasWithheldSharedTabRecords()
        XCTAssertTrue(withheld)

        // Incremental shared-tab batches obey the same gate.
        let batch = CompanionMobilePublicationBatch(nodes: [page], tabs: [tab])
        try await batch.enqueue(using: peer.bridge)
        ids = Set(try await peer.records.allRecords().map(\.recordID))
        XCTAssertFalse(ids.contains(tab.id.rawValue))
        XCTAssertFalse(ids.contains(page.id.rawValue))

        // Opening reports the backlog once; the reseed then publishes it.
        let reseed = await peer.bridge.setSharedTabWriteAllowed(true)
        XCTAssertTrue(reseed)
        let again = await peer.bridge.setSharedTabWriteAllowed(true)
        XCTAssertFalse(again)
        try await peer.bridge.enqueueLocalSnapshot()
        ids = Set(try await peer.records.allRecords().map(\.recordID))
        XCTAssertTrue(ids.contains(tab.id.rawValue))
        XCTAssertTrue(ids.contains(page.id.rawValue))
    }

    @MainActor
    func testModelAnnouncesFeatureAndOpensGateAfterAcknowledgedPass() async throws {
        let local = DeviceID()
        let peer = makePeer(local)
        let model = CompanionAppModel(
            repository: peer.repository, mobileSessionID: DeviceSessionID(),
            mobileDeviceName: "iPhone", mobileDeviceKind: .iPhone,
            defaults: UserDefaults(suiteName: "SharedTabWriterGateTests-\(UUID().uuidString)")!)
        model.syncBridge = peer.bridge
        model.desiredSyncEnabled = true
        _ = try await peer.repository.publishLocalMobileSession(
            sessionID: DeviceSessionID(), deviceName: "iPhone", deviceKind: .iPhone, workspaceID: nil)
        let devices = try await peer.repository.currentSnapshot().devices
        let localDevice = try XCTUnwrap(devices.first)
        try await peer.bridge.enqueue(localDevice)

        // Before native wiring only the control announcement is queued.
        await model.publishLocalSharedTabCapability(using: peer.bridge)
        var stored = try await peer.repository.currentSnapshot().deviceCapabilities
        XCTAssertEqual(stored.map(\.features), [[]])
        model.activateSharedTabNativeSupport()
        XCTAssertTrue(model.sharedTabNativeSupportActive)
        await model.publishLocalSharedTabCapability(using: peer.bridge)
        stored = try await peer.repository.currentSnapshot().deviceCapabilities
        XCTAssertEqual(stored.map(\.features), [[SharedTabContract.feature]])

        // Not yet sent: own records unacknowledged, gate closed, one retry requested.
        let generation = model.syncGeneration
        let retry = await model.refreshSharedTabWriterGate(using: peer.bridge, generation: generation)
        XCTAssertTrue(retry)
        XCTAssertEqual(model.sharedTabWriterAssessment?.isReady, false)
        let closed = await peer.bridge.isSharedTabWriteAllowed()
        XCTAssertFalse(closed)

        // A completed pass acknowledges both; the sole active device may write.
        try await peer.bridge.syncNow()
        let acks = await peer.bridge.localSharedTabAcknowledgements()
        XCTAssertTrue(acks.device && acks.capability)
        _ = await model.refreshSharedTabWriterGate(using: peer.bridge, generation: generation)
        XCTAssertEqual(model.sharedTabWriterAssessment?.isReady, true)
        let open = await peer.bridge.isSharedTabWriteAllowed()
        XCTAssertTrue(open)
    }
}
