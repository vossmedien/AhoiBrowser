import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

final class CompanionProductRecordTests: XCTestCase {
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

    func testLegacySnapshotDefaultsNewProductRecordsToEmpty() throws {
        let decoded = try JSONDecoder().decode(
            CompanionSnapshot.self,
            from: Data(#"{"devices":[],"workspaces":[],"treeNodes":[],"sessions":[],"remoteTabs":[],"history":[]}"#.utf8)
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
