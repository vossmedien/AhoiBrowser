import Foundation

public enum ConflictCanonicalizationError: Error, Equatable {
    case duplicateCanonicalSetElement
}

/// Opt-in contract for values whose conflict ordering must converge across
/// processes. Codable alone is intentionally insufficient because unkeyed
/// containers such as Set do not promise a stable encoding order.
public protocol ConflictCanonicalizable: Codable, Hashable, Sendable {
    func stableConflictCanonicalBytes() throws -> Data
}

private enum ConflictCanonicalFrame {
    static func scalar(tag: UInt8, payload: Data) -> Data {
        var result = Data([tag])
        result.append(length(payload.count))
        result.append(payload)
        return result
    }

    static func aggregate(tag: UInt8, elements: [Data]) -> Data {
        var result = Data([tag])
        result.append(length(elements.count))
        for element in elements {
            result.append(length(element.count))
            result.append(element)
        }
        return result
    }

    static func length(_ value: Int) -> Data {
        unsigned(UInt64(value))
    }

    static func unsigned(_ value: UInt64) -> Data {
        var bigEndian = value.bigEndian
        return Swift.withUnsafeBytes(of: &bigEndian) { Data($0) }
    }
}

extension String: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        let normalized = precomposedStringWithCanonicalMapping
        return ConflictCanonicalFrame.scalar(
            tag: 0x01,
            payload: Data(normalized.utf8)
        )
    }
}

extension Bool: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        ConflictCanonicalFrame.scalar(
            tag: 0x02,
            payload: Data([self ? 1 : 0])
        )
    }
}

extension Int: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try Int64(self).stableConflictCanonicalBytes()
    }
}

extension Int64: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        ConflictCanonicalFrame.scalar(
            tag: 0x03,
            payload: ConflictCanonicalFrame.unsigned(UInt64(bitPattern: self))
        )
    }
}

extension UInt: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try UInt64(self).stableConflictCanonicalBytes()
    }
}

extension UInt32: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try UInt64(self).stableConflictCanonicalBytes()
    }
}

extension UInt64: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        ConflictCanonicalFrame.scalar(
            tag: 0x04,
            payload: ConflictCanonicalFrame.unsigned(self)
        )
    }
}

extension UUID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        ConflictCanonicalFrame.scalar(
            tag: 0x05,
            payload: Data(uuidString.lowercased().utf8)
        )
    }
}

extension Data: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        ConflictCanonicalFrame.scalar(tag: 0x06, payload: self)
    }
}

extension Array: ConflictCanonicalizable where Element: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try ConflictCanonicalFrame.aggregate(
            tag: 0x10,
            elements: map { try $0.stableConflictCanonicalBytes() }
        )
    }
}

extension Set: ConflictCanonicalizable where Element: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        var elements = try map { try $0.stableConflictCanonicalBytes() }
        elements.sort { $0.lexicographicallyPrecedes($1) }
        if elements.count > 1 {
            for index in 1..<elements.count where elements[index - 1] == elements[index] {
                throw ConflictCanonicalizationError.duplicateCanonicalSetElement
            }
        }
        return ConflictCanonicalFrame.aggregate(tag: 0x11, elements: elements)
    }
}

extension Optional: ConflictCanonicalizable where Wrapped: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        switch self {
        case .none:
            return ConflictCanonicalFrame.aggregate(tag: 0x12, elements: [])
        case let .some(value):
            return try ConflictCanonicalFrame.aggregate(
                tag: 0x13,
                elements: [value.stableConflictCanonicalBytes()]
            )
        }
    }
}

extension DeviceID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try rawValue.stableConflictCanonicalBytes()
    }
}

extension WorkspaceID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try rawValue.stableConflictCanonicalBytes()
    }
}

extension TreeNodeID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try rawValue.stableConflictCanonicalBytes()
    }
}

extension TabID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try rawValue.stableConflictCanonicalBytes()
    }
}

extension DeviceSessionID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try rawValue.stableConflictCanonicalBytes()
    }
}

extension HistoryVisitID: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try rawValue.stableConflictCanonicalBytes()
    }
}

extension EncryptedValue: ConflictCanonicalizable {
    public func stableConflictCanonicalBytes() throws -> Data {
        try ConflictCanonicalFrame.aggregate(
            tag: 0x20,
            elements: [
                algorithm.rawValue.stableConflictCanonicalBytes(),
                keyVersion.stableConflictCanonicalBytes(),
                nonce.stableConflictCanonicalBytes(),
                ciphertextAndTag.stableConflictCanonicalBytes(),
            ]
        )
    }
}
