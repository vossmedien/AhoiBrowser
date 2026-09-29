import Foundation

/// ADR 0011 step 4: the CloudKit namespace one sync session uses. Mirrors the
/// desktop helper `overlay/chromium/src/ahoi/browser/sync/sync_namespace.{h,cc}`
/// byte for byte: the main namespace keeps the bundle values unchanged; a fully
/// separated Workspace syncs into `<main zone>-ws-<uuid lowercase>` with its own
/// subscription and end-to-end key account. A separated namespace can never
/// resolve to the main one.
public struct SyncNamespace: Hashable, Sendable {
    /// Default zone of the main namespace when Info.plist does not override it.
    public static let mainZoneName = "AhoiBrowserSyncV3"
    /// A separated Workspace's zone is `<main zone>-ws-<uuid lowercase>`.
    public static let workspaceZoneInfix = "-ws-"
    /// Its end-to-end key lives under `<main key account>.ws-<uuid lowercase>`.
    public static let workspaceKeyAccountInfix = ".ws-"
    /// CloudKit zone names: ASCII letters, digits, '-' and '_', at most 255
    /// characters, not starting with '_' (reserved, e.g. `_defaultZone`).
    public static let maxCloudKitZoneNameLength = 255

    /// Nil for the main namespace.
    public let workspaceID: UUID?

    private init(workspaceID: UUID?) {
        self.workspaceID = workspaceID
    }

    public static let main = SyncNamespace(workspaceID: nil)

    public var isMain: Bool { workspaceID == nil }

    /// Nil for an unusable Workspace id: the caller must then not sync. The
    /// all-zero UUID is rejected (Ahoi never mints it; the Companion treats it
    /// as absent everywhere else).
    public static func separatedWorkspace(_ workspaceID: UUID) -> SyncNamespace? {
        guard workspaceID != nilUUID else { return nil }
        return SyncNamespace(workspaceID: workspaceID)
    }

    /// Accepts only the canonical lowercase form the desktop writes
    /// (`base::Uuid::AsLowercaseString`), so one Workspace has exactly one
    /// zone name and no alias.
    public static func separatedWorkspace(lowercaseUUID string: String) -> SyncNamespace? {
        guard string.count == 36,
              let uuid = UUID(uuidString: string),
              uuid.uuidString.lowercased() == string else {
            return nil
        }
        return separatedWorkspace(uuid)
    }

    private static let nilUUID = UUID(uuidString: "00000000-0000-0000-0000-000000000000")!

    public var workspaceIDLowercase: String? {
        workspaceID?.uuidString.lowercased()
    }
}

/// The per-namespace CloudKit and Keychain identifiers. The base carries the
/// bundle-configured main values.
public struct SyncNamespaceIdentifiers: Hashable, Sendable {
    public var zoneName: String
    /// Empty means CKSyncEngine's default subscription id.
    public var subscriptionIdentifier: String
    public var keychainAccount: String

    public init(zoneName: String, subscriptionIdentifier: String, keychainAccount: String) {
        self.zoneName = zoneName
        self.subscriptionIdentifier = subscriptionIdentifier
        self.keychainAccount = keychainAccount
    }

    /// The value handed to `CKSyncEngine.Configuration.subscriptionID`.
    public var subscriptionID: String? {
        subscriptionIdentifier.isEmpty ? nil : subscriptionIdentifier
    }
}

extension SyncNamespace {
    public static func isValidCloudKitZoneName(_ zoneName: String) -> Bool {
        let bytes = Array(zoneName.utf8)
        guard !bytes.isEmpty, bytes.count <= maxCloudKitZoneNameLength,
              bytes.first != UInt8(ascii: "_") else {
            return false
        }
        return bytes.allSatisfy { byte in
            (byte >= UInt8(ascii: "a") && byte <= UInt8(ascii: "z")) ||
                (byte >= UInt8(ascii: "A") && byte <= UInt8(ascii: "Z")) ||
                (byte >= UInt8(ascii: "0") && byte <= UInt8(ascii: "9")) ||
                byte == UInt8(ascii: "-") || byte == UInt8(ascii: "_")
        }
    }

    /// Main namespace: returns `base` unchanged. Separated namespace: zone
    /// `<base zone>-ws-<uuid>`, subscription `<base subscription>-ws-<uuid>`
    /// (or the zone name when the base is empty, so two sync engines on the
    /// same database never share CKSyncEngine's default subscription id), key
    /// account `<base account>.ws-<uuid>`. Nil when a base value it derives
    /// from is empty or already looks separated, or the derived zone name is
    /// invalid; the caller must then create no provider.
    public func resolve(_ base: SyncNamespaceIdentifiers) -> SyncNamespaceIdentifiers? {
        guard let id = workspaceIDLowercase else { return base }
        // A base that already looks like a Workspace zone would make the main
        // zone indistinguishable from a separated one for the prefix scan.
        guard !base.zoneName.isEmpty, !base.keychainAccount.isEmpty,
              !base.zoneName.contains(Self.workspaceZoneInfix),
              !base.keychainAccount.contains(Self.workspaceKeyAccountInfix) else {
            return nil
        }
        let zoneName = base.zoneName + Self.workspaceZoneInfix + id
        let result = SyncNamespaceIdentifiers(
            zoneName: zoneName,
            subscriptionIdentifier: base.subscriptionIdentifier.isEmpty
                ? zoneName
                : base.subscriptionIdentifier + Self.workspaceZoneInfix + id,
            keychainAccount: base.keychainAccount + Self.workspaceKeyAccountInfix + id
        )
        guard Self.isValidCloudKitZoneName(result.zoneName),
              result.zoneName != base.zoneName,
              result.subscriptionIdentifier != base.subscriptionIdentifier,
              result.keychainAccount != base.keychainAccount else {
            return nil
        }
        return result
    }

    /// Zone discovery: the separated namespace a private-database zone belongs
    /// to, or nil for the main zone, foreign zones and anything malformed. The
    /// zone must round-trip through `resolve` exactly.
    public static func separatedWorkspace(
        fromZoneName zoneName: String,
        base: SyncNamespaceIdentifiers
    ) -> SyncNamespace? {
        let prefix = base.zoneName + workspaceZoneInfix
        guard !base.zoneName.isEmpty, zoneName.hasPrefix(prefix),
              let namespace = separatedWorkspace(
                  lowercaseUUID: String(zoneName.dropFirst(prefix.count))
              ),
              namespace.resolve(base)?.zoneName == zoneName else {
            return nil
        }
        return namespace
    }
}
