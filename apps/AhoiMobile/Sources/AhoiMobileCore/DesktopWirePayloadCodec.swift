import Foundation
import AhoiCloudKitSpike

public enum DesktopWirePayloadCodecError: Error, Equatable, Sendable {
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
        default: throw DesktopWirePayloadCodecError.unsupportedDeviceType
        }
        let deleted = try tombstone(record, value: value)
        let retired = (value["retired"] as? Bool) ?? false
        return Device(
            deviceID: DeviceID(rawValue: try id(value)),
            name: try string(value, "display_name"),
            kind: kind,
            createdAt: try clock(value, timeKey: "created_at"),
            lastSeenAt: try clock(value, timeKey: "last_seen"),
            isOnline: !retired && deleted == nil,
            isRevoked: retired || deleted != nil,
            version: resultVersion,
            tombstone: deleted
        )
    }

    public func decodeWorkspace(_ record: SyncRecord, plaintext: Data) throws -> Workspace {
        try decodeWorkspace(record, value: object(from: plaintext))
    }

    private func decodeWorkspace(_ record: SyncRecord, value: [String: Any]) throws -> Workspace {
        let resultVersion = try version(value, requiredFields: Self.workspaceFields)
        let accent = (value["accent_argb"] as? NSNumber).map {
            String(format: "#%08x", $0.uint32Value)
        }
        return Workspace(
            workspaceID: WorkspaceID(rawValue: try id(value)),
            name: try string(value, "name"),
            icon: (value["icon"] as? String) ?? "",
            accent: accent,
            sortKey: value["sort_key"] as? String,
            createdAt: try clock(value, timeKey: "created_at"),
            modifiedAt: try clock(value, timeKey: "modified_at"),
            version: resultVersion,
            tombstone: try tombstone(record, value: value)
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
        _ = try SharedTabFieldReadMerge.field(
            "is_temporary", existing: isTemporary, incoming: isTemporary, legacyDefault: false,
            oldVersion: resultVersion, newVersion: resultVersion
        )
        if payloadVersion == 3 {
            guard let provenance = resultVersion.fieldVersions["created_at"],
                  SharedTabContract.isBottom(provenance) || SharedTabContract.isActualMutation(provenance) else {
                throw SharedTabFieldReadMergeError.invalidLegacyField
            }
        }
        let rawKind = payloadVersion == 3
            ? Int(try SharedTabWireReadPolicy.strictUInt32(value, key: "node_kind"))
            : try integer(value, "node_kind")
        guard rawKind == 0 || rawKind == 1 else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let kind = rawKind == 0 ? TreeNodeKind.folder : .savedPage
        let target = try decodeTabTarget(value, version: payloadVersion, folder: kind == .folder)
        let rawURL = try string(value, "url")
        let decodedURL: String?
        if payloadVersion == 3 {
            if kind == .folder {
                guard !isTemporary, rawURL.isEmpty else {
                    throw DesktopWirePayloadCodecError.malformedPayload
                }
                decodedURL = nil
            } else if rawURL.isEmpty {
                guard isTemporary || target?.kind == .localOnly else {
                    throw DesktopWirePayloadCodecError.malformedPayload
                }
                decodedURL = nil
            } else {
                decodedURL = rawURL
            }
        } else {
            decodedURL = kind == .folder ? nil : rawURL
        }
        let sortKey = try orderKey(try string(value, "sort_key"), version: resultVersion)
        let result = try TreeNode(
            treeNodeID: TreeNodeID(rawValue: try id(value)),
            workspaceID: WorkspaceID(rawValue: try uuid(value, "workspace_id")),
            parentID: try optionalUUID(value, "parent_id").map(TreeNodeID.init(rawValue:)),
            kind: kind,
            title: try string(value, "title"),
            url: decodedURL,
            icon: (value["icon"] as? String) ?? "",
            accent: (value["accent_argb"] as? NSNumber).map {
                String(format: "#%08x", $0.uint32Value)
            },
            orderKey: sortKey.value,
            wireSortKey: sortKey.legacyWireValue,
            isTemporary: isTemporary,
            creationProvenanceKnown: payloadVersion == 3 &&
                resultVersion.fieldVersions["created_at"].map(SharedTabContract.isActualMutation) == true,
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
            throw DesktopWirePayloadCodecError.unsupportedDeviceType
        }
        return DeviceSession(
            sessionID: DeviceSessionID(rawValue: try id(value)),
            deviceID: deviceID,
            deviceName: device.name,
            deviceKind: device.kind,
            startedAt: try clock(value, timeKey: "started_at"),
            lastActiveAt: try clock(value, timeKey: "last_seen", fieldName: "liveness"),
            isOnline: (value["active"] as? Bool) ?? false,
            version: resultVersion,
            tombstone: try tombstone(record, value: value)
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
        _ = try SharedTabFieldReadMerge.field(
            "tree_node_id", existing: treeNodeID, incoming: treeNodeID, legacyDefault: nil,
            oldVersion: resultVersion, newVersion: resultVersion
        )
        guard try SharedTabWireReadPolicy.strictBoolean(
            value,
            key: "is_incognito"
        ) == false else {
            throw CompanionModelError.incognitoNotSyncable
        }
        let deviceID = DeviceID(rawValue: try uuid(value, "device_id"))
        guard let device = devices[deviceID] else {
            throw DesktopWirePayloadCodecError.unsupportedDeviceType
        }
        let workspaceID = try optionalUUID(value, "workspace_id").map(WorkspaceID.init(rawValue:))
        let target = try decodeTabTarget(value, version: payloadVersion)
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
            targetKind: target?.kind,
            localScheme: target?.localScheme,
            openedAt: try clock(value, timeKey: "opened_at"),
            lastActiveAt: try clock(value, timeKey: "last_active"),
            isOpen: !((value["tombstone"] as? Bool) ?? false),
            pinned: (value["pinned"] as? Bool) ?? false,
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
        let deviceID = try optionalUUID(value, "device_id")
            .map(DeviceID.init(rawValue:)) ?? resultVersion.modifiedBy
        return try HistoryVisit(
            visitID: HistoryVisitID(rawValue: try id(value)),
            deviceID: deviceID,
            title: try string(value, "title"),
            url: try string(value, "url"),
            visitedAt: try clock(value, timeKey: "last_visit"),
            transition: (value["transition"] as? String) ?? "desktop",
            visitCount: Int64(try string(value, "visit_count")) ?? 1,
            version: resultVersion,
            tombstone: try tombstone(record, value: value)
        )
    }

    public func decodeRemoteCommand(
        _ record: SyncRecord,
        plaintext: Data
    ) throws -> RemoteCommandState {
        let value = try object(from: plaintext)
        let resultVersion = try version(value, requiredFields: Self.commandFields)
        let commandID = try id(value)
        let source = DeviceID(rawValue: try uuid(value, "source_device_id"))
        let target = DeviceID(rawValue: try uuid(value, "target_device_id"))
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
        switch try integer(value, "command_kind") {
        case 0:
            command = .open(.init(
                url: try string(value, "url"),
                workspaceID: try optionalUUID(value, "workspace_id")
                    .map(WorkspaceID.init(rawValue:))
            ))
        case 1:
            command = .focus(.init(
                tabID: TabID(rawValue: try uuid(value, "tab_id")),
                context: .normal
            ))
        case 2:
            command = .close([.init(
                tabID: TabID(rawValue: try uuid(value, "tab_id")),
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
            resultCode: try string(value, "result"),
            version: resultVersion
        )
    }

    func common(
        id: UUID,
        tombstone: Bool,
        version: SyncVersion,
        fields: Set<String>
    ) throws -> [String: Any] {
        let normalized = version.normalized(for: fields)
        var value: [String: Any] = [
            "id": uuid(id),
            "model_version": Int(normalized.schemaVersion),
            "tombstone": tombstone,
            "version_model": Int(normalized.schemaVersion),
            "version_physical": try windowsMicroseconds(normalized.modifiedAt),
            "version_logical": Int(normalized.modifiedAt.logicalCounter),
            "version_device": uuid(normalized.modifiedBy.rawValue),
        ]
        if normalized.schemaVersion >= 2 {
            value["field_versions"] = try Dictionary(uniqueKeysWithValues:
                normalized.fieldVersions.map { name, clock in
                    (
                        name,
                        [
                            "physical": try windowsMicroseconds(clock),
                            "logical": Int(clock.logicalCounter),
                            "device": uuid(clock.nodeID.rawValue),
                        ] as [String: Any]
                    )
                }
            )
        }
        return value
    }

    func version(
        _ value: [String: Any],
        requiredFields: Set<String>? = nil
    ) throws -> SyncVersion {
        guard let rawPhysical = Int64(try string(value, "version_physical")),
              rawPhysical >= Self.windowsToUnixMicroseconds,
              let modelVersion = UInt32(exactly: try integer(value, "version_model")),
              let recordModel = UInt32(exactly: try integer(value, "model_version")),
              modelVersion == recordModel,
              let logical = UInt32(exactly: try integer(value, "version_logical")) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let device = DeviceID(rawValue: try uuid(value, "version_device"))
        let micros = UInt64(rawPhysical - Self.windowsToUnixMicroseconds)
        var fieldVersions: [String: HybridLogicalClock] = [:]
        if modelVersion >= 2 {
            guard let encodedFields = value["field_versions"] as? [String: Any],
                  requiredFields.map({ Set(encodedFields.keys) == $0 }) ?? true else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            for (name, encoded) in encodedFields {
                guard let stamp = encoded as? [String: Any],
                      let rawFieldPhysical = stamp["physical"] as? String,
                      let fieldPhysical = Int64(rawFieldPhysical),
                      fieldPhysical >= Self.windowsToUnixMicroseconds,
                      let fieldLogicalNumber = stamp["logical"] as? NSNumber,
                      let fieldLogical = UInt32(exactly: fieldLogicalNumber.int64Value),
                      let fieldDeviceString = stamp["device"] as? String,
                      let fieldDeviceUUID = UUID(uuidString: fieldDeviceString) else {
                    throw DesktopWirePayloadCodecError.malformedPayload
                }
                let fieldMicros = UInt64(
                    fieldPhysical - Self.windowsToUnixMicroseconds
                )
                let fieldClock = HybridLogicalClock(
                    physicalMilliseconds: fieldMicros / 1_000,
                    submillisecondMicroseconds: UInt16(fieldMicros % 1_000),
                    logicalCounter: fieldLogical,
                    nodeID: DeviceID(rawValue: fieldDeviceUUID)
                )
                fieldVersions[name] = fieldClock
            }
        }
        return SyncVersion(
            schemaVersion: modelVersion,
            modifiedAt: .init(
                physicalMilliseconds: micros / 1_000,
                submillisecondMicroseconds: UInt16(micros % 1_000),
                logicalCounter: logical,
                nodeID: device
            ),
            modifiedBy: device,
            fieldVersions: fieldVersions
        )
    }

    private func clock(
        _ value: [String: Any],
        timeKey: String,
        fieldName: String? = nil
    ) throws -> HybridLogicalClock {
        guard let raw = Int64(try string(value, timeKey)),
              raw >= Self.windowsToUnixMicroseconds else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        let parsedVersion = try version(value)
        let fieldClock = parsedVersion.fieldVersions[fieldName ?? timeKey]
            ?? parsedVersion.modifiedAt
        let micros = UInt64(raw - Self.windowsToUnixMicroseconds)
        return .init(
            physicalMilliseconds: micros / 1_000,
            submillisecondMicroseconds: UInt16(micros % 1_000),
            logicalCounter: fieldClock.logicalCounter,
            nodeID: fieldClock.nodeID
        )
    }

    func tombstone(
        _ record: SyncRecord,
        value: [String: Any]
    ) throws -> Tombstone? {
        guard (value["tombstone"] as? Bool) == true else { return nil }
        guard let tombstone = record.tombstone else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return tombstone
    }

    private func validatePortableEnvelope(
        _ record: SyncRecord,
        dataClass: SyncDataClass,
        identity: UUID,
        version: SyncVersion,
        tombstone: Tombstone?,
        requiresAbsentOrderKey: Bool = false
    ) throws {
        guard record.recordID == identity,
              record.entityID == identity,
              record.dataClass == dataClass,
              record.schemaVersion == version.schemaVersion,
              record.modifiedAt == version.modifiedAt,
              record.originatingDevice == version.modifiedBy,
              record.tombstone == tombstone,
              !requiresAbsentOrderKey || record.orderKey == nil else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
    }

    private func orderKey(
        _ raw: String,
        version: SyncVersion
    ) throws -> (value: OrderKey, legacyWireValue: String?) {
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
        guard let raw = Int64(try string(value, key)),
              raw >= Self.windowsToUnixMicroseconds else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return UInt64((raw - Self.windowsToUnixMicroseconds) / 1_000)
    }

    private func windowsMicroseconds(_ clock: HybridLogicalClock) throws -> String {
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
        guard let result = UUID(uuidString: try string(value, key)) else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    func optionalUUID(_ value: [String: Any], _ key: String) throws -> UUID? {
        guard value[key] != nil, !(value[key] is NSNull) else { return nil }
        return try uuid(value, key)
    }

    func string(_ value: [String: Any], _ key: String) throws -> String {
        guard let result = value[key] as? String else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result
    }

    func integer(_ value: [String: Any], _ key: String) throws -> Int {
        guard let result = value[key] as? NSNumber else {
            throw DesktopWirePayloadCodecError.malformedPayload
        }
        return result.intValue
    }
}
