import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionProductRecordTests: XCTestCase {
    func testInventoryUpsertPreservesTwoOfflineFieldEdits() async throws {
        let device = DeviceID()
        let id = UUID()
        let fields = DesktopWirePayloadCodec.extensionInventoryFields
        let nameEdited = try CompanionExtensionInventoryRecord(
            id: id, deviceID: device,
            extensionID: "abcdefghijklmnopabcdefghijklmnop",
            name: "Renamed extension", extensionVersion: "1.2.3", enabled: true,
            version: editVersion(device: device, fields: fields, changed: "name", at: 2_000),
            tombstone: nil
        )
        let enabledEdited = try CompanionExtensionInventoryRecord(
            id: id, deviceID: device, extensionID: nameEdited.extensionID,
            name: "Original extension", extensionVersion: "1.2.3", enabled: false,
            version: editVersion(device: device, fields: fields, changed: "enabled", at: 3_000),
            tombstone: nil
        )
        let repository = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: device)
        _ = try await repository.upsert(nameEdited)
        _ = try await repository.upsert(enabledEdited)
        let snapshot = try await repository.currentSnapshot()
        let merged = try XCTUnwrap(snapshot.productRecords.extensionInventory.first { $0.id == id })
        XCTAssertEqual(merged.name, "Renamed extension")
        XCTAssertFalse(merged.enabled)
        XCTAssertEqual(merged.version.fieldVersions["name"], nameEdited.version.fieldVersions["name"])
        XCTAssertEqual(merged.version.fieldVersions["enabled"], enabledEdited.version.fieldVersions["enabled"])
    }

    func testDeveloperAssetUpsertPreservesTwoOfflineFieldEdits() async throws {
        let device = DeviceID()
        let id = UUID()
        let fields = DesktopWirePayloadCodec.developerAssetFields
        let nameEdited = try CompanionDeveloperAssetRecord(
            id: id, kind: .css, name: "Renamed CSS asset",
            scope: "https://example.test/*", source: "body { color: red; }",
            enabled: true, optedIn: true,
            version: editVersion(device: device, fields: fields, changed: "name", at: 2_000),
            tombstone: nil
        )
        let enabledEdited = try CompanionDeveloperAssetRecord(
            id: id, kind: .css, name: "Original CSS asset",
            scope: nameEdited.scope, source: nameEdited.source,
            enabled: false, optedIn: true,
            version: editVersion(device: device, fields: fields, changed: "enabled", at: 3_000),
            tombstone: nil
        )
        let repository = LocalFirstRepository(store: InMemoryCompanionStore(), localDeviceID: device)
        _ = try await repository.upsert(nameEdited)
        _ = try await repository.upsert(enabledEdited)
        let snapshot = try await repository.currentSnapshot()
        let merged = try XCTUnwrap(snapshot.productRecords.developerAssets.first { $0.id == id })
        XCTAssertEqual(merged.name, "Renamed CSS asset")
        XCTAssertFalse(merged.enabled)
        XCTAssertEqual(merged.version.fieldVersions["name"], nameEdited.version.fieldVersions["name"])
        XCTAssertEqual(merged.version.fieldVersions["enabled"], enabledEdited.version.fieldVersions["enabled"])
    }

    private func editVersion(
        device: DeviceID, fields: Set<String>, changed: String, at time: UInt64
    ) -> SyncVersion {
        let base = HybridLogicalClock(physicalMilliseconds: 1_000, nodeID: device)
        let edit = HybridLogicalClock(physicalMilliseconds: time, nodeID: device)
        return SyncVersion(
            modifiedAt: edit, modifiedBy: device,
            fieldVersions: Dictionary(uniqueKeysWithValues: fields.map { field in
                (field, field == changed ? edit : base)
            })
        )
    }

    func testProductWireRecordsRoundTrip() throws {
        let device = DeviceID(
            rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000001")!
        )
        let version = makeVersion(device: device)
        let codec = DesktopWirePayloadCodec()
        let appearance = try CompanionAppearanceRecord(
            id: UUID(),
            colorMode: .dark,
            accentARGB: 0xff123456,
            useSystemAccent: false,
            version: version,
            tombstone: nil
        )
        let setting = try CompanionPermittedSettingRecord(
            id: UUID(),
            settingID: "ahoi.appearance.glass_enabled",
            valueJSON: "true",
            version: version,
            tombstone: nil
        )
        let inventory = try CompanionExtensionInventoryRecord(
            id: UUID(),
            deviceID: device,
            extensionID: "abcdefghijklmnopabcdefghijklmnop",
            name: "Example",
            extensionVersion: "1.2.3",
            enabled: true,
            version: version,
            tombstone: nil
        )
        let asset = try CompanionDeveloperAssetRecord(
            id: UUID(),
            kind: .css,
            name: "Readable",
            scope: "https://example.test",
            source: "body { color: CanvasText; }",
            enabled: true,
            optedIn: true,
            version: version,
            tombstone: nil
        )
        var normalizedAppearance = appearance
        normalizedAppearance.version = version.normalized(
            for: DesktopWirePayloadCodec.appearanceFields
        )
        var normalizedSetting = setting
        normalizedSetting.version = version.normalized(
            for: DesktopWirePayloadCodec.permittedSettingFields
        )
        var normalizedInventory = inventory
        normalizedInventory.version = version.normalized(
            for: DesktopWirePayloadCodec.extensionInventoryFields
        )
        var normalizedAsset = asset
        normalizedAsset.version = version.normalized(
            for: DesktopWirePayloadCodec.developerAssetFields
        )

        XCTAssertEqual(
            try codec.decodeAppearance(
                envelope(appearance.id, .appearance, version),
                plaintext: codec.encode(appearance)
            ),
            normalizedAppearance
        )
        XCTAssertEqual(
            try codec.decodePermittedSetting(
                envelope(setting.id, .permittedSetting, version),
                plaintext: codec.encode(setting)
            ),
            normalizedSetting
        )
        XCTAssertEqual(
            try codec.decodeExtensionInventory(
                envelope(inventory.id, .extensionInventory, version),
                plaintext: codec.encode(inventory)
            ),
            normalizedInventory
        )
        XCTAssertEqual(
            try codec.decodeDeveloperAsset(
                envelope(asset.id, .developerAsset, version),
                plaintext: codec.encode(asset)
            ),
            normalizedAsset
        )
    }

    func testLegacySnapshotIsRejectedAndCurrentSnapshotDefaultsProductRecordsToEmpty() throws {
        // ADR 0009: an unmarked old snapshot is never decoded as an empty
        // current collection; only the optional product section may be absent.
        XCTAssertThrowsError(try JSONDecoder().decode(
            CompanionSnapshot.self,
            from: Data(#"{"devices":[],"workspaces":[],"treeNodes":[],"sessions":[],"remoteTabs":[],"history":[]}"#.utf8)
        ))
        let decoded = try JSONDecoder().decode(
            CompanionSnapshot.self,
            from: Data(#"{"syncFormatVersion":3,"structureRevision":1,"splitGroups":[],"archiveEntries":[],"devices":[],"workspaces":[],"treeNodes":[],"sessions":[],"remoteTabs":[],"history":[]}"#.utf8)
        )
        XCTAssertEqual(decoded.productRecords, .empty)
    }

    func testDeveloperHeaderProfileCannotCarryLiteralSecret() throws {
        let device = DeviceID()
        XCTAssertThrowsError(try CompanionDeveloperAssetRecord(
            id: UUID(),
            kind: .headerProfile,
            name: "Authorization",
            scope: "https://example.test",
            source: #"{"version":1,"rules":[{"name":"Authorization","action":"set","value":"Bearer secret"}]}"#,
            enabled: true,
            optedIn: true,
            version: makeVersion(device: device),
            tombstone: nil
        )) { error in
            XCTAssertEqual(
                error as? CompanionProductRecordError,
                .developerAssetContainsSecretMaterial
            )
        }
    }

    private func makeVersion(device: DeviceID) -> SyncVersion {
        SyncVersion(
            modifiedAt: HybridLogicalClock(
                physicalMilliseconds: 1_000,
                nodeID: device
            ),
            modifiedBy: device
        )
    }

    private func envelope(
        _ id: UUID,
        _ dataClass: SyncDataClass,
        _ version: SyncVersion
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
                nonce: Data(repeating: 0, count: 12),
                ciphertextAndTag: Data(repeating: 0, count: 16)
            )
        )
    }
}
