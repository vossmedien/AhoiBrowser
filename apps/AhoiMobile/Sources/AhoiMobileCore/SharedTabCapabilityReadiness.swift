import Foundation
import AhoiCloudKitSpike

/// Readiness evaluation only: no transport or writer reads this as an enable
/// switch. Callers must obtain roster/ack facts from authenticated provider state.
enum SharedTabCapabilityReadiness {
    struct Peer: Sendable {
        let deviceID: DeviceID
        var independentlyKnown: Bool = true
        var explicitlyRetired: Bool = false
        var devicePublicationPending: Bool = false
    }

    struct Assessment: Equatable {
        let isReady: Bool
        let blockingDevices: [DeviceID]
        let bootstrapIncomplete: Bool
    }

    static func evaluate(
        localDevice: DeviceID, peers: [Peer], capabilities: [DeviceCapabilityRecord],
        globalSyncEnabled: Bool, normalProfile: Bool, initialFetchComplete: Bool,
        localDeviceAcknowledged: Bool, localCapabilityAcknowledged: Bool
    ) -> Assessment {
        guard globalSyncEnabled, normalProfile, initialFetchComplete,
              localDeviceAcknowledged, localCapabilityAcknowledged,
              peers.contains(where: { $0.deviceID == localDevice && $0.independentlyKnown && !$0.explicitlyRetired }) else {
            return .init(isReady: false, blockingDevices: [], bootstrapIncomplete: true)
        }
        var declarations: [DeviceID: DeviceCapabilityRecord] = [:]
        var conflicts = Set<DeviceID>()
        let peerIDs = Set(peers.map(\.deviceID))
        for declaration in capabilities {
            if peers.contains(where: { $0.deviceID == declaration.deviceID && $0.independentlyKnown &&
                $0.explicitlyRetired && !$0.devicePublicationPending }) { continue }
            guard peerIDs.contains(declaration.deviceID), (try? declaration.validate()) != nil,
                  declarations.updateValue(declaration, forKey: declaration.deviceID) == nil else {
                conflicts.insert(declaration.deviceID)
                continue
            }
        }
        var seen = Set<DeviceID>()
        for peer in peers {
            guard seen.insert(peer.deviceID).inserted else {
                conflicts.insert(peer.deviceID)
                continue
            }
            guard peer.independentlyKnown, !peer.devicePublicationPending else {
                conflicts.insert(peer.deviceID)
                continue
            }
            if peer.explicitlyRetired { continue }
            guard
                  let declaration = declarations[peer.deviceID], !declaration.isDeleted,
                  declaration.readableModels.contains(3), declaration.writableModels.contains(3),
                  declaration.features.contains(SharedTabContract.feature) else {
                conflicts.insert(peer.deviceID)
                continue
            }
        }
        return .init(isReady: conflicts.isEmpty, blockingDevices: conflicts.sorted(), bootstrapIncomplete: false)
    }
}
