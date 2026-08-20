import Foundation

public enum HybridLogicalClockError: Error, Equatable {
    case logicalCounterExhausted
}

/// A deterministic hybrid logical timestamp. Wall-clock milliseconds provide
/// the coarse order; the counter preserves causality when clocks tie or move
/// backwards; the node ID is the final deterministic tie-breaker.
public struct HybridLogicalClock: Codable, Hashable, Sendable, Comparable {
    public let physicalMilliseconds: UInt64
    public let logicalCounter: UInt32
    public let nodeID: DeviceID

    public init(
        physicalMilliseconds: UInt64,
        logicalCounter: UInt32 = 0,
        nodeID: DeviceID
    ) {
        self.physicalMilliseconds = physicalMilliseconds
        self.logicalCounter = logicalCounter
        self.nodeID = nodeID
    }

    public static func < (lhs: Self, rhs: Self) -> Bool {
        if lhs.physicalMilliseconds != rhs.physicalMilliseconds {
            return lhs.physicalMilliseconds < rhs.physicalMilliseconds
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
                logicalCounter: 0,
                nodeID: nodeID
            )
        }

        guard logicalCounter < UInt32.max else {
            throw HybridLogicalClockError.logicalCounterExhausted
        }
        return Self(
            physicalMilliseconds: physicalMilliseconds,
            logicalCounter: logicalCounter + 1,
            nodeID: nodeID
        )
    }

    public func merging(
        _ remote: Self,
        at wallTimeMilliseconds: UInt64
    ) throws -> Self {
        let physical = max(
            wallTimeMilliseconds,
            max(physicalMilliseconds, remote.physicalMilliseconds)
        )

        let nextCounter: UInt32
        if physical == physicalMilliseconds && physical == remote.physicalMilliseconds {
            let counter = max(logicalCounter, remote.logicalCounter)
            guard counter < UInt32.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            nextCounter = counter + 1
        } else if physical == physicalMilliseconds {
            guard logicalCounter < UInt32.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            nextCounter = logicalCounter + 1
        } else if physical == remote.physicalMilliseconds {
            guard remote.logicalCounter < UInt32.max else {
                throw HybridLogicalClockError.logicalCounterExhausted
            }
            nextCounter = remote.logicalCounter + 1
        } else {
            nextCounter = 0
        }

        return Self(
            physicalMilliseconds: physical,
            logicalCounter: nextCounter,
            nodeID: nodeID
        )
    }
}
