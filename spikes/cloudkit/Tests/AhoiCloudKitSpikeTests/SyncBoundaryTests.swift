import XCTest
@testable import AhoiCloudKitSpike

final class SyncBoundaryTests: XCTestCase {
    private let deniedClasses: [SyncDataClass] = [
        .cookie,
        .password,
        .autofill,
        .siteData,
        .cache,
        .permission,
        .extensionStorage,
        .incognito,
        .keychainSecret,
        .headerSecret,
        .httpAuthSecret,
    ]

    func testEverySensitiveDataClassIsDenied() throws {
        let boundary = SyncBoundary()

        for dataClass in deniedClasses {
            let record = makeRecord(dataClass: dataClass)
            XCTAssertThrowsError(try boundary.authorize(record)) { error in
                XCTAssertEqual(
                    error as? SyncBoundaryError,
                    .dataClassDenied(dataClass),
                    "Unexpected result for \(dataClass)"
                )
            }
        }
    }

    func testAllowListIsExhaustiveAndDeveloperAssetRequiresEntityOptIn() throws {
        let boundary = SyncBoundary()
        let permitted: [SyncDataClass] = [
            .workspace,
            .treeNode,
            .device,
            .deviceSession,
            .deviceTab,
            .historyVisit,
            .remoteCommand,
            .appearance,
            .permittedSetting,
            .extensionInventory,
            .bookmark,
            .deviceCapability,
        ]

        XCTAssertEqual(Set(permitted).union([.developerAsset]), SharedSyncFormat.supportedDataClasses)

        for dataClass in permitted {
            XCTAssertNoThrow(try boundary.authorize(makeRecord(dataClass: dataClass)))
        }
        XCTAssertNoThrow(try boundary.authorize(makeTombstoneRecord()))
        for retiredAlias in [SyncDataClass.orderKey, .recoveryMetadata, .history, .tombstone] {
            XCTAssertThrowsError(try boundary.authorize(makeRecord(dataClass: retiredAlias))) { error in
                XCTAssertEqual(error as? SyncBoundaryError, .dataClassDenied(retiredAlias))
            }
        }

        let developerAsset = makeRecord(dataClass: .developerAsset)
        XCTAssertThrowsError(try boundary.authorize(developerAsset))
        XCTAssertNoThrow(
            try boundary.authorize(
                developerAsset,
                context: .init(optedInDeveloperAssetIDs: [developerAsset.entityID])
            )
        )
    }

    func testCiphertextShapeIsValidatedBeforeUpload() {
        let invalidValues: [EncryptedValue] = [
            .init(
                keyVersion: 0,
                nonce: Data(repeating: 0, count: 12),
                ciphertextAndTag: Data(repeating: 1, count: 16)
            ),
            .init(
                keyVersion: 1,
                nonce: Data(repeating: 0, count: 11),
                ciphertextAndTag: Data(repeating: 1, count: 16)
            ),
            .init(
                keyVersion: 1,
                nonce: Data(repeating: 0, count: 12),
                ciphertextAndTag: Data(repeating: 1, count: 15)
            ),
        ]

        for encryptedValue in invalidValues {
            let record = makeRecord(
                dataClass: .workspace,
                encryptedValue: encryptedValue
            )
            XCTAssertThrowsError(try SyncBoundary().authorize(record)) { error in
                XCTAssertEqual(error as? SyncBoundaryError, .invalidCiphertext)
            }
        }
    }

    func testSchemaAndTombstoneShapeAreValidated() {
        for version in [UInt32(0), 1, 2, 4] {
            let invalidSchema = makeRecord(dataClass: .workspace, schemaVersion: version)
            XCTAssertThrowsError(try SyncBoundary().authorize(invalidSchema)) { error in
                XCTAssertEqual(error as? SyncBoundaryError, .invalidSchemaVersion)
            }
        }

        let commandTombstone = makeTombstoneRecord(dataClass: .remoteCommand)
        XCTAssertThrowsError(try SyncBoundary().authorize(commandTombstone)) { error in
            XCTAssertEqual(error as? SyncBoundaryError, .invalidTombstone)
        }
    }

    private func makeRecord(
        dataClass: SyncDataClass,
        schemaVersion: UInt32 = SharedSyncFormat.currentVersion,
        encryptedValue: EncryptedValue = .init(
            keyVersion: 1,
            nonce: Data(repeating: 1, count: 12),
            ciphertextAndTag: Data(repeating: 2, count: 32)
        )
    ) -> SyncRecord {
        let device = DeviceID()
        return .init(
            entityID: UUID(),
            schemaVersion: schemaVersion,
            dataClass: dataClass,
            modifiedAt: .init(physicalMilliseconds: 1, nodeID: device),
            originatingDevice: device,
            encryptedValue: encryptedValue
        )
    }

    private func makeTombstoneRecord(dataClass: SyncDataClass = .workspace) -> SyncRecord {
        let device = DeviceID()
        let entityID = UUID()
        let deletedAt = HybridLogicalClock(
            physicalMilliseconds: 100,
            nodeID: device
        )
        return .init(
            entityID: entityID,
            dataClass: dataClass,
            modifiedAt: deletedAt,
            originatingDevice: device,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 1, count: 12),
                ciphertextAndTag: Data(repeating: 2, count: 16)
            ),
            tombstone: .init(
                entityID: entityID,
                deletedAt: deletedAt,
                deletedBy: device,
                originalParentID: nil,
                originalOrderKey: nil,
                purgeAfterMilliseconds: 101
            )
        )
    }
}
