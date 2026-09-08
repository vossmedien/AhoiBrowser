import Foundation
import AhoiCloudKitSpike

extension DesktopWirePayloadCodec {
    func decodeTabTarget(
        _ value: [String: Any], version: UInt32, folder: Bool = false
    ) throws -> SharedTabTarget? {
        try SharedTabWireReadPolicy.validateWriterVersion(version)
        if folder {
            guard !value.keys.contains("target_kind"), !value.keys.contains("local_scheme") else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            if try string(value, "url") != "" { throw SharedTabTargetError.invalidTarget }
            return nil
        }
        let raw = try SharedTabWireReadPolicy.strictUInt32(value, key: "target_kind")
        guard let kind = SharedTabTargetKind(rawValue: Int(raw)) else { throw SharedTabTargetError.invalidTarget }
        var scheme: SharedTabLocalScheme?
        if value.keys.contains("local_scheme") {
            guard let raw = value["local_scheme"] as? String,
                  let parsed = SharedTabLocalScheme(rawValue: raw) else { throw SharedTabTargetError.invalidTarget }
            scheme = parsed
        }
        return try SharedTabTarget(kind: kind, url: string(value, "url"), localScheme: scheme)
    }

    /// Resolving a linked target needs the separately fetched page authority;
    /// the Presence record cannot enroll a new page or invent its availability.
    func validatePresenceTarget(_ presence: RemoteTab, pages: [TreeNodeID: TreeNode]) throws {
        try SharedSyncFormat.validate(presence.version, fields: SharedTabWireReadPolicy.remoteTabBaseFields)
        try SharedTabWireReadPolicy.validateRemoteTabWrite(presence)
        // Deleting Presence never requires its Page to still be live or already
        // downloaded. The validated link/target does not authorize activation.
        if let tombstone = presence.tombstone {
            guard tombstone.entityID == presence.id.rawValue,
                  tombstone.deletedAt == presence.version.modifiedAt,
                  tombstone.deletedBy == presence.version.modifiedBy,
                  tombstone.purgeAfterMilliseconds > tombstone.deletedAt.physicalMilliseconds else {
                throw SyncBoundaryError.invalidTombstone
            }
            return
        }
        guard let id = presence.treeNodeID, let page = pages[id], page.id == id, !page.isDeleted,
              page.kind == .savedPage, page.version.schemaVersion == SharedSyncFormat.currentVersion,
              page.targetKind == presence.targetKind, page.url ?? "" == presence.url,
              page.localScheme == presence.localScheme else { throw SharedTabTargetError.targetMismatch }
        try SharedSyncFormat.validate(page.version, fields: SharedTabWireReadPolicy.treeNodeBaseFields)
        try SharedTabWireReadPolicy.validateTreeNodeWrite(page)
        if presence.targetKind == .newTab, !page.isTemporary { throw SharedTabTargetError.targetMismatch }
    }
}
