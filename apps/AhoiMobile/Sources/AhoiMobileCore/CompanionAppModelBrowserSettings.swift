import Foundation
import AhoiCloudKitSpike

extension CompanionAppModel {
    static let browserSettingsApprovalKey = "AhoiBrowserSettingsSyncApproved"

    public func setBrowserSettingsSyncEnabled(_ enabled: Bool) {
        guard !enabled || (isSyncConfigured && desiredSyncEnabled) else { return }
        browserSettingsApprovalEpoch &+= 1
        let epoch = browserSettingsApprovalEpoch
        isBrowserSettingsSyncEnabled = enabled
        defaults.set(enabled, forKey: Self.browserSettingsApprovalKey)
        // Revoke on the UI thread before an actor hop. A delayed old enable
        // cannot override the epoch installed by this newer user choice.
        syncProvider?.setBrowserSettingApprovedIDs(
            enabled ? CompanionBrowserSettingCatalog.recordIDs : [], epoch: epoch
        )
        localSnapshotReseedRequired = true
        let originalBridge = syncBridge
        let generation = syncGeneration
        Task { [weak self] in
            await originalBridge?.setBrowserSettingsSyncEnabled(enabled, epoch: epoch)
            guard let self, self.syncGeneration == generation,
                  self.browserSettingsApprovalEpoch == epoch else { return }
            await self.sync()
        }
    }

    /// Called only by the native picker/reset action, not by preference change
    /// observation. Updating AppStorage from a remote record cannot echo itself.
    public func setBrowserSearchEngine(_ engine: MobileSearchEngine?) {
        let previous = browserSearchMutationTask
        let token = UUID()
        browserSearchMutationToken = token
        let generation = syncGeneration
        let approvalEpoch = browserSettingsApprovalEpoch
        let originalBridge = syncBridge
        browserSearchMutationTask = Task { [weak self] in
            await previous?.value
            guard let self else { return }
            defer {
                if self.browserSearchMutationToken == token {
                    self.browserSearchMutationTask = nil
                    self.browserSearchMutationToken = nil
                    self.applySharedBrowserSettings()
                }
            }
            do {
                let record = try await self.repository.setBrowserSearchEngine(engine)
                // Publish to the native preference only AFTER the local domain
                // commit. A crash between stores can replay this same version,
                // not erase the choice or create a fresh shared mutation.
                if self.browserSearchMutationToken == token {
                    if let engine {
                        self.defaults.set(
                            engine.rawValue, forKey: MobileBrowserPreferences.searchEngineKey
                        )
                    } else {
                        self.defaults.removeObject(forKey: MobileBrowserPreferences.searchEngineKey)
                    }
                }
                try await self.refreshLocalState()
                if self.isBrowserSettingsSyncEnabled,
                   self.browserSettingsApprovalEpoch == approvalEpoch,
                   self.syncGeneration == generation,
                   self.syncBridge === originalBridge, let originalBridge {
                    try await originalBridge.enqueue(record)
                    await self.sync()
                } else {
                    self.localSnapshotReseedRequired = true
                }
            } catch {
                self.localSnapshotReseedRequired = true
                self.presentOperationFailure(error)
            }
        }
    }

    func applySharedBrowserSettings() {
        guard browserSearchMutationToken == nil else { return }
        let id = CompanionBrowserSettingCatalog.searchEngineSettingID
        let recordID = CompanionBrowserSettingCatalog.recordID(for: id)
        guard let record = snapshot.productRecords.permittedSettings.first(where: {
            $0.id == recordID && CompanionBrowserSettingCatalog.isPortable($0)
        }) else { return }
        let locallyAuthored = record.version.fieldVersions["value_json"]?.nodeID ==
            repository.localDeviceID
        guard isBrowserSettingsSyncEnabled || locallyAuthored else { return }
        guard let value = try? JSONSerialization.jsonObject(
            with: Data(record.valueJSON.utf8), options: [.fragmentsAllowed]
        ) else { return }
        if value is NSNull {
            defaults.removeObject(forKey: MobileBrowserPreferences.searchEngineKey)
        } else if let raw = value as? String, let engine = MobileSearchEngine(rawValue: raw) {
            defaults.set(engine.rawValue, forKey: MobileBrowserPreferences.searchEngineKey)
        }
        // No WebKit navigation, runtime-tab update, focus or account switch.
    }
}
