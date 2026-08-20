import Foundation

public enum BrowserContextKind: String, Codable, Sendable {
    case normal
    case incognito
}

public struct RemoteTabReference: Codable, Hashable, Sendable {
    public let tabID: TabID
    public let context: BrowserContextKind

    public init(tabID: TabID, context: BrowserContextKind) {
        self.tabID = tabID
        self.context = context
    }
}

public struct RemoteOpenRequest: Codable, Hashable, Sendable {
    public let url: String
    public let workspaceID: WorkspaceID?

    public init(url: String, workspaceID: WorkspaceID? = nil) {
        self.url = url
        self.workspaceID = workspaceID
    }
}

public enum RemoteCommand: Codable, Hashable, Sendable {
    case open(RemoteOpenRequest)
    case focus(RemoteTabReference)
    case close([RemoteTabReference])

    private enum CodingKeys: String, CodingKey {
        case kind
        case openRequest
        case tabReference
        case tabReferences
    }

    private enum Kind: String, Codable {
        case open
        case focus
        case close
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        switch try container.decode(Kind.self, forKey: .kind) {
        case .open:
            self = .open(try container.decode(RemoteOpenRequest.self, forKey: .openRequest))
        case .focus:
            self = .focus(try container.decode(RemoteTabReference.self, forKey: .tabReference))
        case .close:
            self = .close(try container.decode([RemoteTabReference].self, forKey: .tabReferences))
        }
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case let .open(request):
            try container.encode(Kind.open, forKey: .kind)
            try container.encode(request, forKey: .openRequest)
        case let .focus(reference):
            try container.encode(Kind.focus, forKey: .kind)
            try container.encode(reference, forKey: .tabReference)
        case let .close(references):
            try container.encode(Kind.close, forKey: .kind)
            try container.encode(references, forKey: .tabReferences)
        }
    }
}

public struct RemoteCommandPayload: Codable, Hashable, Sendable {
    public static let timeToLiveMilliseconds: UInt64 = 5 * 60 * 1_000

    public let commandID: UUID
    public let sourceDeviceID: DeviceID
    public let targetDeviceID: DeviceID
    public let nonce: Data
    public let issuedAtMilliseconds: UInt64
    public let command: RemoteCommand

    public init(
        commandID: UUID = UUID(),
        sourceDeviceID: DeviceID,
        targetDeviceID: DeviceID,
        nonce: Data,
        issuedAtMilliseconds: UInt64,
        command: RemoteCommand
    ) {
        self.commandID = commandID
        self.sourceDeviceID = sourceDeviceID
        self.targetDeviceID = targetDeviceID
        self.nonce = nonce
        self.issuedAtMilliseconds = issuedAtMilliseconds
        self.command = command
    }

    public var expiresAtMilliseconds: UInt64 {
        let (result, overflow) = issuedAtMilliseconds.addingReportingOverflow(
            Self.timeToLiveMilliseconds
        )
        return overflow ? UInt64.max : result
    }

    public func canonicalData() throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        return try encoder.encode(self)
    }
}

public struct SignedRemoteCommand: Codable, Hashable, Sendable {
    public let payload: RemoteCommandPayload
    public let signature: Data

    public init(payload: RemoteCommandPayload, signature: Data) {
        self.payload = payload
        self.signature = signature
    }
}
