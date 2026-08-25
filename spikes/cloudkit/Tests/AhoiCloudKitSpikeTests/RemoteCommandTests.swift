import Foundation
import XCTest
@testable import AhoiCloudKitSpike

#if canImport(CryptoKit)
import CryptoKit
#endif

final class RemoteCommandTests: XCTestCase {
    private let source = DeviceID(rawValue: UUID(uuidString: "a0000000-0000-4000-8000-000000000001")!)
    private let target = DeviceID(rawValue: UUID(uuidString: "b0000000-0000-4000-8000-000000000002")!)
    private let now: UInt64 = 1_000_000

    func testValidSignedOpenCommandIsAccepted() async throws {
        #if canImport(CryptoKit)
        let privateKey = Curve25519.Signing.PrivateKey()
        let payload = makePayload(command: .open(.init(url: "https://example.test/path")))
        let signature = try privateKey.signature(for: payload.canonicalData())
        let verifier = Ed25519CommandSignatureVerifier(
            publicKeyByDevice: [source: privateKey.publicKey.rawRepresentation]
        )
        let validator = makeValidator(signatureVerifier: verifier)

        let result = try await validator.validate(
            .init(payload: payload, signature: signature),
            nowMilliseconds: now
        )

        XCTAssertEqual(result, payload.command)
        #else
        throw XCTSkip("CryptoKit unavailable")
        #endif
    }

    func testReplayedNonceIsRejectedAtomically() async throws {
        let payload = makePayload(command: .open(.init(url: "https://example.test")))
        let envelope = SignedRemoteCommand(payload: payload, signature: Data([1]))
        let validator = makeValidator(signatureVerifier: AcceptingSignatureVerifier())

        _ = try await validator.validate(envelope, nowMilliseconds: now)
        await assertValidationError(.invalidNonce) {
            try await validator.validate(envelope, nowMilliseconds: self.now)
        }
    }

    func testPersistentReplayRejectsCommandIDAndSourceNonceAfterRestart() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiCommandReplay-\(UUID().uuidString)")
        let fileURL = directory.appendingPathComponent("replay.json")
        defer { try? FileManager.default.removeItem(at: directory) }
        let commandID = UUID(uuidString: "c0000000-0000-4000-8000-000000000003")!
        let firstNonce = Data(repeating: 1, count: 32)
        let first = FileCommandReplayStore(fileURL: fileURL)

        let accepted = await first.consume(
            commandID: commandID,
            nonce: firstNonce,
            sourceDeviceID: source,
            expiresAtMilliseconds: now + 300_000,
            nowMilliseconds: now
        )
        XCTAssertTrue(accepted)

        let restarted = FileCommandReplayStore(fileURL: fileURL)
        let repeatedCommand = await restarted.consume(
            commandID: commandID,
            nonce: Data(repeating: 2, count: 32),
            sourceDeviceID: source,
            expiresAtMilliseconds: now + 300_000,
            nowMilliseconds: now
        )
        let repeatedNonce = await restarted.consume(
            commandID: UUID(),
            nonce: firstNonce,
            sourceDeviceID: source,
            expiresAtMilliseconds: now + 300_000,
            nowMilliseconds: now
        )
        XCTAssertFalse(repeatedCommand)
        XCTAssertFalse(repeatedNonce)
    }

    func testWrongTargetIsRejected() async {
        let payload = makePayload(command: .open(.init(url: "https://example.test")))
        let validator = RemoteCommandValidator(
            localDeviceID: DeviceID(),
            enablement: StaticRemoteControlSetting(isEnabled: true),
            approvalStore: StaticDeviceApprovalStore(
                approvedPairs: [.init(source: source, target: target)]
            ),
            signatureVerifier: AcceptingSignatureVerifier(),
            replayStore: InMemoryCommandReplayStore()
        )
        await assertValidationError(.wrongTarget) {
            try await validator.validate(
                .init(payload: payload, signature: Data([1])),
                nowMilliseconds: self.now
            )
        }
    }

    func testCommandExpiresAtExactlyFiveMinutes() async {
        let payload = makePayload(
            issuedAt: now - RemoteCommandPayload.timeToLiveMilliseconds,
            command: .open(.init(url: "https://example.test"))
        )
        await assertValidationError(.expired) {
            try await self.makeValidator().validate(
                .init(payload: payload, signature: Data([1])),
                nowMilliseconds: self.now
            )
        }
    }

    func testDangerousAndCustomSchemesAreRejected() async {
        for url in [
            "javascript:alert(1)",
            "file:///etc/passwd",
            "data:text/html,test",
            "ahoi://internal/action",
        ] {
            let payload = makePayload(command: .open(.init(url: url)))
            do {
                _ = try await makeValidator().validate(
                    .init(payload: payload, signature: Data([1])),
                    nowMilliseconds: now
                )
                XCTFail("Expected \(url) to be rejected")
            } catch let error as RemoteCommandValidationError {
                switch error {
                case .unsupportedURLScheme, .malformedURL:
                    break
                default:
                    XCTFail("Unexpected error for \(url): \(error)")
                }
            } catch {
                XCTFail("Unexpected error type for \(url): \(error)")
            }
        }
    }

    func testURLUserInfoIsRejectedAsSecretTransfer() async {
        for url in [
            "https://user:password@example.test/private",
            "https://token@example.test/private",
        ] {
            let payload = makePayload(command: .open(.init(url: url)))
            await assertValidationError(.urlUserInfoForbidden) {
                try await self.makeValidator().validate(
                    .init(payload: payload, signature: Data([1])),
                    nowMilliseconds: self.now
                )
            }
        }
    }

    func testRemoteControlDisabledRejectsBeforeExecution() async {
        let payload = makePayload(command: .open(.init(url: "https://example.test")))
        await assertValidationError(.remoteControlDisabled) {
            try await self.makeValidator(remoteControlEnabled: false).validate(
                .init(payload: payload, signature: Data([1])),
                nowMilliseconds: self.now
            )
        }
    }

    func testIncognitoFocusAndCloseAreRejected() async {
        let incognito = RemoteTabReference(tabID: TabID(), context: .incognito)
        for command in [RemoteCommand.focus(incognito), .close([incognito])] {
            let payload = makePayload(command: command)
            await assertValidationError(.incognitoForbidden) {
                try await self.makeValidator().validate(
                    .init(payload: payload, signature: Data([1])),
                    nowMilliseconds: self.now
                )
            }
        }
    }

    func testMassCloseAndEmptyCloseAreRejected() async {
        let first = RemoteTabReference(tabID: TabID(), context: .normal)
        let second = RemoteTabReference(tabID: TabID(), context: .normal)
        for references in [[], [first, second]] {
            let payload = makePayload(command: .close(references))
            await assertValidationError(.massActionForbidden) {
                try await self.makeValidator().validate(
                    .init(payload: payload, signature: Data([1])),
                    nowMilliseconds: self.now
                )
            }
        }
    }

    func testUnapprovedDeviceAndInvalidSignatureAreRejected() async {
        let payload = makePayload(command: .open(.init(url: "https://example.test")))
        let envelope = SignedRemoteCommand(payload: payload, signature: Data([1]))
        let noApproval = StaticDeviceApprovalStore(approvedPairs: [])
        let unapprovedValidator = RemoteCommandValidator(
            localDeviceID: target,
            enablement: StaticRemoteControlSetting(isEnabled: true),
            approvalStore: noApproval,
            signatureVerifier: AcceptingSignatureVerifier(),
            replayStore: InMemoryCommandReplayStore()
        )
        await assertValidationError(.unapprovedDevice) {
            try await unapprovedValidator.validate(envelope, nowMilliseconds: self.now)
        }

        await assertValidationError(.invalidSignature) {
            try await self.makeValidator(signatureVerifier: RejectingSignatureVerifier())
                .validate(envelope, nowMilliseconds: self.now)
        }
    }

    func testCanonicalPayloadEncodingMatchesSignedWireContract() throws {
        let payload = makePayload(command: .open(.init(url: "https://example.test/a/b")))
        let expected = #"{"command":{"kind":"open","openRequest":{"url":"https://example.test/a/b"}},"commandID":"C0000000-0000-4000-8000-000000000003","issuedAtMilliseconds":1000000,"nonce":"AAAAAAAAAAAAAAAAAAAAAA==","sourceDeviceID":{"rawValue":"A0000000-0000-4000-8000-000000000001"},"targetDeviceID":{"rawValue":"B0000000-0000-4000-8000-000000000002"}}"#

        XCTAssertEqual(String(decoding: try payload.canonicalData(), as: UTF8.self), expected)
    }

    private func makePayload(
        issuedAt: UInt64? = nil,
        command: RemoteCommand
    ) -> RemoteCommandPayload {
        .init(
            commandID: UUID(uuidString: "c0000000-0000-4000-8000-000000000003")!,
            sourceDeviceID: source,
            targetDeviceID: target,
            nonce: Data(repeating: 0, count: 16),
            issuedAtMilliseconds: issuedAt ?? now,
            command: command
        )
    }

    private func makeValidator(
        remoteControlEnabled: Bool = true,
        signatureVerifier: any CommandSignatureVerifying = AcceptingSignatureVerifier()
    ) -> RemoteCommandValidator {
        .init(
            localDeviceID: target,
            enablement: StaticRemoteControlSetting(isEnabled: remoteControlEnabled),
            approvalStore: StaticDeviceApprovalStore(
                approvedPairs: [.init(source: source, target: target)]
            ),
            signatureVerifier: signatureVerifier,
            replayStore: InMemoryCommandReplayStore()
        )
    }

    private func assertValidationError(
        _ expected: RemoteCommandValidationError,
        operation: () async throws -> RemoteCommand,
        file: StaticString = #filePath,
        line: UInt = #line
    ) async {
        do {
            _ = try await operation()
            XCTFail("Expected \(expected)", file: file, line: line)
        } catch let error as RemoteCommandValidationError {
            XCTAssertEqual(error, expected, file: file, line: line)
        } catch {
            XCTFail("Unexpected error: \(error)", file: file, line: line)
        }
    }
}

private struct AcceptingSignatureVerifier: CommandSignatureVerifying {
    func verify(
        signature: Data,
        message: Data,
        sourceDeviceID: DeviceID
    ) async throws -> Bool {
        !signature.isEmpty && !message.isEmpty
    }
}

private struct RejectingSignatureVerifier: CommandSignatureVerifying {
    func verify(
        signature: Data,
        message: Data,
        sourceDeviceID: DeviceID
    ) async throws -> Bool {
        false
    }
}
