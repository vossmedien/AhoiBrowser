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

/// Identifies one durable browser session on a device. It is deliberately
/// separate from `DeviceID`: a device can publish a new session after a
/// reinstall or a profile reset without making its device identity unstable.
public struct DeviceSessionID: UUIDIdentifier {
    public let rawValue: UUID

    public init(rawValue: UUID) {
        self.rawValue = rawValue
    }
}

/// A history visit has its own identity so append-only history can be
/// de-duplicated without treating equal URLs as the same visit.
public struct HistoryVisitID: UUIDIdentifier {
    public let rawValue: UUID

    public init(rawValue: UUID) {
        self.rawValue = rawValue
    }
}
