import CryptoKit
import Foundation

/// Frozen ADR 0008 identities and sentinels. None of these values enables a writer.
public enum SharedTabContract {
    public static let feature = "shared-normal-tabs-v3"
    public static let inboxID = WorkspaceID(rawValue: UUID(uuidString: "83699047-edf8-580d-948d-9c37acc35cb6")!)
    public static let systemActor = DeviceID(rawValue: UUID(uuidString: "9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86")!)
    public static let bottom = HybridLogicalClock(physicalMilliseconds: 0, nodeID: systemActor)

    public static func isBottom(_ clock: HybridLogicalClock) -> Bool {
        clock == bottom
    }

    public static func isActualMutation(_ clock: HybridLogicalClock) -> Bool {
        clock.nodeID != systemActor &&
            (clock.physicalMilliseconds > 0 || clock.submillisecondMicroseconds > 0 || clock.logicalCounter > 0)
    }

    public static func capabilityID(for device: DeviceID) -> UUID {
        // UUIDv5 uses SHA-1 for deterministic naming, never authentication or
        // encryption. The standard URL namespace and name are frozen wire data.
        let namespace = UUID(uuidString: "6ba7b811-9dad-11d1-80b4-00c04fd430c8")!
        var namespaceBytes = namespace.uuid
        var input = withUnsafeBytes(of: &namespaceBytes) { Data($0) }
        input.append(Data("ahoi:sync:capability:v1:\(device.rawValue.uuidString.lowercased())".utf8))
        var bytes = Array(Insecure.SHA1.hash(data: input).prefix(16))
        bytes[6] = (bytes[6] & 0x0f) | 0x50
        bytes[8] = (bytes[8] & 0x3f) | 0x80
        return UUID(uuid: (bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                           bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                           bytes[12], bytes[13], bytes[14], bytes[15]))
    }
}
