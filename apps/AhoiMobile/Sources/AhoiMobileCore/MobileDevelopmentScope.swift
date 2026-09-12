import Foundation

/// Binds acceptance persistence to the exact Development zone/key tuple. This
/// never resets, imports or deletes the ordinary product state or another run.
public struct MobileDevelopmentScope: Equatable, Sendable {
    public let id: UUID
    public var name: String { id.uuidString.lowercased() }
    public var defaultsSuite: String { "app.ahoibrowser.AhoiBrowser.acceptance.\(name)" }

    public static func resolve(info: [String: Any]) throws -> Self? {
        let zone = info["AHOI_CLOUDKIT_ZONE_NAME"] as? String ?? ""
        let account = info["AHOI_SYNC_KEYCHAIN_ACCOUNT"] as? String ?? ""
        let subscription = info["AHOI_CLOUDKIT_SUBSCRIPTION_ID"] as? String ?? ""
        let prefix = "AhoiSyncAcceptance-"
        let requested = zone.hasPrefix(prefix)
            || account.contains(".acceptance-")
            || subscription.hasPrefix("AhoiSyncAcceptanceSubscription-")
        guard requested else { return nil }
        let raw = String(zone.dropFirst(prefix.count))
        guard zone.hasPrefix(prefix), let id = UUID(uuidString: raw),
              id.uuidString.lowercased() == raw,
              raw.split(separator: "-").dropFirst(2).first?.first == "4",
              ["8", "9", "a", "b"].contains(String(raw.split(separator: "-")[3].prefix(1))),
              info["AhoiBuildMode"] as? String == "CloudKitDevelopment",
              info["AHOI_CLOUDKIT_CONTAINER_ID"] as? String == "iCloud.app.ahoibrowser.AhoiBrowser",
              subscription == "AhoiSyncAcceptanceSubscription-\(raw)",
              account == "payload-key.acceptance-\(raw)",
              info["AHOI_SYNC_KEYCHAIN_SERVICE"] as? String == "app.ahoibrowser.sync.payload-key",
              info["AHOI_SYNC_KEYCHAIN_ACCESS_GROUP"] as? String == "248AJ5BN47.app.ahoibrowser.sync",
              info["AHOI_SYNC_KEY_VERSION"] as? String == "1" else {
            throw ScopeError.invalidConfiguration
        }
        return Self(id: id)
    }

    public func supportDirectory(under applicationSupport: URL) -> URL {
        applicationSupport.appendingPathComponent("AhoiMobile", isDirectory: true)
            .appendingPathComponent("DevelopmentAcceptance", isDirectory: true)
            .appendingPathComponent(name, isDirectory: true)
            .appendingPathComponent("SyncFormat3", isDirectory: true)
    }

    public static func validateAncestors(of support: URL, under applicationSupport: URL) throws {
        var directory = support
        guard directory.path.hasPrefix(applicationSupport.path + "/") else {
            throw ScopeError.invalidConfiguration
        }
        while directory.path.hasPrefix(applicationSupport.path + "/") {
            if FileManager.default.fileExists(atPath: directory.path),
               try directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true {
                throw CocoaError(.fileWriteInvalidFileName)
            }
            directory.deleteLastPathComponent()
        }
    }

    public enum ScopeError: Error, LocalizedError {
        case invalidConfiguration
        case defaultsUnavailable

        public var errorDescription: String? {
            "The isolated Development configuration is incomplete. Existing browser data was not opened."
        }
    }
}
