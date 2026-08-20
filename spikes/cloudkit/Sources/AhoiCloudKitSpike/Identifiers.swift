import Foundation

public protocol UUIDIdentifier: Codable, Hashable, Sendable, Comparable {
    var rawValue: UUID { get }
    init(rawValue: UUID)
}

public extension UUIDIdentifier {
    init() {
        self.init(rawValue: UUID())
    }

    static func < (lhs: Self, rhs: Self) -> Bool {
        lhs.rawValue.uuidString < rhs.rawValue.uuidString
    }
}

public struct DeviceID: UUIDIdentifier {
    public let rawValue: UUID

    public init(rawValue: UUID) {
        self.rawValue = rawValue
    }
}

public struct WorkspaceID: UUIDIdentifier {
    public let rawValue: UUID

    public init(rawValue: UUID) {
        self.rawValue = rawValue
    }
}

public struct TreeNodeID: UUIDIdentifier {
    public let rawValue: UUID

    public init(rawValue: UUID) {
        self.rawValue = rawValue
    }
}

public struct TabID: UUIDIdentifier {
    public let rawValue: UUID

    public init(rawValue: UUID) {
        self.rawValue = rawValue
    }
}
