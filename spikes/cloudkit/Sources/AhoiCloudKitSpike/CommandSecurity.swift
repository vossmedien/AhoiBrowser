import Foundation

public protocol DeviceApprovalChecking: Sendable {
    func isApproved(source: DeviceID, for target: DeviceID) async -> Bool
}

public protocol RemoteControlEnablementChecking: Sendable {
    func isRemoteControlEnabled(for localDeviceID: DeviceID) async -> Bool
}

public struct StaticRemoteControlSetting: RemoteControlEnablementChecking {
    public let isEnabled: Bool

    public init(isEnabled: Bool) {
        self.isEnabled = isEnabled
    }

    public func isRemoteControlEnabled(for localDeviceID: DeviceID) async -> Bool {
        isEnabled
    }
}

public struct DeviceApprovalPair: Hashable, Sendable {
    public let source: DeviceID
    public let target: DeviceID

    public init(source: DeviceID, target: DeviceID) {
        self.source = source
        self.target = target
    }
}

public struct StaticDeviceApprovalStore: DeviceApprovalChecking {
    private let approvedPairs: Set<DeviceApprovalPair>

    public init(approvedPairs: Set<DeviceApprovalPair>) {
        self.approvedPairs = approvedPairs
    }

    public func isApproved(source: DeviceID, for target: DeviceID) async -> Bool {
        approvedPairs.contains(.init(source: source, target: target))
    }
}

public protocol CommandSignatureVerifying: Sendable {
    func verify(
        signature: Data,
        message: Data,
        sourceDeviceID: DeviceID
    ) async throws -> Bool
}

public protocol CommandReplayChecking: Sendable {
    /// Atomically returns true only for the first observation of a nonce.
    func consume(
        commandID: UUID,
        nonce: Data,
        sourceDeviceID: DeviceID,
        expiresAtMilliseconds: UInt64,
        nowMilliseconds: UInt64
    ) async -> Bool
}

public actor InMemoryCommandReplayStore: CommandReplayChecking {
    private var commandExpirations: [UUID: UInt64] = [:]
    private var nonceExpirations: [String: UInt64] = [:]

    public init() {}

    public func consume(
        commandID: UUID,
        nonce: Data,
        sourceDeviceID: DeviceID,
        expiresAtMilliseconds: UInt64,
        nowMilliseconds: UInt64
    ) -> Bool {
        commandExpirations = commandExpirations.filter { $0.value > nowMilliseconds }
        nonceExpirations = nonceExpirations.filter { $0.value > nowMilliseconds }
        let key = sourceDeviceID.rawValue.uuidString + ":" + nonce.base64EncodedString()
        guard commandExpirations[commandID] == nil,
              nonceExpirations[key] == nil else {
            return false
        }
        commandExpirations[commandID] = expiresAtMilliseconds
        nonceExpirations[key] = expiresAtMilliseconds
        return true
    }
}

public enum RemoteCommandValidationError: Error, Equatable {
    case wrongTarget
    case remoteControlDisabled
    case issuedInFuture
    case expired
    case invalidNonce
    case unapprovedDevice
    case invalidSignature
    case malformedURL
    case unsupportedURLScheme(String?)
    case urlUserInfoForbidden
    case incognitoForbidden
    case massActionForbidden
}

public enum RemoteCommandSemantics {
    public static func validate(_ command: RemoteCommand) throws {
        switch command {
        case let .open(request):
            guard let components = URLComponents(string: request.url) else {
                throw RemoteCommandValidationError.malformedURL
            }
            let scheme = components.scheme?.lowercased()
            guard scheme == "http" || scheme == "https" else {
                throw RemoteCommandValidationError.unsupportedURLScheme(scheme)
            }
            guard components.user == nil, components.password == nil else {
                throw RemoteCommandValidationError.urlUserInfoForbidden
            }
            guard components.url != nil,
                  components.host?.isEmpty == false else {
                throw RemoteCommandValidationError.malformedURL
            }
        case let .focus(reference):
            guard reference.context == .normal else {
                throw RemoteCommandValidationError.incognitoForbidden
            }
        case let .close(references):
            guard references.count == 1 else {
                throw RemoteCommandValidationError.massActionForbidden
            }
            guard references[0].context == .normal else {
                throw RemoteCommandValidationError.incognitoForbidden
            }
        }
    }
}

public struct RemoteCommandValidator: Sendable {
    public static let allowedFutureClockSkewMilliseconds: UInt64 = 60_000

    private let localDeviceID: DeviceID
    private let enablement: any RemoteControlEnablementChecking
    private let approvalStore: any DeviceApprovalChecking
    private let signatureVerifier: any CommandSignatureVerifying
    private let replayStore: any CommandReplayChecking

    public init(
        localDeviceID: DeviceID,
        enablement: any RemoteControlEnablementChecking,
        approvalStore: any DeviceApprovalChecking,
        signatureVerifier: any CommandSignatureVerifying,
        replayStore: any CommandReplayChecking
    ) {
        self.localDeviceID = localDeviceID
        self.enablement = enablement
        self.approvalStore = approvalStore
        self.signatureVerifier = signatureVerifier
        self.replayStore = replayStore
    }

    public func validate(
        _ envelope: SignedRemoteCommand,
        nowMilliseconds: UInt64
    ) async throws -> RemoteCommand {
        let payload = envelope.payload
        guard payload.targetDeviceID == localDeviceID else {
            throw RemoteCommandValidationError.wrongTarget
        }
        guard await enablement.isRemoteControlEnabled(for: localDeviceID) else {
            throw RemoteCommandValidationError.remoteControlDisabled
        }

        let latestAcceptedIssueTime: UInt64
        let (futureLimit, overflow) = nowMilliseconds.addingReportingOverflow(
            Self.allowedFutureClockSkewMilliseconds
        )
        latestAcceptedIssueTime = overflow ? UInt64.max : futureLimit
        guard payload.issuedAtMilliseconds <= latestAcceptedIssueTime else {
            throw RemoteCommandValidationError.issuedInFuture
        }
        guard nowMilliseconds < payload.expiresAtMilliseconds else {
            throw RemoteCommandValidationError.expired
        }
        guard (16...64).contains(payload.nonce.count) else {
            throw RemoteCommandValidationError.invalidNonce
        }

        try RemoteCommandSemantics.validate(payload.command)

        guard await approvalStore.isApproved(
            source: payload.sourceDeviceID,
            for: payload.targetDeviceID
        ) else {
            throw RemoteCommandValidationError.unapprovedDevice
        }

        let message = try payload.canonicalData()
        guard try await signatureVerifier.verify(
            signature: envelope.signature,
            message: message,
            sourceDeviceID: payload.sourceDeviceID
        ) else {
            throw RemoteCommandValidationError.invalidSignature
        }

        guard await replayStore.consume(
            commandID: payload.commandID,
            nonce: payload.nonce,
            sourceDeviceID: payload.sourceDeviceID,
            expiresAtMilliseconds: payload.expiresAtMilliseconds,
            nowMilliseconds: nowMilliseconds
        ) else {
            throw RemoteCommandValidationError.invalidNonce
        }

        return payload.command
    }
}

#if canImport(CryptoKit)
import CryptoKit

/// Production-shaped verifier for per-device Ed25519 public keys. Key
/// distribution and approval remain outside this type by design.
public struct Ed25519CommandSignatureVerifier: CommandSignatureVerifying {
    private let publicKeyByDevice: [DeviceID: Data]

    public init(publicKeyByDevice: [DeviceID: Data]) {
        self.publicKeyByDevice = publicKeyByDevice
    }

    public func verify(
        signature: Data,
        message: Data,
        sourceDeviceID: DeviceID
    ) async throws -> Bool {
        guard let rawKey = publicKeyByDevice[sourceDeviceID] else {
            return false
        }
        let publicKey = try Curve25519.Signing.PublicKey(rawRepresentation: rawKey)
        return publicKey.isValidSignature(signature, for: message)
    }
}
#endif
