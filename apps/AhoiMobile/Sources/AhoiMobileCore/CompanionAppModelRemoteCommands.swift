import Foundation
import AhoiCloudKitSpike

public struct CompanionRemoteCommandStatusItem: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let action: String
    public let targetDeviceID: DeviceID
    public let status: RemoteCommandStatus
    public let resultCode: String
    public let issuedAtMilliseconds: UInt64
    public let expiresAtMilliseconds: UInt64

    public var isTerminal: Bool {
        status == .executed || status == .failed
    }

    public var isExpired: Bool {
        status == .failed && resultCode == "expired"
    }
}

enum CompanionRemoteCommandOrdering {
    static func newestFirst(
        _ lhs: RemoteCommandState,
        _ rhs: RemoteCommandState
    ) -> Bool {
        let lhsIssuedAt = lhs.envelope.payload.issuedAtMilliseconds
        let rhsIssuedAt = rhs.envelope.payload.issuedAtMilliseconds
        if lhsIssuedAt != rhsIssuedAt {
            return lhsIssuedAt > rhsIssuedAt
        }
        if lhs.version != rhs.version {
            return lhs.version > rhs.version
        }
        return lhs.id.uuidString > rhs.id.uuidString
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

    func refreshRemoteCommandStates(
        using bridge: CompanionSyncBridge,
        nowMilliseconds: UInt64? = nil
    ) async {
        let states = await bridge.remoteCommandStates(
            limit: CompanionRemoteCommandRetention.maximumReadModelCount
        )
        reconcileRemoteCommandStates(states, nowMilliseconds: nowMilliseconds)
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
            presentOperationFailure(error)
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
            _ = updateRemoteCommandStatusItem(state, action: action)
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
            let item = updateRemoteCommandStatusItem(updated ?? state, action: action)
            remoteCommandStatus = statusText(item)
            snapshot = try await repository.currentSnapshot()
            syncStatus = provider.status()
            syncSafetyState = provider.safetyState()
            loadError = nil
            // Later acknowledgements are imported by the provider callback or
            // an explicit launch/foreground/manual sync. A per-command timer
            // would multiply foreground wakeups and violate that contract.
        } catch {
            remoteCommandStatus = CompanionL10n.format(
                "remote.send_failed",
                fallback: "%@ was not sent.",
                action
            )
            presentOperationFailure(error)
            syncStatus = syncProvider?.status()
        }
    }

    private func statusText(_ item: CompanionRemoteCommandStatusItem) -> String {
        if item.isExpired {
            return CompanionL10n.format(
                "remote.status.expired",
                fallback: "%@ expired before confirmation.",
                item.action
            )
        }
        return switch item.status {
        case .queued:
            CompanionL10n.format(
                "remote.status.queued",
                fallback: "%@ was sent; confirmation is pending.",
                item.action
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
                item.action
            )
        case .failed:
            CompanionL10n.string(
                "remote.status.failed",
                fallback: "The Mac rejected the command safely."
            )
        }
    }

    func reconcileRemoteCommandStates(
        _ states: [RemoteCommandState],
        nowMilliseconds: UInt64? = nil
    ) {
        let now = nowMilliseconds ?? currentTimeMilliseconds()
        recentRemoteCommands = boundedRemoteCommandPresentation(states.map { state in
                makeRemoteCommandStatusItem(
                    state,
                    action: commandLabels[state.id],
                    nowMilliseconds: now
                )
            })
        let trackedIDs = Set(recentRemoteCommands.lazy.filter {
            !$0.isTerminal
        }.map(\.id))
        commandLabels = commandLabels.filter { trackedIDs.contains($0.key) }
        if let latest = recentRemoteCommands.first {
            remoteCommandStatus = statusText(latest)
        } else {
            remoteCommandStatus = nil
        }
        scheduleRemoteCommandExpiryRefresh(nowMilliseconds: now)
    }

    @discardableResult
    func updateRemoteCommandStatusItem(
        _ state: RemoteCommandState,
        action: String,
        nowMilliseconds: UInt64? = nil
    ) -> CompanionRemoteCommandStatusItem {
        let item = makeRemoteCommandStatusItem(
            state,
            action: action,
            nowMilliseconds: nowMilliseconds ?? currentTimeMilliseconds()
        )
        recentRemoteCommands.removeAll { $0.id == item.id }
        recentRemoteCommands.insert(item, at: 0)
        let retained = boundedRemoteCommandPresentation(recentRemoteCommands)
        let retainedIDs = Set(retained.map(\.id))
        for command in recentRemoteCommands where !retainedIDs.contains(command.id) {
            commandLabels[command.id] = nil
        }
        recentRemoteCommands = retained
        if item.isTerminal {
            commandLabels[item.id] = nil
        }
        scheduleRemoteCommandExpiryRefresh(nowMilliseconds: nowMilliseconds)
        return item
    }

    private func makeRemoteCommandStatusItem(
        _ state: RemoteCommandState,
        action: String?,
        nowMilliseconds: UInt64
    ) -> CompanionRemoteCommandStatusItem {
        let expiresAt = state.envelope.payload.expiresAtMilliseconds
        let expired = !state.status.isTerminal && nowMilliseconds >= expiresAt
        return CompanionRemoteCommandStatusItem(
            id: state.id,
            action: action ?? actionLabel(for: state.envelope.payload.command),
            targetDeviceID: state.envelope.payload.targetDeviceID,
            status: expired ? .failed : state.status,
            resultCode: expired ? "expired" : state.resultCode,
            issuedAtMilliseconds: state.envelope.payload.issuedAtMilliseconds,
            expiresAtMilliseconds: expiresAt
        )
    }

    /// Keep every bounded live command observable until completion/TTL while
    /// retaining only a small newest terminal history. A burst of completed
    /// commands must never hide an older command that still needs an expiry
    /// transition.
    private func boundedRemoteCommandPresentation(
        _ items: [CompanionRemoteCommandStatusItem]
    ) -> [CompanionRemoteCommandStatusItem] {
        let ordered = items.sorted(by: remoteCommandItemNewestFirst)
        let active = Array(ordered.lazy.filter { !$0.isTerminal }
            .prefix(CompanionRemoteCommandRetention.maximumReadModelCount))
        let remaining = max(
            0,
            CompanionRemoteCommandRetention.maximumReadModelCount - active.count
        )
        let history = ordered.lazy.filter(\.isTerminal).prefix(min(
            CompanionRemoteCommandRetention.terminalHistoryCount,
            remaining
        ))
        return (active + Array(history)).sorted(by: remoteCommandItemNewestFirst)
    }

    private func remoteCommandItemNewestFirst(
        _ lhs: CompanionRemoteCommandStatusItem,
        _ rhs: CompanionRemoteCommandStatusItem
    ) -> Bool {
        if lhs.issuedAtMilliseconds != rhs.issuedAtMilliseconds {
            return lhs.issuedAtMilliseconds > rhs.issuedAtMilliseconds
        }
        return lhs.id.uuidString > rhs.id.uuidString
    }

    private func actionLabel(for command: RemoteCommand) -> String {
        switch command {
        case .open:
            CompanionL10n.string("remote.action.open", fallback: "Open")
        case .focus:
            CompanionL10n.string("remote.action.focus", fallback: "Focus")
        case .close:
            CompanionL10n.string("remote.action.close", fallback: "Close")
        }
    }

    func cancelRemoteCommandExpiryRefresh() {
        remoteCommandExpiryGeneration &+= 1
        remoteCommandExpiryTask?.cancel()
        remoteCommandExpiryTask = nil
    }

    private func scheduleRemoteCommandExpiryRefresh(
        nowMilliseconds suppliedNow: UInt64? = nil
    ) {
        cancelRemoteCommandExpiryRefresh()
        let now = suppliedNow ?? currentTimeMilliseconds()
        guard let deadline = recentRemoteCommands.lazy
            .filter({ !$0.isTerminal })
            .map(\.expiresAtMilliseconds)
            .min() else { return }
        let delay = deadline > now ? deadline - now : 0
        let generation = remoteCommandExpiryGeneration
        let sleeper = remoteCommandSleeper
        remoteCommandExpiryTask = Task { @MainActor [weak self] in
            await sleeper(delay)
            guard !Task.isCancelled, let self,
                  self.remoteCommandExpiryGeneration == generation else { return }
            self.applyRemoteCommandExpirations()
        }
    }

    private func applyRemoteCommandExpirations() {
        let now = currentTimeMilliseconds()
        var changed = false
        recentRemoteCommands = recentRemoteCommands.map { item in
            guard !item.isTerminal,
                  item.expiresAtMilliseconds <= now else { return item }
            changed = true
            commandLabels[item.id] = nil
            return CompanionRemoteCommandStatusItem(
                id: item.id,
                action: item.action,
                targetDeviceID: item.targetDeviceID,
                status: .failed,
                resultCode: "expired",
                issuedAtMilliseconds: item.issuedAtMilliseconds,
                expiresAtMilliseconds: item.expiresAtMilliseconds
            )
        }
        if changed {
            remoteCommandStatus = recentRemoteCommands.first.map(statusText)
        }
        scheduleRemoteCommandExpiryRefresh(nowMilliseconds: now)
    }

    private func currentTimeMilliseconds() -> UInt64 {
        remoteCommandClock()
    }
}

private extension RemoteCommandStatus {
    var isTerminal: Bool {
        self == .executed || self == .failed
    }
}
