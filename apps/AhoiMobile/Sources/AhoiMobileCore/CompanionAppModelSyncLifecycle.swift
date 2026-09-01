import Foundation

extension CompanionAppModel {
    public func retrySyncActivation() async {
        guard desiredSyncEnabled else { return }
        syncActivationCompletedIntentGeneration = nil
        await setSyncEnabled(true)
    }

    /// Applies the user-owned transport preference at runtime. Disabling drops
    /// the CKSyncEngine/provider immediately while retaining all local data;
    /// enabling constructs CKContainer only after this call.
    public func setSyncEnabled(_ enabled: Bool) async {
        if desiredSyncEnabled != enabled {
            syncPreferenceIntentGeneration &+= 1
            desiredSyncEnabled = enabled
        }
        let intentGeneration = syncPreferenceIntentGeneration

        guard enabled else {
            await disableSyncRuntime()
            return
        }

        await waitForRuntimeCancellation()
        guard isCurrentSyncIntent(intentGeneration) else { return }

        await waitForSyncActivation()
        guard isCurrentSyncIntent(intentGeneration) else { return }
        guard syncProvider == nil else { return }
        guard syncActivationCompletedIntentGeneration != intentGeneration else { return }
        guard let syncRuntimeFactory else {
            isSyncConfigured = false
            loadError = CompanionL10n.string(
                "sync.configuration_missing",
                fallback: "CloudKit is not fully configured. Local data remains available."
            )
            return
        }

        syncActivationInProgress = true
        let activation: CompanionSyncRuntimeActivation
        do {
            activation = try await syncRuntimeFactory()
        } catch {
            if isCurrentSyncIntent(intentGeneration) {
                keyLifecycleStatus = .recovery(reason: .keychainFailure, keyVersion: nil)
                isSyncConfigured = false
                presentOperationFailure(error)
            }
            finishSyncActivation(for: intentGeneration)
            return
        }

        guard isCurrentSyncIntent(intentGeneration) else {
            await activation.discardRuntime()
            finishSyncActivation(for: intentGeneration)
            return
        }

        guard let runtime = activation.runtime,
              activation.status.permitsEncryptedDomainRecords else {
            await activation.discardRuntime()
            if isCurrentSyncIntent(intentGeneration) {
                keyLifecycleStatus = activation.status
                isSyncConfigured = false
                loadError = activation.status.localizedSummary
            }
            finishSyncActivation(for: intentGeneration)
            return
        }

        syncGeneration &+= 1
        let runtimeGeneration = syncGeneration
        syncProvider = runtime.provider
        syncBridge = runtime.bridge
        bindEventDrivenSync(to: runtime.provider)
        keyLifecycleStatus = activation.status
        isSyncConfigured = true
        loadError = nil
        finishSyncActivation(for: intentGeneration)

        do {
            let identity = try await runtime.bridge.remoteControlIdentity()
            guard isCurrentSyncIntent(intentGeneration),
                  isCurrentSyncRuntime(runtime.provider, generation: runtimeGeneration) else {
                return
            }
            remoteControlIdentity = identity
        } catch RemoteCommandSignerError.identityRevoked {
            guard isCurrentSyncIntent(intentGeneration),
                  isCurrentSyncRuntime(runtime.provider, generation: runtimeGeneration) else {
                return
            }
            // A locally revoked remote-control identity must stay revoked across
            // relaunches, but it must not poison the otherwise healthy sync
            // runtime. Settings retains the explicit re-enrol action.
            remoteControlIdentity = nil
        } catch {
            guard isCurrentSyncIntent(intentGeneration),
                  isCurrentSyncRuntime(runtime.provider, generation: runtimeGeneration) else {
                return
            }
            remoteControlIdentity = nil
            presentOperationFailure(error)
        }
        guard isCurrentSyncIntent(intentGeneration),
              isCurrentSyncRuntime(runtime.provider, generation: runtimeGeneration) else {
            return
        }
        await sync()
    }

    private func disableSyncRuntime() async {
        eventDrivenSyncGeneration &+= 1
        eventDrivenSyncTask?.cancel()
        eventDrivenSyncTask = nil
        eventDrivenSyncRequested = false
        syncRequestedWhileInProgress = false

        syncGeneration &+= 1
        let cancellationGeneration = syncGeneration
        let providerToCancel = syncProvider
        providerToCancel?.setEventDrivenSyncHandler(nil)
        syncProvider = nil
        syncBridge = nil
        providerPrepared = false
        clearSyncVisibleUITestRuntime()
        resetDisabledSyncPresentation()

        if let providerToCancel {
            let cancellation = Task<Void, Never> {
                await providerToCancel.cancel()
            }
            syncRuntimeCancellation = cancellation
            syncRuntimeCancellationGeneration = cancellationGeneration
        }
        await waitForRuntimeCancellation()
    }

    private func resetDisabledSyncPresentation() {
        cancelRemoteCommandExpiryRefresh()
        isSyncConfigured = false
        syncStatus = nil
        syncSafetyState = .init()
        physicalDeletionRecoveryRequired = false
        remoteControlIdentity = nil
        remoteCommandStatus = nil
        recentRemoteCommands = []
        commandLabels.removeAll()
        keyLifecycleStatus = .disabled
        loadError = nil
    }

    private func isCurrentSyncIntent(_ generation: UInt64) -> Bool {
        syncPreferenceIntentGeneration == generation && desiredSyncEnabled
    }

    private func waitForSyncActivation() async {
        guard syncActivationInProgress else { return }
        await withCheckedContinuation { continuation in
            syncActivationWaiters.append(continuation)
        }
    }

    private func finishSyncActivation(for intentGeneration: UInt64) {
        syncActivationCompletedIntentGeneration = intentGeneration
        syncActivationInProgress = false
        let waiters = syncActivationWaiters
        syncActivationWaiters.removeAll()
        waiters.forEach { $0.resume() }
    }

    private func waitForRuntimeCancellation() async {
        guard let cancellation = syncRuntimeCancellation else { return }
        let generation = syncRuntimeCancellationGeneration
        await cancellation.value
        guard syncRuntimeCancellationGeneration == generation else { return }
        syncRuntimeCancellation = nil
        syncRuntimeCancellationGeneration = nil
    }
}
