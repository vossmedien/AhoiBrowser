import Foundation
import AhoiCloudKitSpike

public enum DesktopWirePayloadCodecError: Error, Equatable, Sendable {
    case missingDependency
    case malformedPayload
    case unsupportedDeviceType
    case invalidOrderKey
}

/// Canonical JSON payload shared with Chromium's `sync_serialization.cc`.
/// Integer timestamps remain decimal strings so 64-bit microseconds survive
/// JSON/Foundation without precision loss.
public struct DesktopWirePayloadCodec: Sendable {
    static let windowsToUnixMicroseconds: Int64 = 11_644_473_600_000_000
    private static let deviceFields: Set<String> = [
        "type", "display_name", "created_at", "last_seen", "retired", "tombstone",
    ]
    private static let workspaceFields: Set<String> = [
        "name", "icon", "sort_key", "accent_argb", "created_at", "modified_at",
        "tombstone",
    ]
    private static let treeNodeFields = SharedTabWireReadPolicy.treeNodeBaseFields
    private static let remoteTabFields = SharedTabWireReadPolicy.remoteTabBaseFields
    private static let sessionFields: Set<String> = [
        "device_id", "started_at", "liveness", "tombstone",
    ]
    private static let historyFields: Set<String> = [
        "device_id", "url", "title", "last_visit", "visit_count", "transition",
        "tombstone",
    ]
    private static let commandFields: Set<String> = ["request", "status", "tombstone"]

    public init() {}

    public func encode(_ device: Device) throws -> Data {
        var value = try common(
            id: device.id.rawValue,
            tombstone: device.isDeleted,
            version: device.version,
            fields: Self.deviceFields
        )
        let deviceType: Int
        switch device.kind {
        case .mac: deviceType = 0
        case .iPhone: deviceType = 1
        case .iPad: deviceType = 2
        case .other: deviceType = 3
        }
        value["device_type"] = deviceType
        value["display_name"] = device.name
        value["created_at"] = try timeString(device.createdAt)
        value["last_seen"] = try timeString(device.lastSeenAt)
        value["retired"] = device.isRevoked
        return try serialize(value)
    }

    public func encode(_ workspace: Workspace) throws -> Data {
        var value = try common(
            id: workspace.id.rawValue,
            tombstone: workspace.isDeleted,
            version: workspace.version,
            fields: Self.workspaceFields
        )
        value["name"] = workspace.name
        value["icon"] = workspace.icon
        value["sort_key"] = workspace.sortKey
        value["created_at"] = try timeString(workspace.createdAt)
        value["modified_at"] = try timeString(workspace.modifiedAt)
        if let accent = workspace.accent,
           let parsed = UInt32(accent.trimmingCharacters(in: CharacterSet(charactersIn: "#")), radix: 16) {
            value["accent_argb"] = Int(Int32(bitPattern: parsed))
        }
        return try serialize(value)
    }

    public func encode(_ node: TreeNode) throws -> Data {
        try SharedTabWireReadPolicy.validateTreeNodeWrite(node)
        var value = try common(
            id: node.id.rawValue,
            tombstone: node.isDeleted,
            version: node.version,
            fields: Self.treeNodeFields
        )
        value["workspace_id"] = uuid(node.workspaceID.rawValue)
        value["parent_id"] = node.parentID.map { uuid($0.rawValue) }
        value["node_kind"] = node.kind == .folder ? 0 : 1
        value["title"] = node.title
        value["icon"] = node.icon
        value["url"] = node.url ?? ""
        value["sort_key"] = node.syncSortKey
        value["created_at"] = try timeString(node.createdAt)
        value["modified_at"] = try timeString(node.modifiedAt)
        value["is_temporary"] = node.isTemporary
        if node.kind == .savedPage {
            guard let kind = node.targetKind else { throw SharedTabTargetError.invalidTarget }
            value["target_kind"] = kind.rawValue
            value["local_scheme"] = node.localScheme?.rawValue
        }
        if let accent = node.accent,
           let parsed = UInt32(
               accent.trimmingCharacters(in: CharacterSet(charactersIn: "#")),
               radix: 16
           ) {
            value["accent_argb"] = Int(Int32(bitPattern: parsed))
        }
        return try serialize(value)
    }

    public func encode(_ session: DeviceSession) throws -> Data {
        var value = try common(
            id: session.id.rawValue,
            tombstone: session.isDeleted,
            version: session.version,
            fields: Self.sessionFields
        )
        value["device_id"] = uuid(session.deviceID.rawValue)
        value["started_at"] = try timeString(session.startedAt)
        value["last_seen"] = try timeString(session.lastActiveAt)
        value["active"] = session.isOnline
        return try serialize(value)
    }

    public func encode(_ visit: HistoryVisit) throws -> Data {
        var value = try common(
            id: visit.id.rawValue,
            tombstone: visit.isDeleted,
            version: visit.version,
            fields: Self.historyFields
        )
        value["device_id"] = uuid(visit.deviceID.rawValue)
        value["url"] = visit.url
        value["title"] = visit.title
        value["last_visit"] = try timeString(visit.visitedAt)
        value["visit_count"] = String(visit.visitCount)
        value["transition"] = visit.transition
        return try serialize(value)
    }

    public func encode(_ tab: RemoteTab) throws -> Data {
        guard tab.context == .normal else { throw CompanionModelError.incognitoNotSyncable }
        try SharedTabWireReadPolicy.validateRemoteTabWrite(tab)
        guard let pageID = tab.treeNodeID, let kind = tab.targetKind else {
            throw SharedTabTargetError.missingPageLink
        }
        var value = try common(
            id: tab.id.rawValue,
            tombstone: tab.isDeleted,
            version: tab.version,
            fields: Self.remoteTabFields
        )
        value["device_id"] = uuid(tab.deviceID.rawValue)
        value["session_id"] = uuid(tab.sessionID.rawValue)
        value["workspace_id"] = tab.workspaceID.map { uuid($0.rawValue) }
        value["url"] = tab.url
        value["title"] = tab.title
        value["opened_at"] = try timeString(tab.openedAt)
        value["last_active"] = try timeString(tab.lastActiveAt)
        value["pinned"] = tab.pinned
        value["is_incognito"] = false
        value["tree_node_id"] = uuid(pageID.rawValue)
        value["target_kind"] = kind.rawValue
        value["local_scheme"] = tab.localScheme?.rawValue
        return try serialize(value)
    }

    public func encode(_ command: RemoteCommandState) throws -> Data {
        let payload = command.envelope.payload
        var value = try common(
            id: payload.commandID,
            tombstone: false,
            version: command.version,
            fields: Self.commandFields
        )
        value["source_device_id"] = uuid(payload.sourceDeviceID.rawValue)
        value["target_device_id"] = uuid(payload.targetDeviceID.rawValue)
        value["nonce"] = payload.nonce.base64EncodedString()
        value["issued_at"] = try timeString(milliseconds: payload.issuedAtMilliseconds)
        value["expires_at"] = try timeString(milliseconds: payload.expiresAtMilliseconds)
        value["signature"] = command.envelope.signature.base64EncodedString()
        value["status"] = command.status.rawValue
        value["result"] = command.resultCode
        switch payload.command {
        case let .open(request):
            value["command_kind"] = 0
            value["url"] = request.url
            value["workspace_id"] = request.workspaceID.map { uuid($0.rawValue) }
        case let .focus(reference):
            guard reference.context == .normal else {
                throw CompanionModelError.incognitoNotSyncable
            }
            value["command_kind"] = 1
            value["url"] = ""
            value["tab_id"] = uuid(reference.tabID.rawValue)
        case let .close(references):
            guard references.count == 1 else {
                throw RemoteCommandValidationError.massActionForbidden
            }
            guard references[0].context == .normal else {
                throw CompanionModelError.incognitoNotSyncable
            }
            value["command_kind"] = 2
            value["url"] = ""
            value["tab_id"] = uuid(references[0].tabID.rawValue)
        }
        return try serialize(value)
    }

    public func object(from plaintext: Data) throws -> [String: Any] {
        guard let result = try JSONSerialization.jsonObject(with: plaintext) as? [String: Any] else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    public func decodeDevice(_ record: SyncRecord, plaintext: Data) throws -> Device {
        try decodeDevice(record, value: object(from: plaintext))
    }

    private func decodeDevice(_ record: SyncRecord, value: [String: Any]) throws -> Device {
        let resultVersion = try version(value, requiredFields: Self.deviceFields)
        let type = try integer(value, "device_type")
        let kind: DeviceKind
        switch type {
        case 0: kind = .mac
        case 1: kind = .iPhone
        case 2: kind = .iPad
        case 3: kind = .other
        default: throw DesktopWirePayloadCodecError.unsupportedDeviceType
        }
        let deleted = try tombstone(record, value: value)
        let retired = try SharedTabWireReadPolicy.strictBoolean(value, key: "retired")
        try validatePortableEnvelope(record, dataClass: .device, identity: id(value),
                                     version: resultVersion, tombstone: deleted)
        return Device(
            deviceID: DeviceID(rawValue: try id(value)),
            name: try string(value, "display_name"),
            kind: kind,
            createdAt: try clock(value, timeKey: "created_at"),
            lastSeenAt: try clock(value, timeKey: "last_seen"),
            isOnline: !retired && deleted == nil,
            isRevoked: retired,
            version: resultVersion,
            tombstone: deleted
        )
    }

    public func decodeWorkspace(_ record: SyncRecord, plaintext: Data) throws -> Workspace {
        try decodeWorkspace(record, value: object(from: plaintext))
    }

    private func decodeWorkspace(_ record: SyncRecord, value: [String: Any]) throws -> Workspace {
        let resultVersion = try version(value, requiredFields: Self.workspaceFields)
        let accent = try optionalARGB(value).map { String(format: "#%08x", $0) }
        let deleted = try tombstone(record, value: value)
        try validatePortableEnvelope(record, dataClass: .workspace, identity: id(value),
                                     version: resultVersion, tombstone: deleted)
        return Workspace(
            workspaceID: WorkspaceID(rawValue: try id(value)),
            name: try string(value, "name"),
            icon: try string(value, "icon"),
            accent: accent,
            sortKey: try string(value, "sort_key"),
            createdAt: try clock(value, timeKey: "created_at"),
            modifiedAt: try clock(value, timeKey: "modified_at"),
            version: resultVersion,
            tombstone: deleted
        )
    }

    public func decodeTreeNode(_ record: SyncRecord, plaintext: Data) throws -> TreeNode {
        let value = try object(from: plaintext)
        let payloadVersion = try SharedTabWireReadPolicy.payloadVersion(value)
        let isTemporary = try SharedTabWireReadPolicy.validateTreeNodeReadShape(
            value,
            version: payloadVersion
        )
        let resultVersion = try version(
            value,
            requiredFields: try SharedTabWireReadPolicy.requiredTreeNodeFields(
                version: payloadVersion
            )
        )
        try SharedTabWireReadPolicy.validateDecodedVersion(resultVersion)
        let rawKind = Int(try SharedTabWireReadPolicy.strictUInt32(value, key: "node_kind"))
        guard rawKind == 0 || rawKind == 1 else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let kind = rawKind == 0 ? TreeNodeKind.folder : .savedPage
        let target = try decodeTabTarget(value, version: payloadVersion, folder: kind == .folder)
        let rawURL = try string(value, "url")
        let decodedURL: String?
        if kind == .folder {
            guard !isTemporary, rawURL.isEmpty else { throw SharedTabTargetError.invalidTarget }
            decodedURL = nil
        } else {
            guard let target else { throw SharedTabTargetError.invalidTarget }
            try target.validatePage(isTemporary: isTemporary)
            decodedURL = rawURL.isEmpty ? nil : rawURL
        }
        let sortKey = try orderKey(try string(value, "sort_key"), version: resultVersion)
        let result = try TreeNode(
            treeNodeID: TreeNodeID(rawValue: try id(value)),
            workspaceID: WorkspaceID(rawValue: try uuid(value, "workspace_id")),
            parentID: try optionalUUID(value, "parent_id").map(TreeNodeID.init(rawValue:)),
            kind: kind,
            title: try string(value, "title"),
            url: decodedURL,
            icon: try string(value, "icon"),
            accent: try optionalARGB(value).map { String(format: "#%08x", $0) },
            orderKey: sortKey.value,
            wireSortKey: sortKey.wireValue,
            isTemporary: isTemporary,
            targetKind: target?.kind,
            localScheme: target?.localScheme,
            createdAt: try clock(value, timeKey: "created_at"),
            modifiedAt: try clock(value, timeKey: "modified_at"),
            version: resultVersion,
            tombstone: try tombstone(record, value: value)
        )
        try validatePortableEnvelope(
            record,
            dataClass: .treeNode,
            identity: result.id.rawValue,
            version: result.version,
            tombstone: result.tombstone
        )
        return result
    }

    public func decodeSession(
        _ record: SyncRecord,
        plaintext: Data,
        devices: [DeviceID: Device]
    ) throws -> DeviceSession {
        let value = try object(from: plaintext)
        let resultVersion = try version(value, requiredFields: Self.sessionFields)
        let deviceID = DeviceID(rawValue: try uuid(value, "device_id"))
        guard let device = devices[deviceID] else {
            throw DesktopWirePayloadCodecError.missingDependency
        }
        let deleted = try tombstone(record, value: value)
        try validatePortableEnvelope(record, dataClass: .deviceSession, identity: id(value),
                                     version: resultVersion, tombstone: deleted)
        return DeviceSession(
            sessionID: DeviceSessionID(rawValue: try id(value)),
            deviceID: deviceID,
            deviceName: device.name,
            deviceKind: device.kind,
            startedAt: try clock(value, timeKey: "started_at"),
            lastActiveAt: try clock(value, timeKey: "last_seen", fieldName: "liveness"),
            isOnline: try SharedTabWireReadPolicy.strictBoolean(value, key: "active"),
            version: resultVersion,
            tombstone: deleted
        )
    }

    public func decodeRemoteTab(
        _ record: SyncRecord,
        plaintext: Data,
        devices: [DeviceID: Device],
        workspaces: [WorkspaceID: Workspace]
    ) throws -> RemoteTab {
        let value = try object(from: plaintext)
        let payloadVersion = try SharedTabWireReadPolicy.payloadVersion(value)
        let treeNodeID = try SharedTabWireReadPolicy.validateRemoteTabReadShape(
            value,
            version: payloadVersion
        )
        let resultVersion = try version(
            value,
            requiredFields: try SharedTabWireReadPolicy.requiredRemoteTabFields(
                version: payloadVersion
            )
        )
        try SharedTabWireReadPolicy.validateDecodedVersion(resultVersion)
        guard try SharedTabWireReadPolicy.strictBoolean(
            value,
            key: "is_incognito"
        ) == false else {
            throw CompanionModelError.incognitoNotSyncable
        }
        let deviceID = DeviceID(rawValue: try uuid(value, "device_id"))
        guard let device = devices[deviceID] else {
            throw DesktopWirePayloadCodecError.missingDependency
        }
        let workspaceID = try optionalUUID(value, "workspace_id").map(WorkspaceID.init(rawValue:))
        guard let target = try decodeTabTarget(value, version: payloadVersion),
              let treeNodeID else { throw SharedTabTargetError.missingPageLink }
        try target.validatePresence(treeNodeID: treeNodeID)
        let result = try RemoteTab(
            tabID: TabID(rawValue: try id(value)),
            deviceID: deviceID,
            deviceKind: device.kind,
            deviceName: device.name,
            sessionID: DeviceSessionID(rawValue: try uuid(value, "session_id")),
            workspaceID: workspaceID,
            treeNodeID: treeNodeID,
            workspaceName: workspaceID.flatMap { workspaces[$0]?.name },
            title: try string(value, "title"),
            url: try string(value, "url"),
            targetKind: target.kind,
            localScheme: target.localScheme,
            openedAt: try clock(value, timeKey: "opened_at"),
            lastActiveAt: try clock(value, timeKey: "last_active"),
            isOpen: !(try SharedTabWireReadPolicy.strictBoolean(value, key: "tombstone")),
            pinned: try SharedTabWireReadPolicy.strictBoolean(value, key: "pinned"),
            version: resultVersion,
            tombstone: try tombstone(record, value: value)
        )
        guard result.treeNodeID?.rawValue != result.id.rawValue else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        try validatePortableEnvelope(
            record,
            dataClass: .deviceTab,
            identity: result.id.rawValue,
            version: result.version,
            tombstone: result.tombstone,
            requiresAbsentOrderKey: true
        )
        return result
    }

    public func decodeHistory(_ record: SyncRecord, plaintext: Data) throws -> HistoryVisit {
        let value = try object(from: plaintext)
        let resultVersion = try version(value, requiredFields: Self.historyFields)
        let deviceID = DeviceID(rawValue: try uuid(value, "device_id"))
        let rawCount = try string(value, "visit_count")
        guard let count = Int64(rawCount), String(count) == rawCount else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let deleted = try tombstone(record, value: value)
        try validatePortableEnvelope(record, dataClass: .historyVisit, identity: id(value),
                                     version: resultVersion, tombstone: deleted)
        return try HistoryVisit(
            visitID: HistoryVisitID(rawValue: try id(value)),
            deviceID: deviceID,
            title: try string(value, "title"),
            url: try string(value, "url"),
            visitedAt: try clock(value, timeKey: "last_visit"),
            transition: try string(value, "transition"),
            visitCount: count,
            version: resultVersion,
            tombstone: deleted
        )
    }

    public func decodeRemoteCommand(
        _ record: SyncRecord,
        plaintext: Data
    ) throws -> RemoteCommandState {
        let value = try object(from: plaintext)
        let resultVersion = try version(value, requiredFields: Self.commandFields)
        let commandID = try id(value)
        guard try SharedTabWireReadPolicy.strictBoolean(value, key: "tombstone") == false,
              record.tombstone == nil else { throw SyncBoundaryError.invalidTombstone }
        try validatePortableEnvelope(record, dataClass: .remoteCommand, identity: commandID,
                                     version: resultVersion, tombstone: nil)
        let source = DeviceID(rawValue: try uuid(value, "source_device_id"))
        let target = DeviceID(rawValue: try uuid(value, "target_device_id"))
        let resultCode = try string(value, "result")
        guard source != target, resultCode.utf8.count <= 64 else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        guard let nonce = Data(base64Encoded: try string(value, "nonce")),
              let signature = Data(base64Encoded: try string(value, "signature")),
              nonce.count >= 16, nonce.count <= 64, signature.count == 64 else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let issued = try unixMilliseconds(value, "issued_at")
        let expires = try unixMilliseconds(value, "expires_at")
        let (expectedExpiry, expiryOverflow) = issued.addingReportingOverflow(
            RemoteCommandPayload.timeToLiveMilliseconds
        )
        guard !expiryOverflow, expires == expectedExpiry,
              let status = RemoteCommandStatus(rawValue: try integer(value, "status")) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let command: RemoteCommand
        let workspaceID = try optionalUUID(value, "workspace_id").map(WorkspaceID.init(rawValue:))
        let tabID = try optionalUUID(value, "tab_id").map(TabID.init(rawValue:))
        let url = try string(value, "url")
        switch try integer(value, "command_kind") {
        case 0:
            guard tabID == nil, let parsed = URLComponents(string: url),
                  let scheme = parsed.scheme?.lowercased(), ["http", "https"].contains(scheme),
                  parsed.host?.isEmpty == false, parsed.user == nil, parsed.password == nil,
                  parsed.url != nil else { throw DesktopWirePayloadCodecError.malformedPayload }
            command = .open(.init(
                url: url,
                workspaceID: workspaceID
            ))
        case 1:
            guard let tabID, workspaceID == nil, url.isEmpty else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            command = .focus(.init(
                tabID: tabID,
                context: .normal
            ))
        case 2:
            guard let tabID, workspaceID == nil, url.isEmpty else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            command = .close([.init(
                tabID: tabID,
                context: .normal
            )])
        default:
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let payload = RemoteCommandPayload(
            commandID: commandID,
            sourceDeviceID: source,
            targetDeviceID: target,
            nonce: nonce,
            issuedAtMilliseconds: issued,
            command: command
        )
        return RemoteCommandState(
            envelope: .init(payload: payload, signature: signature),
            status: status,
            resultCode: resultCode,
            version: resultVersion
        )
    }

    func common(
        id: UUID, tombstone: Bool, version: SyncVersion, fields: Set<String>
    ) throws -> [String: Any] {
        try SharedTabWireReadPolicy.validateWriterVersion(version.schemaVersion)
        _ = try SharedTabWireReadPolicy.strictUUID(["id": uuid(id)], key: "id")
        let supplied = Set(version.fieldVersions.keys)
        guard !fields.isEmpty, supplied.isEmpty || supplied == fields else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
        // Only a completely new local clock map may be completed here. Partial
        // or unknown maps are rejected rather than assigned synthetic clocks.
        let clocks = supplied.isEmpty
            ? Dictionary(uniqueKeysWithValues: fields.map { ($0, version.modifiedAt) })
            : version.fieldVersions
        let authored = SyncVersion(schemaVersion: SharedSyncFormat.currentVersion,
                                   modifiedAt: version.modifiedAt, modifiedBy: version.modifiedBy,
                                   fieldVersions: clocks)
        try SharedSyncFormat.validate(authored, fields: fields)
        var value: [String: Any] = [
            "id": uuid(id),
            "model_version": Int(SharedSyncFormat.currentVersion),
            "tombstone": tombstone,
            "version_model": Int(SharedSyncFormat.currentVersion),
            "version_physical": try windowsMicroseconds(authored.modifiedAt),
            "version_logical": Int(authored.modifiedAt.logicalCounter),
            "version_device": uuid(authored.modifiedBy.rawValue),
        ]
        value["field_versions"] = try Dictionary(uniqueKeysWithValues:
            authored.fieldVersions.map { name, clock in
                (name, [
                    "physical": try windowsMicroseconds(clock),
                    "logical": Int(clock.logicalCounter),
                    "device": uuid(clock.nodeID.rawValue),
                ] as [String: Any])
            }
        )
        return value
    }

    func version(
        _ value: [String: Any], requiredFields: Set<String>? = nil
    ) throws -> SyncVersion {
        let schema = try SharedTabWireReadPolicy.payloadVersion(value)
        _ = try id(value)
        _ = try SharedTabWireReadPolicy.strictBoolean(value, key: "tombstone")
        guard let rawFields = value["field_versions"] as? [String: Any],
              !rawFields.isEmpty else { throw SharedTabWirePreparationError.invalidFieldMap }
        let supplied = Set(rawFields.keys)
        let knownMaps: [Set<String>] = [
            Self.deviceFields, Self.workspaceFields, Self.treeNodeFields,
            Self.historyFields, Self.remoteTabFields, Self.sessionFields,
            Self.commandFields, Self.appearanceFields, Self.permittedSettingFields,
            Self.extensionInventoryFields, Self.developerAssetFields,
            BookmarkRecord.syncFields, DeviceCapabilityRecord.syncFields,
        ]
        guard requiredFields.map({ supplied == $0 }) ?? knownMaps.contains(supplied) else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
        let stamp = try SharedTabWireReadPolicy.parseClock(
            value, physicalKey: "version_physical", logicalKey: "version_logical", deviceKey: "version_device"
        )
        let fields = try SharedTabWireReadPolicy.fieldClocks(value, expected: supplied)
        let result = SyncVersion(schemaVersion: schema, modifiedAt: stamp,
                                 modifiedBy: stamp.nodeID, fieldVersions: fields)
        try SharedTabWireReadPolicy.validateDecodedVersion(result)
        return result
    }

    private func clock(
        _ value: [String: Any], timeKey: String, fieldName: String? = nil
    ) throws -> HybridLogicalClock {
        let raw = try SharedTabWireReadPolicy.canonicalWindowsMicroseconds(value, key: timeKey)
        let parsedVersion = try version(value)
        guard let fieldClock = parsedVersion.fieldVersions[fieldName ?? timeKey] else {
            throw SharedTabWirePreparationError.invalidFieldMap
        }
        let micros = UInt64(raw - Self.windowsToUnixMicroseconds)
        return .init(physicalMilliseconds: micros / 1_000,
                     submillisecondMicroseconds: UInt16(micros % 1_000),
                     logicalCounter: fieldClock.logicalCounter, nodeID: fieldClock.nodeID)
    }

    func tombstone(_ record: SyncRecord, value: [String: Any]) throws -> Tombstone? {
        guard record.schemaVersion == SharedSyncFormat.currentVersion else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let deleted = try SharedTabWireReadPolicy.strictBoolean(value, key: "tombstone")
        if !deleted {
            guard record.tombstone == nil else { throw DesktopWirePayloadCodecError.malformedPayload }
            return nil
        }
        guard record.dataClass != .remoteCommand,
              let tombstone = record.tombstone,
              tombstone.entityID == record.entityID,
              tombstone.deletedAt == record.modifiedAt,
              tombstone.deletedBy == record.originatingDevice,
              tombstone.purgeAfterMilliseconds > tombstone.deletedAt.physicalMilliseconds else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return tombstone
    }

    func validatePortableEnvelope(
        _ record: SyncRecord, dataClass: SyncDataClass, identity: UUID,
        version: SyncVersion, tombstone: Tombstone?,
        requiresAbsentOrderKey: Bool = false
    ) throws {
        guard record.schemaVersion == SharedSyncFormat.currentVersion,
              record.recordID == identity, record.entityID == identity,
              record.dataClass == dataClass, record.schemaVersion == version.schemaVersion,
              record.modifiedAt == version.modifiedAt,
              record.originatingDevice == version.modifiedBy, record.tombstone == tombstone,
              !requiresAbsentOrderKey || record.orderKey == nil else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
    }

    private func orderKey(
        _ raw: String,
        version: SyncVersion
    ) throws -> (value: OrderKey, wireValue: String?) {
        if let separator = raw.firstIndex(of: "!"),
           let tieBreaker = UUID(uuidString: String(raw[raw.index(after: separator)...])) {
            let parts = raw[..<separator].split(separator: ".")
            let components = parts.compactMap { UInt16($0, radix: 16) }
            if components.count == parts.count,
               let result = try? OrderKey(
                   components: components,
                   tieBreaker: DeviceID(rawValue: tieBreaker)
               ) {
                return (result, nil)
            }
        }
        // Current sort_key is an opaque lexical value. Retain its exact bytes
        // while adapting it to the local OrderKey representation.
        let components = raw.utf8.prefix(OrderKey.maximumDepth).map(UInt16.init)
        guard !components.isEmpty else {
            throw DesktopWirePayloadCodecError.invalidOrderKey
        }
        return (try OrderKey(
            components: components,
            tieBreaker: version.modifiedBy
        ), raw)
    }

    func timeString(_ clock: HybridLogicalClock) throws -> String {
        try windowsMicroseconds(clock)
    }

    private func timeString(milliseconds: UInt64) throws -> String {
        guard milliseconds <= UInt64(
            (Int64.max - Self.windowsToUnixMicroseconds) / 1_000
        ) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return String(Int64(milliseconds) * 1_000 + Self.windowsToUnixMicroseconds)
    }

    private func unixMilliseconds(_ value: [String: Any], _ key: String) throws -> UInt64 {
        let raw = try SharedTabWireReadPolicy.canonicalWindowsMicroseconds(value, key: key)
        guard raw % 1_000 == 0 else { throw DesktopWirePayloadCodecError.malformedPayload }
        return UInt64((raw - Self.windowsToUnixMicroseconds) / 1_000)
    }

    private func windowsMicroseconds(_ clock: HybridLogicalClock) throws -> String {
        try SharedTabWireReadPolicy.validateClock(clock)
        guard clock.physicalMicroseconds <= UInt64(
            Int64.max - Self.windowsToUnixMicroseconds
        ) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return String(
            Int64(clock.physicalMicroseconds) + Self.windowsToUnixMicroseconds
        )
    }

    func serialize(_ value: [String: Any]) throws -> Data {
        try JSONSerialization.data(
            withJSONObject: value,
            options: [.sortedKeys, .withoutEscapingSlashes]
        )
    }

    func id(_ value: [String: Any]) throws -> UUID { try uuid(value, "id") }
    func uuid(_ value: UUID) -> String { value.uuidString.lowercased() }

    func uuid(_ value: [String: Any], _ key: String) throws -> UUID {
        try SharedTabWireReadPolicy.strictUUID(value, key: key)
    }

    func optionalUUID(_ value: [String: Any], _ key: String) throws -> UUID? {
        guard value.keys.contains(key) else { return nil }
        return try uuid(value, key)
    }

    func string(_ value: [String: Any], _ key: String) throws -> String {
        guard let result = value[key] as? String else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    func integer(_ value: [String: Any], _ key: String) throws -> Int {
        try SharedTabWireReadPolicy.strictInteger(value, key: key)
    }
}
