import Foundation

public enum ConflictTieBreakerError: Error, Equatable {
    case unsupportedVersion(UInt16)
    case valueDoesNotMatchCanonicalKey
}

/// Versioned canonical value bytes provide the final total-order component.
/// Raw bytes are retained instead of a digest, so this spike introduces no
/// cryptographic primitive and no hash-collision ambiguity.
public struct ConflictTieBreaker: Codable, Hashable, Sendable, Comparable {
    public static let currentVersion: UInt16 = 1

    public let version: UInt16
    public let canonicalValue: Data

    public init<Value: ConflictCanonicalizable>(canonicalizing value: Value) throws {
        self.version = Self.currentVersion
        self.canonicalValue = try value.stableConflictCanonicalBytes()
    }

    public static func < (lhs: Self, rhs: Self) -> Bool {
        if lhs.version != rhs.version {
            return lhs.version < rhs.version
        }
        return lhs.canonicalValue.lexicographicallyPrecedes(rhs.canonicalValue)
    }

    private enum CodingKeys: String, CodingKey {
        case version
        case canonicalValue
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let version = try container.decode(UInt16.self, forKey: .version)
        guard version == Self.currentVersion else {
            throw ConflictTieBreakerError.unsupportedVersion(version)
        }
        self.version = version
        self.canonicalValue = try container.decode(Data.self, forKey: .canonicalValue)
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(version, forKey: .version)
        try container.encode(canonicalValue, forKey: .canonicalValue)
    }
}

public struct VersionedValue<Value: ConflictCanonicalizable>: Codable, Hashable, Sendable {
    public let value: Value
    public let modifiedAt: HybridLogicalClock
    public let originatingDevice: DeviceID
    public let isTombstone: Bool
    public let conflictTieBreaker: ConflictTieBreaker

    public init(
        value: Value,
        modifiedAt: HybridLogicalClock,
        originatingDevice: DeviceID,
        isTombstone: Bool = false
    ) throws {
        self.value = value
        self.modifiedAt = modifiedAt
        self.originatingDevice = originatingDevice
        self.isTombstone = isTombstone
        self.conflictTieBreaker = try ConflictTieBreaker(canonicalizing: value)
    }

    private enum CodingKeys: String, CodingKey {
        case value
        case modifiedAt
        case originatingDevice
        case isTombstone
        case conflictTieBreaker
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let value = try container.decode(Value.self, forKey: .value)
        let storedTieBreaker = try container.decode(
            ConflictTieBreaker.self,
            forKey: .conflictTieBreaker
        )
        let expectedTieBreaker = try ConflictTieBreaker(canonicalizing: value)
        guard storedTieBreaker == expectedTieBreaker else {
            throw ConflictTieBreakerError.valueDoesNotMatchCanonicalKey
        }

        self.value = value
        self.modifiedAt = try container.decode(
            HybridLogicalClock.self,
            forKey: .modifiedAt
        )
        self.originatingDevice = try container.decode(
            DeviceID.self,
            forKey: .originatingDevice
        )
        self.isTombstone = try container.decode(Bool.self, forKey: .isTombstone)
        self.conflictTieBreaker = storedTieBreaker
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(value, forKey: .value)
        try container.encode(modifiedAt, forKey: .modifiedAt)
        try container.encode(originatingDevice, forKey: .originatingDevice)
        try container.encode(isTombstone, forKey: .isTombstone)
        try container.encode(conflictTieBreaker, forKey: .conflictTieBreaker)
    }
}

public struct LastWriterWinsResolver: Sendable {
    public init() {}

    /// Resolve by a total order: physical/logical HLC, tombstone state,
    /// originating device, HLC node, then versioned canonical value bytes.
    public func resolve<Value>(
        _ lhs: VersionedValue<Value>,
        _ rhs: VersionedValue<Value>
    ) -> VersionedValue<Value> where Value: ConflictCanonicalizable {
        if lhs.modifiedAt.physicalMilliseconds != rhs.modifiedAt.physicalMilliseconds {
            return lhs.modifiedAt.physicalMilliseconds > rhs.modifiedAt.physicalMilliseconds
                ? lhs
                : rhs
        }
        if lhs.modifiedAt.submillisecondMicroseconds !=
            rhs.modifiedAt.submillisecondMicroseconds {
            return lhs.modifiedAt.submillisecondMicroseconds >
                rhs.modifiedAt.submillisecondMicroseconds ? lhs : rhs
        }
        if lhs.modifiedAt.logicalCounter != rhs.modifiedAt.logicalCounter {
            return lhs.modifiedAt.logicalCounter > rhs.modifiedAt.logicalCounter ? lhs : rhs
        }
        if lhs.isTombstone != rhs.isTombstone {
            return lhs.isTombstone ? lhs : rhs
        }
        if lhs.originatingDevice != rhs.originatingDevice {
            return lhs.originatingDevice > rhs.originatingDevice ? lhs : rhs
        }
        if lhs.modifiedAt.nodeID != rhs.modifiedAt.nodeID {
            return lhs.modifiedAt.nodeID > rhs.modifiedAt.nodeID ? lhs : rhs
        }
        if lhs.conflictTieBreaker != rhs.conflictTieBreaker {
            return lhs.conflictTieBreaker > rhs.conflictTieBreaker ? lhs : rhs
        }
        return lhs
    }
}
