import XCTest
@testable import AhoiMobileCore
import AhoiCloudKitSpike

#if canImport(CryptoKit)
import CryptoKit
#endif

final class CompanionCryptographyTests: XCTestCase {
    func testRemoteCommandWireMatchesMacGoldenPayloadAndRoundTrips() throws {
        let source = DeviceID(
            rawValue: UUID(uuidString: "10000000-0000-4000-8000-000000000001")!
        )
        let target = DeviceID(
            rawValue: UUID(uuidString: "20000000-0000-4000-8000-000000000002")!
        )
        let commandID = UUID(uuidString: "30000000-0000-4000-8000-000000000003")!
        let clock = HybridLogicalClock(
            physicalMilliseconds: 1_000_000,
            nodeID: source
        )
        let state = RemoteCommandState(
            envelope: .init(
                payload: .init(
                    commandID: commandID,
                    sourceDeviceID: source,
                    targetDeviceID: target,
                    nonce: Data(repeating: 0, count: 16),
                    issuedAtMilliseconds: 1_000_000,
                    command: .open(.init(url: "https://example.test/a/b"))
                ),
                signature: Data(repeating: 0, count: 64)
            ),
            version: .init(modifiedAt: clock, modifiedBy: source)
        )
        let codec = DesktopWirePayloadCodec()
        let payload = try codec.encode(state)
        let expected = #"{"command_kind":0,"expires_at":"11644474900000000","field_versions":{"request":{"device":"10000000-0000-4000-8000-000000000001","logical":0,"physical":"11644474600000000"},"status":{"device":"10000000-0000-4000-8000-000000000001","logical":0,"physical":"11644474600000000"},"tombstone":{"device":"10000000-0000-4000-8000-000000000001","logical":0,"physical":"11644474600000000"}},"id":"30000000-0000-4000-8000-000000000003","issued_at":"11644474600000000","model_version":2,"nonce":"AAAAAAAAAAAAAAAAAAAAAA==","result":"","signature":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==","source_device_id":"10000000-0000-4000-8000-000000000001","status":0,"target_device_id":"20000000-0000-4000-8000-000000000002","tombstone":false,"url":"https://example.test/a/b","version_device":"10000000-0000-4000-8000-000000000001","version_logical":0,"version_model":2,"version_physical":"11644474600000000"}"#
        XCTAssertEqual(String(decoding: payload, as: UTF8.self), expected)

        let record = SyncRecord(
            recordID: commandID,
            entityID: commandID,
            dataClass: .remoteCommand,
            modifiedAt: clock,
            originatingDevice: source,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: Data(repeating: 0, count: 12),
                ciphertextAndTag: Data(repeating: 0, count: 16)
            )
        )
        let decoded = try codec.decodeRemoteCommand(record, plaintext: payload)
        XCTAssertEqual(decoded.envelope, state.envelope)
        XCTAssertEqual(decoded.status, state.status)
        XCTAssertEqual(decoded.resultCode, state.resultCode)
        XCTAssertEqual(
            Set(decoded.version.fieldVersions.keys),
            ["request", "status", "tombstone"]
        )
        XCTAssertEqual(try codec.encode(decoded), payload)
    }

    func testWorkspaceWirePreservesIconColorAndCanonicalOrder() throws {
        let device = DeviceID()
        let workspace = Workspace(
            workspaceID: WorkspaceID(),
            name: "Produkt",
            icon: "🧭",
            accent: "#ff3366aa",
            sortKey: "000a!device",
            version: makeVersion(device: device, milliseconds: 1_000)
        )

        let object = try DesktopWirePayloadCodec().object(
            from: DesktopWirePayloadCodec().encode(workspace)
        )

        XCTAssertEqual(object["icon"] as? String, "🧭")
        XCTAssertEqual(object["sort_key"] as? String, "000a!device")
        XCTAssertEqual((object["accent_argb"] as? NSNumber)?.uint32Value, 0xff3366aa)
    }

    func testEd25519SignerUsesProvisionedBytesWithoutCreatingKeys() throws {
#if canImport(CryptoKit) && canImport(Security)
        let source = DeviceID()
        let privateKey = Curve25519.Signing.PrivateKey()
        let signer = KeychainRemoteCommandSigner(
            configuration: .init(
                service: "test.invalid",
                account: "fixture",
                sourceDeviceID: source
            ),
            keyLoader: { privateKey.rawRepresentation },
            nonceLoader: { Data(repeating: 9, count: 32) }
        )
        let payload = RemoteCommandPayload(
            sourceDeviceID: source,
            targetDeviceID: DeviceID(),
            nonce: try signer.makeNonce(),
            issuedAtMilliseconds: 1_000,
            command: .open(.init(url: "https://example.test"))
        )

        let signed = try signer.sign(payload)

        XCTAssertTrue(privateKey.publicKey.isValidSignature(
            signed.signature,
            for: try payload.canonicalData()
        ))
        XCTAssertEqual(
            try signer.provisioningIdentity().publicKeyBase64,
            privateKey.publicKey.rawRepresentation.base64EncodedString()
        )
#else
        throw XCTSkip("CryptoKit/Security unavailable")
#endif
    }

    func testKeychainSealerCryptoKitInteropShapeWithoutKeychainMutation() throws {
        let configuration = CompanionSyncKeyConfiguration(
            service: "test.invalid",
            account: "fixture",
            keyVersion: 7
        )
        let sealer = KeychainCompanionPayloadSealer(
            configuration: configuration,
            keyLoader: { Data(repeating: 0x42, count: 32) }
        )
        let plaintext = Data("private payload".utf8)

        let sealed = try sealer.seal(plaintext)

        XCTAssertEqual(sealed.algorithm, .aes256GCM)
        XCTAssertEqual(sealed.keyVersion, 7)
        XCTAssertEqual(sealed.nonce.count, 12)
        XCTAssertGreaterThanOrEqual(sealed.ciphertextAndTag.count, 16)
        XCTAssertEqual(try sealer.open(sealed), plaintext)
    }

    func testOpensSharedDesktopAESGCMGoldenEnvelope() throws {
        let configuration = CompanionSyncKeyConfiguration(
            service: "test.invalid",
            account: "fixture",
            keyVersion: 1
        )
        let sealer = KeychainCompanionPayloadSealer(
            configuration: configuration,
            keyLoader: { Data(repeating: 0, count: 32) }
        )
        let value = EncryptedValue(
            keyVersion: 1,
            nonce: try XCTUnwrap(Data(base64Encoded: "AAAAAAAAAAAAAAAA")),
            ciphertextAndTag: try XCTUnwrap(Data(
                base64Encoded: "zqdAPU1ga24HTsXTuvOdGNDRyKeZmWvwJluYtdSKuRk="
            ))
        )

        XCTAssertEqual(try sealer.open(value), Data(repeating: 0, count: 16))
    }

    private func makeVersion(device: DeviceID, milliseconds: UInt64) -> SyncVersion {
        SyncVersion(
            modifiedAt: .init(
                physicalMilliseconds: milliseconds,
                nodeID: device
            ),
            modifiedBy: device
        )
    }
}
