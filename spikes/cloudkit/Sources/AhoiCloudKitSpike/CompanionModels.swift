import Foundation

/// The platform kind is descriptive metadata only. It never grants a device
/// permission to execute a remote command; command approval remains a
/// separate, explicit policy decision.
public enum DeviceKind: String, Codable, CaseIterable, Sendable {
    case mac
    case iPhone
    case iPad
}

/// Version metadata is shared by every local-first entity. The HLC and
/// originating device are the stable conflict inputs; `schemaVersion` is
/// independent so migrations can be forward-tested without changing order.
public struct SyncVersion: Codable, Hashable, Sendable, Comparable {
    public let schemaVersion: UInt32
    public let modifiedAt: HybridLogicalClock
    public let modifiedBy: DeviceID
    public var fieldVersions: [String: HybridLogicalClock]

    public init(
        schemaVersion: UInt32 = 2,
        modifiedAt: HybridLogicalClock,
        modifiedBy: DeviceID,
        fieldVersions: [String: HybridLogicalClock] = [:]
    ) {
        self.schemaVersion = schemaVersion
        self.modifiedAt = modifiedAt
        self.modifiedBy = modifiedBy
        self.fieldVersions = fieldVersions
    }

    public func normalized(for fields: Set<String>) -> Self {
        var result = self
        for field in fields where result.fieldVersions[field] == nil {
            result.fieldVersions[field] = modifiedAt
        }
        return result
    }

    public static func < (lhs: Self, rhs: Self) -> Bool {
        if lhs.modifiedAt != rhs.modifiedAt {
            return lhs.modifiedAt < rhs.modifiedAt
        }
        if lhs.modifiedBy != rhs.modifiedBy {
            return lhs.modifiedBy < rhs.modifiedBy
        }
        return lhs.schemaVersion < rhs.schemaVersion
    }

    public static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.schemaVersion == rhs.schemaVersion
            && lhs.modifiedAt == rhs.modifiedAt
            && lhs.modifiedBy == rhs.modifiedBy
            && lhs.fieldVersions == rhs.fieldVersions
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(schemaVersion)
        hasher.combine(modifiedAt)
        hasher.combine(modifiedBy)
        for key in fieldVersions.keys.sorted() {
            hasher.combine(key)
            hasher.combine(fieldVersions[key])
        }
    }

    private enum CodingKeys: String, CodingKey {
        case schemaVersion, modifiedAt, modifiedBy, fieldVersions
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        schemaVersion = try container.decode(UInt32.self, forKey: .schemaVersion)
        modifiedAt = try container.decode(HybridLogicalClock.self, forKey: .modifiedAt)
        modifiedBy = try container.decode(DeviceID.self, forKey: .modifiedBy)
        fieldVersions = try container.decodeIfPresent(
            [String: HybridLogicalClock].self,
            forKey: .fieldVersions
        ) ?? [:]
    }
}

public struct Device: Codable, Hashable, Sendable, Identifiable {
    public let deviceID: DeviceID
    public var name: String
    public let kind: DeviceKind
    public let createdAt: HybridLogicalClock
    public var lastSeenAt: HybridLogicalClock
    public var isOnline: Bool
    public var isRevoked: Bool
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        deviceID: DeviceID,
        name: String,
        kind: DeviceKind,
        createdAt: HybridLogicalClock? = nil,
        lastSeenAt: HybridLogicalClock,
        isOnline: Bool = false,
        isRevoked: Bool = false,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) {
        self.deviceID = deviceID
        self.name = name
        self.kind = kind
        self.createdAt = createdAt ?? lastSeenAt
        self.lastSeenAt = lastSeenAt
        self.isOnline = isOnline
        self.isRevoked = isRevoked
        self.version = version
        self.tombstone = tombstone
    }

    public var deviceName: String {
        get { name }
        set { name = newValue }
    }

    public var deviceKind: DeviceKind { kind }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.deviceID = try container.decode(DeviceID.self, forKey: .deviceID)
        self.name = try container.decodeIfPresent(String.self, forKey: .deviceName)
            ?? container.decode(String.self, forKey: .name)
        self.kind = try container.decodeIfPresent(DeviceKind.self, forKey: .deviceKind)
            ?? container.decode(DeviceKind.self, forKey: .kind)
        self.lastSeenAt = try container.decode(HybridLogicalClock.self, forKey: .lastSeenAt)
        self.createdAt = try container.decodeIfPresent(
            HybridLogicalClock.self,
            forKey: .createdAt
        ) ?? lastSeenAt
        self.isOnline = try container.decode(Bool.self, forKey: .isOnline)
        self.isRevoked = try container.decode(Bool.self, forKey: .isRevoked)
        self.version = try container.decode(SyncVersion.self, forKey: .version)
        self.tombstone = try container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(deviceID, forKey: .deviceID)
        try container.encode(deviceName, forKey: .deviceName)
        try container.encode(deviceKind, forKey: .deviceKind)
        try container.encode(createdAt, forKey: .createdAt)
        try container.encode(lastSeenAt, forKey: .lastSeenAt)
        try container.encode(isOnline, forKey: .isOnline)
        try container.encode(isRevoked, forKey: .isRevoked)
        try container.encode(version, forKey: .version)
        try container.encodeIfPresent(tombstone, forKey: .tombstone)
    }

    private enum CodingKeys: String, CodingKey {
        case deviceID, deviceName, deviceKind, createdAt, lastSeenAt, isOnline, isRevoked
        case version, tombstone, name, kind
    }

    public var id: DeviceID { deviceID }
    public var identity: DeviceID { deviceID }
    public var isDeleted: Bool { tombstone != nil }
}

public struct Workspace: Codable, Hashable, Sendable, Identifiable {
    public let workspaceID: WorkspaceID
    public var name: String
    public var icon: String
    public var accent: String?
    public var sortKey: String
    public let createdAt: HybridLogicalClock
    public var modifiedAt: HybridLogicalClock
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        workspaceID: WorkspaceID,
        name: String,
        icon: String = "",
        accent: String? = nil,
        sortKey: String? = nil,
        createdAt: HybridLogicalClock? = nil,
        modifiedAt: HybridLogicalClock? = nil,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) {
        self.workspaceID = workspaceID
        self.name = name
        self.icon = icon
        self.accent = accent
        self.sortKey = sortKey ?? workspaceID.rawValue.uuidString.lowercased()
        self.createdAt = createdAt ?? version.modifiedAt
        self.modifiedAt = modifiedAt ?? version.modifiedAt
        self.version = version
        self.tombstone = tombstone
    }

    public var workspaceName: String {
        get { name }
        set { name = newValue }
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.workspaceID = try container.decode(WorkspaceID.self, forKey: .workspaceID)
        self.name = try container.decodeIfPresent(String.self, forKey: .workspaceName)
            ?? container.decode(String.self, forKey: .name)
        self.icon = try container.decodeIfPresent(String.self, forKey: .icon) ?? ""
        self.accent = try container.decodeIfPresent(String.self, forKey: .accent)
        self.sortKey = try container.decodeIfPresent(String.self, forKey: .sortKey)
            ?? workspaceID.rawValue.uuidString.lowercased()
        self.version = try container.decode(SyncVersion.self, forKey: .version)
        self.createdAt = try container.decodeIfPresent(
            HybridLogicalClock.self,
            forKey: .createdAt
        ) ?? version.modifiedAt
        self.modifiedAt = try container.decodeIfPresent(
            HybridLogicalClock.self,
            forKey: .modifiedAt
        ) ?? version.modifiedAt
        self.tombstone = try container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(workspaceID, forKey: .workspaceID)
        try container.encode(workspaceName, forKey: .workspaceName)
        try container.encode(icon, forKey: .icon)
        try container.encodeIfPresent(accent, forKey: .accent)
        try container.encode(sortKey, forKey: .sortKey)
        try container.encode(createdAt, forKey: .createdAt)
        try container.encode(modifiedAt, forKey: .modifiedAt)
        try container.encode(version, forKey: .version)
        try container.encodeIfPresent(tombstone, forKey: .tombstone)
    }

    private enum CodingKeys: String, CodingKey {
        case workspaceID, workspaceName, icon, accent, sortKey, createdAt, modifiedAt
        case version, tombstone
        case name
    }

    public var id: WorkspaceID { workspaceID }
    public var identity: WorkspaceID { workspaceID }
    public var isDeleted: Bool { tombstone != nil }
}

public enum TreeNodeKind: String, Codable, CaseIterable, Sendable {
    case folder
    case savedPage
}

public struct TreeNode: Codable, Hashable, Sendable, Identifiable {
    public let treeNodeID: TreeNodeID
    public var workspaceID: WorkspaceID
    public var parentID: TreeNodeID?
    public var kind: TreeNodeKind
    public var title: String
    public var url: String?
    public var icon: String
    public var accent: String?
    public var orderKey: OrderKey
    public var wireSortKey: String?
    public var isTemporary: Bool
    public let createdAt: HybridLogicalClock
    public var modifiedAt: HybridLogicalClock
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        treeNodeID: TreeNodeID,
        workspaceID: WorkspaceID,
        parentID: TreeNodeID? = nil,
        kind: TreeNodeKind,
        title: String,
        url: String? = nil,
        icon: String = "",
        accent: String? = nil,
        orderKey: OrderKey,
        wireSortKey: String? = nil,
        isTemporary: Bool = false,
        createdAt: HybridLogicalClock? = nil,
        modifiedAt: HybridLogicalClock? = nil,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) throws {
        if kind == .savedPage && url == nil && !isTemporary {
            throw CompanionModelError.savedPageRequiresURL
        }
        if kind == .folder && isTemporary {
            throw CompanionModelError.folderCannotBeTemporary
        }
        if kind == .folder && url != nil {
            throw CompanionModelError.folderCannotHaveURL
        }
        self.treeNodeID = treeNodeID
        self.workspaceID = workspaceID
        self.parentID = parentID
        self.kind = kind
        self.title = title
        self.url = url
        self.icon = icon
        self.accent = accent
        self.orderKey = orderKey
        self.wireSortKey = wireSortKey
        self.isTemporary = isTemporary
        self.createdAt = createdAt ?? version.modifiedAt
        self.modifiedAt = modifiedAt ?? version.modifiedAt
        self.version = version
        self.tombstone = tombstone
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let version = try container.decode(SyncVersion.self, forKey: .version)
        try self.init(
            treeNodeID: container.decode(TreeNodeID.self, forKey: .treeNodeID),
            workspaceID: container.decode(WorkspaceID.self, forKey: .workspaceID),
            parentID: container.decodeIfPresent(TreeNodeID.self, forKey: .parentID),
            kind: container.decode(TreeNodeKind.self, forKey: .kind),
            title: container.decode(String.self, forKey: .title),
            url: container.decodeIfPresent(String.self, forKey: .url),
            icon: container.decodeIfPresent(String.self, forKey: .icon) ?? "",
            accent: container.decodeIfPresent(String.self, forKey: .accent),
            orderKey: container.decode(OrderKey.self, forKey: .orderKey),
            wireSortKey: container.decodeIfPresent(String.self, forKey: .wireSortKey),
            isTemporary: container.decodeIfPresent(Bool.self, forKey: .isTemporary) ?? false,
            createdAt: container.decodeIfPresent(
                HybridLogicalClock.self,
                forKey: .createdAt
            ) ?? version.modifiedAt,
            modifiedAt: container.decodeIfPresent(
                HybridLogicalClock.self,
                forKey: .modifiedAt
            ) ?? version.modifiedAt,
            version: version,
            tombstone: container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }

    private enum CodingKeys: String, CodingKey {
        case treeNodeID, workspaceID, parentID, kind, title, url, icon, accent, orderKey
        case wireSortKey
        case isTemporary, createdAt, modifiedAt, version, tombstone
    }

    public var id: TreeNodeID { treeNodeID }
    public var identity: TreeNodeID { treeNodeID }
    public var isDeleted: Bool { tombstone != nil }
    public var syncSortKey: String { wireSortKey ?? orderKey.canonicalSortKey }
}

public struct DeviceSession: Codable, Hashable, Sendable, Identifiable {
    public let sessionID: DeviceSessionID
    public let deviceID: DeviceID
    public var deviceName: String
    public let deviceKind: DeviceKind
    public var workspaceID: WorkspaceID?
    public let startedAt: HybridLogicalClock
    public var lastActiveAt: HybridLogicalClock
    public var isOnline: Bool
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        sessionID: DeviceSessionID,
        deviceID: DeviceID,
        deviceName: String,
        deviceKind: DeviceKind,
        workspaceID: WorkspaceID? = nil,
        startedAt: HybridLogicalClock? = nil,
        lastActiveAt: HybridLogicalClock,
        isOnline: Bool = false,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) {
        self.sessionID = sessionID
        self.deviceID = deviceID
        self.deviceName = deviceName
        self.deviceKind = deviceKind
        self.workspaceID = workspaceID
        self.startedAt = startedAt ?? lastActiveAt
        self.lastActiveAt = lastActiveAt
        self.isOnline = isOnline
        self.version = version
        self.tombstone = tombstone
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let lastActiveAt = try container.decode(
            HybridLogicalClock.self,
            forKey: .lastActiveAt
        )
        self.init(
            sessionID: try container.decode(DeviceSessionID.self, forKey: .sessionID),
            deviceID: try container.decode(DeviceID.self, forKey: .deviceID),
            deviceName: try container.decode(String.self, forKey: .deviceName),
            deviceKind: try container.decode(DeviceKind.self, forKey: .deviceKind),
            workspaceID: try container.decodeIfPresent(WorkspaceID.self, forKey: .workspaceID),
            startedAt: try container.decodeIfPresent(
                HybridLogicalClock.self,
                forKey: .startedAt
            ) ?? lastActiveAt,
            lastActiveAt: lastActiveAt,
            isOnline: try container.decode(Bool.self, forKey: .isOnline),
            version: try container.decode(SyncVersion.self, forKey: .version),
            tombstone: try container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }

    private enum CodingKeys: String, CodingKey {
        case sessionID, deviceID, deviceName, deviceKind, workspaceID
        case startedAt, lastActiveAt, isOnline, version, tombstone
    }

    public var id: DeviceSessionID { sessionID }
    public var identity: DeviceSessionID { sessionID }
    public var isDeleted: Bool { tombstone != nil }
}

/// A normal, open tab published by a device. Incognito is rejected at the
/// model boundary rather than being filtered later by a view or CloudKit
/// adapter, so it cannot leak into local search or an outbox by accident.
public struct RemoteTab: Codable, Hashable, Sendable, Identifiable {
    public static let maximumTitleUTF8Bytes = 1_024
    public static let maximumURLUTF8Bytes = 16 * 1_024
    public static let maximumDeviceNameUTF8Bytes = 256
    public static let maximumWorkspaceNameUTF8Bytes = 256

    public let tabID: TabID
    public let deviceID: DeviceID
    public let deviceKind: DeviceKind
    public var deviceName: String
    public let sessionID: DeviceSessionID
    public var workspaceID: WorkspaceID?
    public var treeNodeID: TreeNodeID?
    public var workspaceName: String?
    public var title: String
    public var url: String
    public let openedAt: HybridLogicalClock
    public var lastActiveAt: HybridLogicalClock
    public let context: BrowserContextKind
    public var isOpen: Bool
    public var pinned: Bool
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        tabID: TabID,
        deviceID: DeviceID,
        deviceKind: DeviceKind,
        deviceName: String,
        sessionID: DeviceSessionID,
        workspaceID: WorkspaceID? = nil,
        treeNodeID: TreeNodeID? = nil,
        workspaceName: String? = nil,
        title: String,
        url: String,
        openedAt: HybridLogicalClock? = nil,
        lastActiveAt: HybridLogicalClock,
        context: BrowserContextKind = .normal,
        isOpen: Bool = true,
        pinned: Bool = false,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) throws {
        guard context == .normal else {
            throw CompanionModelError.incognitoNotSyncable
        }
        guard treeNodeID?.rawValue != tabID.rawValue else {
            throw CompanionModelError.sharedIdentityCollision
        }
        guard title.utf8.count <= Self.maximumTitleUTF8Bytes,
              url.utf8.count <= Self.maximumURLUTF8Bytes,
              deviceName.utf8.count <= Self.maximumDeviceNameUTF8Bytes,
              (workspaceName?.utf8.count ?? 0) <= Self.maximumWorkspaceNameUTF8Bytes else {
            throw CompanionModelError.metadataTooLarge
        }
        guard let components = URLComponents(string: url),
              let scheme = components.scheme?.lowercased(),
              (scheme == "http" || scheme == "https"),
              components.host?.isEmpty == false,
              components.user == nil,
              components.password == nil else {
            throw CompanionModelError.remoteTabURLNotAllowed
        }
        self.tabID = tabID
        self.deviceID = deviceID
        self.deviceKind = deviceKind
        self.deviceName = deviceName
        self.sessionID = sessionID
        self.workspaceID = workspaceID
        self.treeNodeID = treeNodeID
        self.workspaceName = workspaceName
        self.title = title
        self.url = url
        self.openedAt = openedAt ?? lastActiveAt
        self.lastActiveAt = lastActiveAt
        self.context = context
        self.isOpen = isOpen
        self.pinned = pinned
        self.version = version
        self.tombstone = tombstone
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        try self.init(
            tabID: container.decode(TabID.self, forKey: .tabID),
            deviceID: container.decode(DeviceID.self, forKey: .deviceID),
            deviceKind: container.decode(DeviceKind.self, forKey: .deviceKind),
            deviceName: container.decode(String.self, forKey: .deviceName),
            sessionID: container.decode(DeviceSessionID.self, forKey: .sessionID),
            workspaceID: container.decodeIfPresent(WorkspaceID.self, forKey: .workspaceID),
            treeNodeID: container.decodeIfPresent(TreeNodeID.self, forKey: .treeNodeID),
            workspaceName: container.decodeIfPresent(String.self, forKey: .workspaceName),
            title: container.decode(String.self, forKey: .title),
            url: container.decode(String.self, forKey: .url),
            openedAt: container.decodeIfPresent(
                HybridLogicalClock.self,
                forKey: .openedAt
            ),
            lastActiveAt: container.decode(HybridLogicalClock.self, forKey: .lastActiveAt),
            context: container.decode(BrowserContextKind.self, forKey: .context),
            isOpen: container.decode(Bool.self, forKey: .isOpen),
            pinned: container.decodeIfPresent(Bool.self, forKey: .pinned) ?? false,
            version: container.decode(SyncVersion.self, forKey: .version),
            tombstone: container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }

    private enum CodingKeys: String, CodingKey {
        case tabID, deviceID, deviceKind, deviceName, sessionID
        case workspaceID, workspaceName, title, url, openedAt, lastActiveAt, context
        case treeNodeID
        case isOpen, pinned, version, tombstone
    }

    public var id: TabID { tabID }
    public var identity: TabID { tabID }
    public var isDeleted: Bool { tombstone != nil || !isOpen }
}

public struct HistoryVisit: Codable, Hashable, Sendable, Identifiable {
    public static let maximumTitleUTF8Bytes = 1_024
    public static let maximumURLUTF8Bytes = 16 * 1_024
    public static let maximumTransitionUTF8Bytes = 128

    public let visitID: HistoryVisitID
    public let deviceID: DeviceID
    public var title: String
    public var url: String
    public let visitedAt: HybridLogicalClock
    public var transition: String
    public let visitCount: Int64
    public var version: SyncVersion
    public var tombstone: Tombstone?

    public init(
        visitID: HistoryVisitID,
        deviceID: DeviceID,
        title: String,
        url: String,
        visitedAt: HybridLogicalClock,
        transition: String,
        visitCount: Int64 = 1,
        version: SyncVersion,
        tombstone: Tombstone? = nil
    ) throws {
        guard title.utf8.count <= Self.maximumTitleUTF8Bytes,
              url.utf8.count <= Self.maximumURLUTF8Bytes,
              transition.utf8.count <= Self.maximumTransitionUTF8Bytes else {
            throw CompanionModelError.metadataTooLarge
        }
        guard let components = URLComponents(string: url),
              let scheme = components.scheme?.lowercased(),
              (scheme == "http" || scheme == "https"),
              components.host?.isEmpty == false,
              components.user == nil,
              components.password == nil else {
            throw CompanionModelError.historyURLNotAllowed
        }
        self.visitID = visitID
        self.deviceID = deviceID
        self.title = title
        self.url = url
        self.visitedAt = visitedAt
        self.transition = transition
        self.visitCount = visitCount
        self.version = version
        self.tombstone = tombstone
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        try self.init(
            visitID: container.decode(HistoryVisitID.self, forKey: .visitID),
            deviceID: container.decode(DeviceID.self, forKey: .deviceID),
            title: container.decode(String.self, forKey: .title),
            url: container.decode(String.self, forKey: .url),
            visitedAt: container.decode(HybridLogicalClock.self, forKey: .visitedAt),
            transition: container.decode(String.self, forKey: .transition),
            visitCount: container.decodeIfPresent(Int64.self, forKey: .visitCount) ?? 1,
            version: container.decode(SyncVersion.self, forKey: .version),
            tombstone: container.decodeIfPresent(Tombstone.self, forKey: .tombstone)
        )
    }

    private enum CodingKeys: String, CodingKey {
        case visitID, deviceID, title, url, visitedAt, transition, visitCount, version, tombstone
    }

    public var id: HistoryVisitID { visitID }
    public var identity: HistoryVisitID { visitID }
    public var isDeleted: Bool { tombstone != nil }
}

public enum CompanionModelError: Error, Equatable, Sendable {
    case folderCannotBeTemporary
    case sharedIdentityCollision
    case incognitoNotSyncable
    case metadataTooLarge
    case remoteTabURLNotAllowed
    case historyURLNotAllowed
    case savedPageRequiresURL
    case folderCannotHaveURL
}

public extension SyncVersion {
    /// Converts the model's version metadata into the common transport shape.
    /// The payload itself remains opaque and is sealed by the platform crypto
    /// provider before it reaches `CKRecord.encryptedValues`.
    var modifiedAtHLC: HybridLogicalClock { modifiedAt }
}
