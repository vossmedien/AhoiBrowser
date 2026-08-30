import Foundation
import AhoiCloudKitSpike

public struct CompanionRemoteCommandStatusItem: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let action: String
    public let targetDeviceID: DeviceID
    public let status: RemoteCommandStatus
    public let resultCode: String
    public let expiresAtMilliseconds: UInt64

    public var isTerminal: Bool {
        status == .executed || status == .failed
    }
}

@MainActor
extension CompanionAppModel {
    public func remotelyOpen(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .open(.init(url: tab.url, workspaceID: tab.workspaceID)),
            target: tab.deviceID,
            action: CompanionL10n.string("remote.action.open", fallback: "Open")
        )
    }

    public func sendLink(
        _ url: String,
        to target: DeviceID,
        workspaceID: WorkspaceID?
    ) async {
        await sendRemoteCommand(
            .open(.init(url: url, workspaceID: workspaceID)),
            target: target,
            action: CompanionL10n.string(
                "remote.action.send_link",
                fallback: "Send link"
            )
        )
    }

    public func remotelyFocus(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .focus(.init(tabID: tab.id, context: .normal)),
            target: tab.deviceID,
            action: CompanionL10n.string("remote.action.focus", fallback: "Focus")
        )
    }

    public func remotelyClose(_ tab: RemoteTab) async {
        await sendRemoteCommand(
            .close([.init(tabID: tab.id, context: .normal)]),
            target: tab.deviceID,
            action: CompanionL10n.string("remote.action.close", fallback: "Close")
        )
    }

    public func visibleTabs(for workspaceID: WorkspaceID?) -> [RemoteTab] {
        snapshot.visibleRemoteTabs.filter { tab in
            guard let workspaceID else { return true }
            return tab.workspaceID == workspaceID
        }
    }

    public var actionableRemoteTabIDs: Set<TabID> {
        Set(snapshot.visibleRemoteTabs.filter {
            snapshot.isRemoteTabActionable($0)
        }.map(\.id))
    }

    func refreshRemoteCommandStates(using bridge: CompanionSyncBridge) async {
        let states = await bridge.remoteCommandStates(Set(commandLabels.keys))
        for state in states {
            let action = commandLabels[state.id] ?? CompanionL10n.string(
                "remote.action.generic",
                fallback: "Remote command"
            )
            updateRemoteCommandStatusItem(state, action: action)
            if recentRemoteCommands.first(where: { $0.id == state.id })?.isTerminal == true {
                commandFollowUpTasks[state.id]?.cancel()
                commandFollowUpTasks[state.id] = nil
            }
        }
    }

    private func sendRemoteCommand(
        _ command: RemoteCommand,
        target: DeviceID,
        action: String
    ) async {
        guard canTargetRemoteCommand(target) else {
            remoteCommandStatus = CompanionL10n.string(
                "remote.device_unavailable",
                fallback: "This device is unavailable or has been revoked."
            )
            return
        }
        guard let syncBridge, let provider = syncProvider else {
            remoteCommandStatus = CompanionL10n.string(
                "remote.not_configured",
                fallback: "Remote control is not configured."
            )
            return
        }
        do {
            let sourceDeviceID = try await syncBridge.remoteControlSourceDeviceID()
            if let sourceDevice = snapshot.devices.first(where: {
                $0.id == sourceDeviceID
            }), sourceDevice.isDeleted || sourceDevice.isRevoked {
                remoteCommandStatus = CompanionL10n.string(
                    "remote.source_revoked",
                    fallback: "This device's remote-control identity is revoked."
                )
                return
            }
            guard try await !syncBridge.remoteControlIdentityIsRevoked() else {
                remoteCommandStatus = CompanionL10n.string(
                    "remote.source_revoked",
                    fallback: "This device's remote-control identity is revoked."
                )
                return
            }
        } catch {
            remoteCommandStatus = CompanionL10n.string(
                "remote.not_configured",
                fallback: "Remote control is not configured."
            )
            loadError = error.localizedDescription
            return
        }
        do {
            remoteCommandStatus = CompanionL10n.format(
                "remote.signing",
                fallback: "%@ is being signed…",
                action
            )
            let state = try await syncBridge.enqueueRemoteCommand(
                targetDeviceID: target,
                command: command
            )
            commandLabels[state.id] = action
            updateRemoteCommandStatusItem(state, action: action)
            remoteCommandStatus = CompanionL10n.format(
                "remote.queued_securely",
                fallback: "%@ was queued securely.",
                action
            )
            let generation = syncGeneration
            await sync()
            guard isCurrentSyncRuntime(provider, generation: generation),
                  self.syncBridge === syncBridge else { return }
            let updated = await syncBridge.remoteCommandState(state.id)
            if let updated { updateRemoteCommandStatusItem(updated, action: action) }
            remoteCommandStatus = statusText(updated?.status ?? .queued, action: action)
            snapshot = try await repository.currentSnapshot()
            syncStatus = provider.status()
            syncSafetyState = provider.safetyState()
            loadError = nil
            beginCommandFollowUp(
                commandID: state.id,
                expiresAtMilliseconds: state.envelope.payload.expiresAtMilliseconds
            )
        } catch {
            remoteCommandStatus = CompanionL10n.format(
                "remote.send_failed",
                fallback: "%@ was not sent.",
                action
            )
            loadError = error.localizedDescription
            syncStatus = syncProvider?.status()
        }
    }

    private func statusText(_ status: RemoteCommandStatus, action: String) -> String {
        switch status {
        case .queued:
            CompanionL10n.format(
                "remote.status.queued",
                fallback: "%@ was sent; confirmation is pending.",
                action
            )
        case .delivered:
            CompanionL10n.string(
                "remote.status.delivered",
                fallback: "The Mac verified the command."
            )
        case .executed:
            CompanionL10n.format(
                "remote.status.executed",
                fallback: "%@ was completed on the Mac.",
                action
            )
        case .failed:
            CompanionL10n.string(
                "remote.status.failed",
                fallback: "The Mac rejected the command safely."
            )
        }
    }

    private func updateRemoteCommandStatusItem(
        _ state: RemoteCommandState,
        action: String
    ) {
        let item = CompanionRemoteCommandStatusItem(
            id: state.id,
            action: action,
            targetDeviceID: state.envelope.payload.targetDeviceID,
            status: state.status,
            resultCode: state.resultCode,
            expiresAtMilliseconds: state.envelope.payload.expiresAtMilliseconds
        )
        recentRemoteCommands.removeAll { $0.id == item.id }
        recentRemoteCommands.insert(item, at: 0)
        if recentRemoteCommands.count > 20 {
            recentRemoteCommands.removeLast(recentRemoteCommands.count - 20)
        }
    }

    private func beginCommandFollowUp(
        commandID: UUID,
        expiresAtMilliseconds: UInt64
    ) {
        commandFollowUpTasks[commandID]?.cancel()
        commandFollowUpTasks[commandID] = Task { [weak self] in
            while !Task.isCancelled {
                let now = UInt64(Date().timeIntervalSince1970 * 1_000)
                guard now < expiresAtMilliseconds else { break }
                do {
                    try await Task.sleep(for: .seconds(5))
                } catch {
                    break
                }
                guard let self,
                      let bridge = self.syncBridge,
                      let provider = self.syncProvider else { break }
                let generation = self.syncGeneration
                await self.sync()
                guard self.isCurrentSyncRuntime(provider, generation: generation),
                      self.syncBridge === bridge else { break }
                await self.refreshRemoteCommandStates(using: bridge)
                self.syncStatus = provider.status()
                if self.recentRemoteCommands.first(where: {
                    $0.id == commandID
                })?.isTerminal == true { break }
            }
            self?.commandFollowUpTasks[commandID] = nil
        }
    }
}
