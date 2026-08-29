import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionConvergenceTests: XCTestCase {
    private let windowsEpochMicroseconds: Int64 = 11_644_473_600_000_000

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
