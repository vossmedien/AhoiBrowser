import Foundation

public enum HybridLogicalClockError: Error, Equatable {
    case logicalCounterExhausted
}

/// A deterministic hybrid logical timestamp. Wall-clock milliseconds provide
/// the coarse order; the counter preserves causality when clocks tie or move
/// backwards; the node ID is the final deterministic tie-breaker.
public struct HybridLogicalClock: Codable, Hashable, Sendable, Comparable {
    public let physicalMilliseconds: UInt64
    public let submillisecondMicroseconds: UInt16
    public let logicalCounter: UInt32
    public let nodeID: DeviceID

    public init(
        physicalMilliseconds: UInt64,
        submillisecondMicroseconds: UInt16 = 0,
        logicalCounter: UInt32 = 0,
        nodeID: DeviceID
    ) {
        precondition(submillisecondMicroseconds < 1_000)
        self.physicalMilliseconds = physicalMilliseconds
        self.submillisecondMicroseconds = submillisecondMicroseconds
        self.logicalCounter = logicalCounter
        self.nodeID = nodeID
    }

    public static func < (lhs: Self, rhs: Self) -> Bool {
        if lhs.physicalMilliseconds != rhs.physicalMilliseconds {
            return lhs.physicalMilliseconds < rhs.physicalMilliseconds
        }
        if lhs.submillisecondMicroseconds != rhs.submillisecondMicroseconds {
            return lhs.submillisecondMicroseconds < rhs.submillisecondMicroseconds
        }
        if lhs.logicalCounter != rhs.logicalCounter {
            return lhs.logicalCounter < rhs.logicalCounter
        }
        return lhs.nodeID < rhs.nodeID
    }

    public func ticking(at wallTimeMilliseconds: UInt64) throws -> Self {
        if wallTimeMilliseconds > physicalMilliseconds {
            return Self(
                physicalMilliseconds: wallTimeMilliseconds,
                submillisecondMicroseconds: 0,
                logicalCounter: 0,
                nodeID: nodeID
            )
        }

        guard logicalCounter < UInt32.max else {
            throw HybridLogicalClockError.logicalCounterExhausted
        }
        return Self(
            physicalMilliseconds: physicalMilliseconds,
            submillisecondMicroseconds: submillisecondMicroseconds,
            logicalCounter: logicalCounter + 1,
            nodeID: nodeID
        )
    }

    public func merging(
        _ remote: Self,
        at wallTimeMilliseconds: UInt64
    ) throws -> Self {
        let localPhysical = (physicalMilliseconds, submillisecondMicroseconds)
        let remotePhysical = (
            remote.physicalMilliseconds,
            remote.submillisecondMicroseconds
        )
        let wallPhysical = (wallTimeMilliseconds, UInt16(0))
        func later(
            _ lhs: (UInt64, UInt16),
            _ rhs: (UInt64, UInt16)
        ) -> (UInt64, UInt16) {
            if lhs.0 != rhs.0 { return lhs.0 > rhs.0 ? lhs : rhs }
            return lhs.1 >= rhs.1 ? lhs : rhs
        }
        let physical = later(wallPhysical, later(localPhysical, remotePhysical))

        let nextCounter: UInt32
        if physical == localPhysical && physical == remotePhysical {
            let counter = max(logicalCounter, remote.logicalCounter)
            guard counter < UInt32.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            nextCounter = counter + 1
        } else if physical == localPhysical {
            guard logicalCounter < UInt32.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            nextCounter = logicalCounter + 1
        } else if physical == remotePhysical {
            guard remote.logicalCounter < UInt32.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            nextCounter = remote.logicalCounter + 1
        } else {
            nextCounter = 0
        }

        return Self(
            physicalMilliseconds: physical.0,
            submillisecondMicroseconds: physical.1,
            logicalCounter: nextCounter,
            nodeID: nodeID
        )
    }

    public var physicalMicroseconds: UInt64 {
        physicalMilliseconds * 1_000 + UInt64(submillisecondMicroseconds)
    }

    private enum CodingKeys: String, CodingKey {
        case physicalMilliseconds, submillisecondMicroseconds, logicalCounter, nodeID
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let submillisecondMicroseconds = try container.decodeIfPresent(
            UInt16.self,
            forKey: .submillisecondMicroseconds
        ) ?? 0
        guard submillisecondMicroseconds < 1_000 else {
            throw DecodingError.dataCorruptedError(
                forKey: .submillisecondMicroseconds,
                in: container,
                debugDescription: "Submillisecond microseconds must be below 1000"
            )
        }
        self.init(
            physicalMilliseconds: try container.decode(
                UInt64.self,
                forKey: .physicalMilliseconds
            ),
            submillisecondMicroseconds: submillisecondMicroseconds,
            logicalCounter: try container.decode(UInt32.self, forKey: .logicalCounter),
            nodeID: try container.decode(DeviceID.self, forKey: .nodeID)
        )
    }
}
