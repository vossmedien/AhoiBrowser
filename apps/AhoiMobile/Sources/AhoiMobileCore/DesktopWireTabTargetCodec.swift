import Foundation
import AhoiCloudKitSpike

extension DesktopWirePayloadCodec {
    func decodeTabTarget(
        _ value: [String: Any], version: UInt32, folder: Bool = false
    ) throws -> SharedTabTarget? {
        if version < 3 || folder {
            guard !value.keys.contains("target_kind"), !value.keys.contains("local_scheme") else {
                throw DesktopWirePayloadCodecError.malformedPayload
            }
            if folder, try string(value, "url") != "" { throw SharedTabTargetError.invalidTarget }
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
        guard presence.version.schemaVersion == 3, presence.targetKind != .web else { return }
        guard let id = presence.treeNodeID, let page = pages[id], !page.isDeleted,
              page.kind == .savedPage, page.version.schemaVersion == 3,
              page.targetKind == presence.targetKind, page.url ?? "" == presence.url,
              page.localScheme == presence.localScheme else { throw SharedTabTargetError.targetMismatch }
        if presence.targetKind == .newTab, !page.isTemporary { throw SharedTabTargetError.targetMismatch }
    }
}
