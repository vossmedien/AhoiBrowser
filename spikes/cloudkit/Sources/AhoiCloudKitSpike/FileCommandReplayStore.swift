import Foundation

/// Persistent at-most-once protection for app-side command validation. Both
/// Command-ID and source-scoped nonce survive process restarts. A persistence
/// error fails closed instead of accepting an unrecorded command.
public actor FileCommandReplayStore: CommandReplayChecking {
    private struct State: Codable {
        var commandExpirations: [String: UInt64] = [:]
        var nonceExpirations: [String: UInt64] = [:]
    }

    private let fileURL: URL
    private var state: State?

    public init(fileURL: URL) {
        self.fileURL = fileURL
    }

    public func consume(
        commandID: UUID,
        nonce: Data,
        sourceDeviceID: DeviceID,
        expiresAtMilliseconds: UInt64,
        nowMilliseconds: UInt64
    ) -> Bool {
        do {
            var value = try load()
            value.commandExpirations = value.commandExpirations.filter {
                $0.value > nowMilliseconds
            }
            value.nonceExpirations = value.nonceExpirations.filter {
                $0.value > nowMilliseconds
            }
            let commandKey = commandID.uuidString.lowercased()
            let nonceKey = sourceDeviceID.rawValue.uuidString.lowercased()
                + ":" + nonce.base64EncodedString()
            guard value.commandExpirations[commandKey] == nil,
                  value.nonceExpirations[nonceKey] == nil else {
                return false
            }
            value.commandExpirations[commandKey] = expiresAtMilliseconds
            value.nonceExpirations[nonceKey] = expiresAtMilliseconds
            let directory = fileURL.deletingLastPathComponent()
            try FileManager.default.createDirectory(
                at: directory,
                withIntermediateDirectories: true
            )
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.sortedKeys]
            try encoder.encode(value).write(to: fileURL, options: [.atomic])
            state = value
            return true
        } catch {
            return false
        }
    }

    private func load() throws -> State {
        if let state { return state }
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            let empty = State()
            state = empty
            return empty
        }
        let decoded = try JSONDecoder().decode(
            State.self,
            from: Data(contentsOf: fileURL)
        )
        state = decoded
        return decoded
    }
}
