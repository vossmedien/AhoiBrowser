import Foundation

public protocol SyncQuarantineStore: Sendable {
    func quarantine(recordID: UUID, reason: String) async
    func allQuarantined() async -> [UUID: String]
}

public actor InMemorySyncQuarantineStore: SyncQuarantineStore {
    private var values: [UUID: String] = [:]

    public init() {}

    public func quarantine(recordID: UUID, reason: String) async {
        values[recordID] = reason
    }

    public func allQuarantined() async -> [UUID: String] {
        values
    }
}

/// Restart-persistent quarantine metadata. Payloads stay solely in the sealed
/// record store; this file contains only UUIDs and bounded reason codes so a
/// corrupt remote record cannot leak private plaintext through diagnostics.
public actor FileSyncQuarantineStore: SyncQuarantineStore {
    private let fileURL: URL
    private var values: [UUID: String]

    public init(fileURL: URL) throws {
        self.fileURL = fileURL
        if FileManager.default.fileExists(atPath: fileURL.path) {
            values = try JSONDecoder().decode(
                [UUID: String].self,
                from: Data(contentsOf: fileURL)
            )
        } else {
            values = [:]
        }
    }

    public func quarantine(recordID: UUID, reason: String) async {
        let safeReason = Self.safeReason(reason)
        let previous = values.updateValue(safeReason, forKey: recordID)
        do {
            try persist()
        } catch {
            if let previous {
                values[recordID] = previous
            } else {
                values.removeValue(forKey: recordID)
            }
        }
    }

    public func allQuarantined() async -> [UUID: String] {
        values
    }

    private func persist() throws {
        let data = try JSONEncoder().encode(values)
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: fileURL, options: [.atomic])
    }

    private static func safeReason(_ reason: String) -> String {
        let allowed = reason.unicodeScalars.filter {
            CharacterSet.alphanumerics.contains($0) || $0 == "_" || $0 == "-"
        }
        return String(String.UnicodeScalarView(allowed)).prefix(80).description
    }
}
