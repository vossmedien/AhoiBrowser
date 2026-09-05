import XCTest
import CloudKit
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionConvergenceTests: XCTestCase {
    private let windowsEpochMicroseconds: Int64 = 11_644_473_600_000_000

#if DEBUG
    /// Domain integration simulation: independent durable repositories and
    /// encrypted stores, with only delivery substituted. Both sides execute
    /// production bridge, wire codec, field merge and CryptoKit encryption.
    func testTwoIndependentRepositoriesConvergeThroughSerializedEncryptedRelay() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiDomainRelay-\(UUID().uuidString)", isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let mac = makeRelayPeer(at: directory.appendingPathComponent("mac.json"))
        let phone = makeRelayPeer(at: directory.appendingPathComponent("phone.json"))
        let workspace = try await mac.repository.createWorkspace(name: "Shared workspace")
        let page = try await mac.repository.createTreeNode(
            workspaceID: workspace.id, kind: .savedPage,
            title: "From Mac", url: "https://example.test/original"
        )
        let macTab = try await mac.repository.publishLocalMobileTab(
            tabID: UUID(), sessionID: DeviceSessionID(), deviceName: "Logical Mac",
            deviceKind: .mac, workspaceID: workspace.id, title: "Mac tab",
            url: "https://example.test/mac", pinned: false
        )
        try await relay(mac, to: phone)
        let received = try await phone.repository.currentSnapshot()
        XCTAssertEqual(received.visibleWorkspaces.map(\.id), [workspace.id])
        XCTAssertEqual(received.visibleTreeNodes.first { $0.id == page.id }, page)
        XCTAssertTrue(received.visibleRemoteTabs.contains { $0.id == macTab.tab.id })

        _ = try await phone.repository.updateTreeNode(page.id, title: "From iPhone")
        let phoneTab = try await phone.repository.publishLocalMobileTab(
            tabID: UUID(), sessionID: DeviceSessionID(), deviceName: "Logical iPhone",
            deviceKind: .iPhone, workspaceID: workspace.id, title: "Phone tab",
            url: "https://example.test/phone", pinned: false
        )
        try await relay(phone, to: mac)
        let returned = try await mac.repository.currentSnapshot()
        XCTAssertEqual(returned.visibleTreeNodes.first { $0.id == page.id }?.title,
                       "From iPhone")
        XCTAssertTrue(returned.visibleRemoteTabs.contains { $0.id == phoneTab.tab.id })

        // No delivery while both independent local stores receive edits.
        _ = try await mac.repository.updateTreeNode(page.id, title: "Offline title")
        _ = try await phone.repository.updateTreeNode(
            page.id, url: .some("https://example.test/offline")
        )
        try await relay(mac, to: phone)
        try await relay(phone, to: mac)
        let macMerged = try await mac.repository.currentSnapshot()
        let phoneMerged = try await phone.repository.currentSnapshot()
        let merged = try XCTUnwrap(macMerged.visibleTreeNodes.first { $0.id == page.id })
        XCTAssertEqual(merged, phoneMerged.visibleTreeNodes.first { $0.id == page.id })
        XCTAssertEqual(merged.title, "Offline title")
        XCTAssertEqual(merged.url, "https://example.test/offline")

        // Retain a genuinely stale encrypted live envelope for delayed delivery.
        let retained = try await mac.records.record(for: page.id.rawValue)
        let stale = try XCTUnwrap(retained)
        let deleted = try await phone.repository.deleteTreeNode(page.id)
        for node in deleted { try await phone.bridge.enqueue(node) }
        try await relay(phone, to: mac)
        try await deliver([stale], to: phone)
        try await relay(phone, to: mac)

        let deniedID = UUID()
        let denied = SyncRecord(
            recordID: deniedID, entityID: deniedID, dataClass: .incognito,
            modifiedAt: workspace.version.modifiedAt,
            originatingDevice: mac.deviceID,
            encryptedValue: try mac.sealer.seal(Data("private-relay-sentinel".utf8))
        )
        do {
            try await mac.transport.enqueue(denied)
            XCTFail("Private data must never enter the simulated network store.")
        } catch let error as SyncBoundaryError {
            XCTAssertEqual(error, .dataClassDenied(.incognito))
        }
        try await relay(mac, to: phone)
        for peer in [mac, phone] {
            let snapshot = try await peer.repository.currentSnapshot()
            XCTAssertFalse(snapshot.visibleTreeNodes.contains { $0.id == page.id })
            XCTAssertTrue(snapshot.treeNodes.first { $0.id == page.id }?.isDeleted == true)
            XCTAssertEqual(Set(snapshot.visibleRemoteTabs.map(\.id)),
                           Set([macTab.tab.id, phoneTab.tab.id]))
            let forbidden = try await peer.records.record(for: deniedID)
            XCTAssertNil(forbidden)
            let restarted = LocalFirstRepository(
                store: FileCompanionStore(fileURL: peer.fileURL),
                localDeviceID: peer.deviceID
            )
            let restored = try await restarted.currentSnapshot()
            XCTAssertEqual(restored.treeNodes, snapshot.treeNodes)
            XCTAssertEqual(restored.remoteTabs, snapshot.remoteTabs)
        }
    }

    private struct RelayPeer {
        let fileURL: URL
        let deviceID: DeviceID
        let repository: LocalFirstRepository
        let records: InMemorySyncRecordStore
        let transport: CompanionSyncVisibleTestTransport
        let sealer: KeychainCompanionPayloadSealer
        let bridge: CompanionSyncBridge
    }

    private func makeRelayPeer(at fileURL: URL) -> RelayPeer {
        let deviceID = DeviceID()
        let records = InMemorySyncRecordStore()
        let transport = CompanionSyncVisibleTestTransport(recordStore: records)
        let repository = LocalFirstRepository(
            store: FileCompanionStore(fileURL: fileURL), localDeviceID: deviceID
        )
        let sealer = KeychainCompanionPayloadSealer(
            configuration: .init(service: "domain-relay-test", account: "fixture", keyVersion: 1),
            keyLoader: { Data(repeating: 0x42, count: 32) }
        )
        return RelayPeer(
            fileURL: fileURL, deviceID: deviceID, repository: repository,
            records: records, transport: transport, sealer: sealer,
            bridge: CompanionSyncBridge(repository: repository, transport: transport,
                                        sealer: sealer)
        )
    }

    private func relay(_ source: RelayPeer, to target: RelayPeer) async throws {
        try await source.bridge.enqueueLocalSnapshot()
        try await source.bridge.syncNow()
        let outgoing = try await source.records.allRecords()
        try await deliver(outgoing, to: target)
    }

    private func deliver(_ records: [SyncRecord], to peer: RelayPeer) async throws {
        let codec = AppleCloudKitRecordCodec()
        let zone = CKRecordZone.ID(zoneName: "DomainRelay", ownerName: CKCurrentUserDefaultName)
        let copied = try records.map { record in
            let cloud = try codec.encode(record, zoneID: zone)
            XCTAssertNil(cloud["url"])
            XCTAssertNil(cloud["title"])
            return try codec.decode(cloud)
        }
        _ = try await peer.records.mergeRecords(copied, policy: .transportLastWriterWins)
        try await peer.records.stageFetchedRecords(copied)
        try await peer.bridge.syncNow()
    }
#endif

    func testRemoteTabDisjointFieldsMergeAndMintDominatingRecordClock() throws {
        let device = DeviceID(
            rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000001")!
        )
        let base = clock(100, device)
        let titleClock = clock(200, device)
        let remoteDevice = DeviceID(
            rawValue: UUID(uuidString: "20000000-0000-4000-8000-000000000002")!
        )
        let pinnedClock = clock(150, remoteDevice)
        let fields: Set<String> = [
            "device_id", "session_id", "workspace_id", "url", "title", "opened_at",
            "last_active", "pinned", "is_incognito", "tombstone",
        ]
        var localFields = Dictionary(uniqueKeysWithValues: fields.map { ($0, base) })
        localFields["title"] = titleClock
        var remoteFields = Dictionary(uniqueKeysWithValues: fields.map { ($0, base) })
        remoteFields["pinned"] = pinnedClock
        let tabID = TabID()
        let sessionID = DeviceSessionID()
        let local = try RemoteTab(
            tabID: tabID,
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Mac",
            sessionID: sessionID,
            title: "Local title",
            url: "https://example.test",
            openedAt: base,
            lastActiveAt: base,
            pinned: false,
            version: .init(
                modifiedAt: titleClock,
                modifiedBy: device,
                fieldVersions: localFields
            )
        )
        let remote = try RemoteTab(
            tabID: tabID,
            deviceID: device,
            deviceKind: .mac,
            deviceName: "Renamed Mac",
            sessionID: sessionID,
            title: "Base title",
            url: "https://example.test",
            openedAt: base,
            lastActiveAt: base,
            pinned: true,
            version: .init(
                modifiedAt: pinnedClock,
                modifiedBy: remoteDevice,
                fieldVersions: remoteFields
            )
        )

        let merged = try CompanionReadModelFieldMerge.merge(local, remote)

        XCTAssertEqual(merged.title, "Local title")
        XCTAssertTrue(merged.pinned)
        XCTAssertEqual(merged.deviceName, "Renamed Mac")
        XCTAssertGreaterThan(merged.version, local.version)
        XCTAssertGreaterThan(merged.version, remote.version)
        XCTAssertEqual(merged.version.fieldVersions["title"], titleClock)
        XCTAssertEqual(merged.version.fieldVersions["pinned"], pinnedClock)
    }

#if canImport(CloudKit)
    func testFetchedEnvelopeLosingTransportLWWStillReachesDomainMerge() async throws {
        let localDevice = DeviceID(
            rawValue: UUID(uuidString: "50000000-0000-4000-8000-000000000005")!
        )
        let remoteDevice = DeviceID(
            rawValue: UUID(uuidString: "60000000-0000-4000-8000-000000000006")!
        )
        let workspaceID = WorkspaceID(
            rawValue: UUID(uuidString: "70000000-0000-4000-8000-000000000007")!
        )
        let base = clock(100, localDevice)
        let localNameClock = clock(300, localDevice)
        let remoteOrderClock = clock(200, remoteDevice)
        var localFields = Dictionary(
            uniqueKeysWithValues: CompanionFieldMerge.workspaceFields.map { ($0, base) }
        )
        localFields["name"] = localNameClock
        localFields["modified_at"] = localNameClock
        var remoteFields = Dictionary(
            uniqueKeysWithValues: CompanionFieldMerge.workspaceFields.map { ($0, base) }
        )
        remoteFields["sort_key"] = remoteOrderClock
        remoteFields["modified_at"] = remoteOrderClock
        let localVersion = SyncVersion(
            modifiedAt: localNameClock,
            modifiedBy: localDevice,
            fieldVersions: localFields
        )
        let remoteVersion = SyncVersion(
            modifiedAt: remoteOrderClock,
            modifiedBy: remoteDevice,
            fieldVersions: remoteFields
        )
        let local = Workspace(
            workspaceID: workspaceID,
            name: "Local name",
            sortKey: "a",
            createdAt: base,
            modifiedAt: localNameClock,
            version: localVersion
        )
        let remote = Workspace(
            workspaceID: workspaceID,
            name: "Base name",
            sortKey: "z",
            createdAt: base,
            modifiedAt: remoteOrderClock,
            version: remoteVersion
        )
        let localEnvelope = envelope(
            id: workspaceID.rawValue,
            dataClass: .workspace,
            version: localVersion,
            byte: 0x11
        )
        let remoteEnvelope = envelope(
            id: workspaceID.rawValue,
            dataClass: .workspace,
            version: remoteVersion,
            byte: 0x22
        )
        let store = InMemorySyncRecordStore(records: [localEnvelope])

        try await CloudKitSyncProvider.retainFetchedEnvelope(
            remoteEnvelope,
            in: store
        )

        // The opaque transport snapshot still selects the later local record,
        // but the exact remote bytes remain available to the domain importer.
        let selectedEnvelope = try await store.record(for: workspaceID.rawValue)
        let pendingEnvelopes = try await store.fetchedRecords()
        XCTAssertEqual(selectedEnvelope, localEnvelope)
        XCTAssertEqual(pendingEnvelopes, [remoteEnvelope])
        let primaryEnvelopes = try await store.allRecords()
        let importCandidates = CompanionSyncBridge.makeImportCandidates(
            primaryRecords: primaryEnvelopes,
            fetchedRecords: pendingEnvelopes
        )
        XCTAssertEqual(importCandidates.map(\.record), [localEnvelope, remoteEnvelope])
        XCTAssertEqual(importCandidates.map(\.acknowledgeOnSuccess), [false, true])
        let merged = try CompanionFieldMerge.merge(local, remote)
        XCTAssertEqual(merged.name, "Local name")
        XCTAssertEqual(merged.sortKey, "z")
        XCTAssertGreaterThan(merged.version, localVersion)
        XCTAssertGreaterThan(merged.version, remoteVersion)
    }
#endif

    func testTreeOrderTieBreakerRoundTripsIndependentlyOfRecordWriter() throws {
        let orderDevice = DeviceID(
            rawValue: UUID(uuidString: "30000000-0000-4000-8000-000000000003")!
        )
        let writer = DeviceID(
            rawValue: UUID(uuidString: "40000000-0000-4000-8000-000000000004")!
        )
        let version = SyncVersion(modifiedAt: clock(1_000, writer), modifiedBy: writer)
        let node = try TreeNode(
            treeNodeID: TreeNodeID(),
            workspaceID: WorkspaceID(),
            kind: .folder,
            title: "Folder",
            orderKey: try OrderKey(components: [10, 20], tieBreaker: orderDevice),
            version: version
        )
        let codec = DesktopWirePayloadCodec()
        let payload = try codec.encode(node)
        let record = envelope(
            id: node.id.rawValue,
            dataClass: .treeNode,
            version: version
        )

        let decoded = try codec.decodeTreeNode(record, plaintext: payload)

        XCTAssertEqual(decoded.orderKey, node.orderKey)
        XCTAssertEqual(try codec.encode(decoded), payload)

        var legacyObject = try codec.object(from: payload)
        legacyObject["sort_key"] = "folder-a"
        let legacyPayload = try JSONSerialization.data(
            withJSONObject: legacyObject,
            options: [.sortedKeys, .withoutEscapingSlashes]
        )
        let legacy = try codec.decodeTreeNode(record, plaintext: legacyPayload)
        XCTAssertEqual(legacy.wireSortKey, "folder-a")
        XCTAssertEqual(try codec.encode(legacy), legacyPayload)
    }

    func testWorkspaceARGBAndModifiedTimeMatchDesktopIntegerSemantics() throws {
        let device = DeviceID()
        let version = SyncVersion(modifiedAt: clock(1_000, device), modifiedBy: device)
        let modified = clock(900, device)
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Produkt",
            accent: "#ff3366aa",
            modifiedAt: modified,
            version: version
        )
        let codec = DesktopWirePayloadCodec()
        let payload = try codec.encode(workspace)
        let object = try codec.object(from: payload)

        XCTAssertEqual(
            (object["accent_argb"] as? NSNumber)?.int64Value,
            Int64(Int32(bitPattern: 0xff33_66aa))
        )
        XCTAssertEqual(
            object["modified_at"] as? String,
            String(windowsEpochMicroseconds + 900_000)
        )
        let decoded = try codec.decodeWorkspace(
            envelope(id: workspace.id.rawValue, dataClass: .workspace, version: version),
            plaintext: payload
        )
        XCTAssertEqual(decoded.modifiedAt, modified)
        XCTAssertEqual(try codec.encode(decoded), payload)
    }

    func testWorkspaceCreationClockKeepsItsFieldWriterAcrossRemoteEdit() throws {
        let creator = DeviceID()
        let editor = DeviceID()
        let created = clock(100, creator)
        let edited = clock(200, editor)
        let fields: Set<String> = [
            "name", "icon", "sort_key", "accent_argb", "created_at", "modified_at",
            "tombstone",
        ]
        var fieldVersions = Dictionary(uniqueKeysWithValues: fields.map { ($0, created) })
        fieldVersions["name"] = edited
        fieldVersions["modified_at"] = edited
        let version = SyncVersion(
            modifiedAt: edited,
            modifiedBy: editor,
            fieldVersions: fieldVersions
        )
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Remote edit",
            createdAt: created,
            modifiedAt: edited,
            version: version
        )
        let codec = DesktopWirePayloadCodec()
        let payload = try codec.encode(workspace)

        let decoded = try codec.decodeWorkspace(
            envelope(id: workspace.id.rawValue, dataClass: .workspace, version: version),
            plaintext: payload
        )

        XCTAssertEqual(decoded.createdAt, created)
        XCTAssertEqual(decoded.modifiedAt, edited)
    }

    func testUnknownDeviceDoesNotBecomeMacDuringRemoteTabDecode() throws {
        let device = DeviceID()
        let version = SyncVersion(modifiedAt: clock(100, device), modifiedBy: device)
        let tab = try RemoteTab(
            tabID: TabID(),
            deviceID: device,
            deviceKind: .iPad,
            deviceName: "iPad",
            sessionID: DeviceSessionID(),
            title: "Ahoi",
            url: "https://example.test",
            lastActiveAt: version.modifiedAt,
            version: version
        )
        let codec = DesktopWirePayloadCodec()

        XCTAssertThrowsError(try codec.decodeRemoteTab(
            envelope(id: tab.id.rawValue, dataClass: .deviceTab, version: version),
            plaintext: codec.encode(tab),
            devices: [:],
            workspaces: [:]
        )) { error in
            XCTAssertEqual(
                error as? DesktopWirePayloadCodecError,
                .unsupportedDeviceType
            )
        }
    }

    private func clock(_ milliseconds: UInt64, _ device: DeviceID) -> HybridLogicalClock {
        HybridLogicalClock(physicalMilliseconds: milliseconds, nodeID: device)
    }

    private func envelope(
        id: UUID,
        dataClass: SyncDataClass,
        version: SyncVersion,
        byte: UInt8 = 0
    ) -> SyncRecord {
        SyncRecord(
            recordID: id,
            entityID: id,
            schemaVersion: version.schemaVersion,
            dataClass: dataClass,
            modifiedAt: version.modifiedAt,
            originatingDevice: version.modifiedBy,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: byte, count: 12),
                ciphertextAndTag: Data(repeating: byte, count: 16)
            )
        )
    }
}
