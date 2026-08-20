import Foundation

public enum OrderKeyError: Error, Equatable {
    case invalidBounds
    case depthLimitReached
    case invalidComponentCount(Int)
}

/// A fractional, lexicographically ordered position plus a device tie-breaker.
/// New positions can normally be inserted without renumbering their siblings.
public struct OrderKey: Codable, Hashable, Sendable, Comparable {
    public static let maximumDepth = 64

    public let components: [UInt16]
    public let tieBreaker: DeviceID

    public init(components: [UInt16], tieBreaker: DeviceID) throws {
        guard !components.isEmpty, components.count <= Self.maximumDepth else {
            throw OrderKeyError.invalidComponentCount(components.count)
        }
        self.components = components
        self.tieBreaker = tieBreaker
    }

    private enum CodingKeys: String, CodingKey {
        case components
        case tieBreaker
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let components = try container.decode([UInt16].self, forKey: .components)
        let tieBreaker = try container.decode(DeviceID.self, forKey: .tieBreaker)
        try self.init(components: components, tieBreaker: tieBreaker)
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(components, forKey: .components)
        try container.encode(tieBreaker, forKey: .tieBreaker)
    }

    public static func < (lhs: Self, rhs: Self) -> Bool {
        let commonCount = min(lhs.components.count, rhs.components.count)
        for index in 0..<commonCount where lhs.components[index] != rhs.components[index] {
            return lhs.components[index] < rhs.components[index]
        }

        if lhs.components.count != rhs.components.count {
            return lhs.components.count < rhs.components.count
        }
        return lhs.tieBreaker < rhs.tieBreaker
    }

    public static func between(
        _ lower: Self?,
        _ upper: Self?,
        tieBreaker: DeviceID
    ) throws -> Self {
        if let lower, let upper, !(lower < upper) {
            throw OrderKeyError.invalidBounds
        }

        var prefix: [UInt16] = []
        for depth in 0..<maximumDepth {
            let lowerDigit = digit(at: depth, in: lower, missing: UInt16.min)
            let upperDigit = digit(at: depth, in: upper, missing: UInt16.max)

            guard lowerDigit <= upperDigit else {
                throw OrderKeyError.invalidBounds
            }

            let gap = UInt32(upperDigit) - UInt32(lowerDigit)
            if gap > 1 {
                prefix.append(UInt16(UInt32(lowerDigit) + gap / 2))
                let candidate = try Self(components: prefix, tieBreaker: tieBreaker)
                if lower.map({ !($0 < candidate) }) == true
                    || upper.map({ !(candidate < $0) }) == true {
                    throw OrderKeyError.invalidBounds
                }
                return candidate
            }

            prefix.append(lowerDigit)
        }

        throw OrderKeyError.depthLimitReached
    }

    private static func digit(
        at index: Int,
        in key: Self?,
        missing: UInt16
    ) -> UInt16 {
        guard let key, index < key.components.count else {
            return missing
        }
        return key.components[index]
    }
}
