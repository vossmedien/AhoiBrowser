// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_

#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"
#include "ahoi/browser/sync/browser_setting_consent.h"
#include "ahoi/browser/sync/browser_settings_sync_types.h"
#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/profile_shared_tab_types.h"
#include "ahoi/browser/sync/profile_sync_types.h"
#include "ahoi/browser/sync/profile_sync_ui_bridge.h"
#include "ahoi/browser/sync/remote_command_security.h"
#include "ahoi/browser/sync/sync_authorization.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "extensions/browser/extension_registry_observer.h"

class Profile;

namespace history {
class HistoryService;
}

namespace extensions {
class Extension;
class ExtensionRegistry;
enum class UnloadedExtensionReason;
}  // namespace extensions

namespace ahoi::sync {

class NativeSearchEngineSetting;

class ProfileSyncBackend;
class ProfileSyncServiceTest;
class NativeBookmarkSyncAdapter;

// Profile-scoped UI facade around the blocking local-first SQLite store. Disk
// work remains on one MayBlock sequence; views only receive immutable copies.
class ProfileSyncService final : public KeyedService,
                                 public history::HistoryServiceObserver,
                                 public extensions::ExtensionRegistryObserver {
 public:
  enum class BookmarkSyncIssue {
    kNone,
    kUnsupportedLocalData,
    kReconciliationFailed,
    kAuthorizationChanged,
  };

  enum class RemoteControlPrerequisite {
    kReady = 0,
    kSyncDisabled,
    kTransportUnavailable,
    kRecoveryPending,
    kApprovedDeviceRequired,
  };

  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;
    virtual void OnAhoiDeviceTabsChanged(
        const DeviceTabsSnapshot& snapshot) = 0;
    virtual void OnAhoiSyncStatusChanged(const SyncTransportStatus& status) {}
    virtual void OnAhoiSharedTabSyncStateChanged(
        const SharedTabSyncState& state) {}
  };

  explicit ProfileSyncService(Profile* profile);
  ProfileSyncService(const ProfileSyncService&) = delete;
  ProfileSyncService& operator=(const ProfileSyncService&) = delete;
  ~ProfileSyncService() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Browser/UI ownership remains outside this profile service. Multiple
  // windows may attach the same profile bridge; the last detach removes the
  // callback without affecting the local store or transport.
  void AttachUiBridge(ProfileSyncUiBridge* bridge);
  void DetachUiBridge(ProfileSyncUiBridge* bridge);

  bool initialized() const { return initialized_; }
  bool sync_enabled() const { return sync_enabled_; }
  const base::Uuid& local_device_id() const { return local_device_id_; }
  const DeviceTabsSnapshot& snapshot() const { return snapshot_; }
  const SyncTransportStatus& transport_status() const {
    return transport_status_;
  }
  const SharedTabSyncState& shared_tab_sync_state() const {
    return shared_tab_state_;
  }
  SharedTabProvenance GetSharedTabProvenance(
      const base::Uuid& tree_node_id) const;
  const std::vector<ExtensionInventoryRecord>& extension_inventory() const {
    return extension_inventory_;
  }
  const std::vector<DeveloperAssetRecord>& developer_assets() const {
    return developer_assets_;
  }

  // Register a window and request a fresh, profile-wide capture after a native
  // mutation. Each registered window must answer the same issued generation.
  void RequestSharedTabCapture(std::string window_key);
  void PublishSharedTabCapture(std::string window_key, LocalTabCapture capture);
  // Source compatibility during native integration only: requests capture and
  // NEVER treats an unqualified vector as authoritative current-format data.
  void PublishWindowTabs(std::string window_key,
                         std::vector<LocalTabState> tabs);
  void RemoveWindowTabs(const std::string& window_key);

  // Provider seam used by the macOS CloudKit transport. The merge, inbox and
  // token update occur atomically on the store sequence.
  void ApplyRemoteBatch(ProviderBatch batch);
  void Refresh();
  void SyncNow();
  void SetSyncEnabled(bool enabled);
  [[nodiscard]] bool SetHistoryRetentionDays(int days);
  bool SetRemoteControlEnabled(bool enabled);
  [[nodiscard]] bool ApproveRemoteControlDevice(const base::Uuid& device_id,
                                                std::string public_key_base64);
  void RevokeRemoteControlDevice(const base::Uuid& device_id);
  void ConfirmCloudKitAccountTransition(bool allow_local_upload);
  void ConfirmCloudKitZoneRecovery();
  bool remote_control_enabled() const;
  RemoteControlPrerequisite remote_control_prerequisite() const;
  bool can_pair_remote_control_device() const;
  int history_retention_days() const;
  std::vector<base::Uuid> approved_remote_control_devices() const;
  std::vector<std::string> permitted_setting_ids() const;
  std::vector<std::string> supported_setting_ids() const;
  // One deliberate "browser settings" category action in the native Sync UI.
  // Never invoke implicitly from startup, account discovery or global opt-in.
  [[nodiscard]] bool SetBrowserSettingsSyncEnabled(bool enabled);
  [[nodiscard]] bool SetPermittedSettingSyncEnabled(std::string setting_id,
                                                    bool enabled);
  [[nodiscard]] bool SetDeveloperAssetSyncEnabled(const base::Uuid& asset_id,
                                                  bool enabled);
  [[nodiscard]] bool PublishDeveloperAsset(DeveloperAssetRecord record);
  bool bookmark_sync_enabled() const;
  // Deliberate category action only; never called by shelf construction or
  // bookmark creation/navigation. Global opt-in remains a separate
  // prerequisite.
  bool SetBookmarkSyncEnabled(bool enabled);
  BookmarkSyncIssue bookmark_sync_issue() const { return bookmark_sync_issue_; }
  // Subscription lifetime is independent of the profile service's lifetime.
  base::CallbackListSubscription ObserveBookmarkSync(
      base::RepeatingClosure callback);

  // KeyedService:
  void Shutdown() override;

 private:
  friend class ProfileSyncServiceTest;
  friend class BookmarkSyncAuthorizationTest;

  void StartBackend();
  void StopBackend();
  SyncAuthorization StartProfileAuthorization();
  void RevokeProfileAuthorization();
  void ScheduleLocalPublish();
  void PublishCombinedLocalTabs();
  void CancelSharedTabCapture();
  void StopSharedTabs();
  void UpdateSharedTabNativeSupport();
  void OnSharedTabSupportUpdated(std::optional<SyncStateSnapshot> snapshot);
  void SetSharedTabState(SharedTabSyncState state);
  void OnSharedTabCaptureApplied(SharedTabCaptureResult result);
  void RefreshSharedTabProjection();
  void OnSharedTabProjection(uint64_t native_revision,
                             std::optional<SharedTabProjection> projection);
  void OnSharedTabProjectionPrepared(
      uint64_t native_revision,
      std::optional<PreparedSharedTabProjection> projection);
  void OnSharedTabProjectionApplied(uint64_t native_revision,
                                    PreparedSharedTabProjection projection,
                                    tab_tree::TabTreeStore::Result result);
  void FinishSharedTabProjection();
  void OnSyncCompleted(std::optional<SyncStateSnapshot> snapshot);
  void OnCloudKitRecoveryConfirmed(bool confirmed);
  void OnSyncEnabledPrefChanged();
  void OnHistoryRetentionPrefChanged();
  void OnRemoteControlPolicyPrefChanged();
  void OnBackendState(std::optional<SyncStateSnapshot> snapshot);
  void OnBackendSnapshot(std::optional<DeviceTabsSnapshot> snapshot);
  void OnTabTreeSnapshotChanged(const tab_tree::TabTreeSnapshot& snapshot);
  void ApplyDomainState(const SyncStateSnapshot& snapshot);
  void ClaimRemoteCommands();
  void OnRemoteCommandsClaimed(std::vector<RemoteCommandRecord> commands);
  void CompleteRemoteCommand(const RemoteCommandRecord& command,
                             bool executed,
                             std::string result_code);
  RemoteCommandPolicy CurrentRemoteCommandPolicy() const;
  void ApplyRemoteHistory(const std::vector<HistoryRecord>& records);
  void OnRemoteHistoryExpired();
  void InitializeProductSync();
  void ShutdownProductSync();
  void ApplyProductState(const SyncStateSnapshot& snapshot);
  void PublishCurrentAppearance();
  void InitializeBrowserSettings();
  void InitializeNativeSearchEngineSetting();
  bool SupportsBrowserSetting(std::string_view id) const;
  std::optional<std::string> ReadBrowserSetting(
      std::string_view id,
      bool explicit_reset = false) const;
  bool ApplyBrowserSetting(std::string_view id, std::string_view value_json);
  void OnNativeSearchEngineSettingChanged();
  void ResetBrowserSettingsWork();
  void UpdateBrowserSettingConsent();
  void OnBrowserSettingsConsentChanged();
  void RefreshBrowserSettings();
  void OnBrowserSettingsRead(
      uint64_t generation,
      std::optional<BrowserSettingsProjection> projection);
  void OnBrowserSettingIntentStored(uint64_t generation,
                                    std::string setting_id,
                                    std::string original_payload,
                                    std::optional<SyncStateSnapshot> snapshot);
  void PublishPermittedProductSetting(std::string setting_id,
                                      bool explicit_reset = false);
  void PublishExtensionInventory();
  void OnPermittedProductSettingChanged(std::string setting_id);
  void NotifyObservers();
  void InitializeBookmarkSync();
  void StopBookmarkSync();
  void SetBookmarkSyncIssue(BookmarkSyncIssue issue);
  void OnBookmarkSyncPrefChanged();
  void RefreshBookmarkProjection();
  void OnNativeBookmarkSnapshot(uint64_t generation,
                                NativeBookmarkSnapshot snapshot);
  void OnBookmarkProjection(uint64_t generation,
                            bool local_change,
                            std::optional<BookmarkSyncProjection> projection);
  void OnBookmarkProjectionAcknowledged(uint64_t generation,
                                        bool local_change,
                                        BookmarkSyncAuthorization authorization,
                                        bool success);

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;
  void OnShutdown(extensions::ExtensionRegistry* registry) override;

  // history::HistoryServiceObserver:
  void OnURLVisited(history::HistoryService* history_service,
                    const history::VisitedURLInfo& visited_url_info) override;
  void OnHistoryDeletions(history::HistoryService* history_service,
                          const history::DeletionInfo& deletion_info) override;
  void HistoryServiceBeingDeleted(
      history::HistoryService* history_service) override;

  const base::Uuid local_device_id_;
  const base::Uuid local_session_id_;
  HybridLogicalClock browser_settings_clock_;
  const scoped_refptr<base::SequencedTaskRunner> backend_task_runner_;
  std::shared_ptr<std::atomic<bool>> profile_scope_cancelled_ =
      std::make_shared<std::atomic<bool>>(true);
  base::SequenceBound<ProfileSyncBackend> backend_;
  raw_ptr<Profile> profile_ = nullptr;
  base::WeakPtr<ProfileSyncUiBridge> ui_bridge_;
  raw_ptr<history::HistoryService> history_service_ = nullptr;
  raw_ptr<extensions::ExtensionRegistry> extension_registry_ = nullptr;
  base::CallbackListSubscription tab_tree_subscription_;
  size_t ui_bridge_attachment_count_ = 0;
  std::map<std::string, std::vector<LocalTabState>> window_tabs_;
  std::set<std::string> registered_window_keys_;
  std::map<std::string, LocalTabCapture> pending_window_captures_;
  uint64_t shared_capture_generation_ = 0;
  bool shared_capture_submitted_ = false;
  std::shared_ptr<std::atomic<bool>> shared_capture_cancelled_ =
      std::make_shared<std::atomic<bool>>(true);
  SharedTabSyncState shared_tab_state_;
  std::map<base::Uuid, SharedTabProvenance> shared_tab_provenance_;
  uint64_t native_tree_revision_ = 0;
  std::shared_ptr<std::atomic<bool>> native_tree_cancelled_ =
      std::make_shared<std::atomic<bool>>(false);
  bool shared_projection_pending_ = false;
  bool shared_projection_requested_ = false;
  bool capture_after_projection_ = false;
  std::map<std::string, base::Uuid> tab_sync_ids_;
  std::map<base::Uuid, std::string> local_tab_keys_by_sync_id_;
  std::map<base::Uuid, SyncVersion> applied_history_versions_;
  std::map<base::Uuid, SyncVersion> applied_appearance_versions_;
  std::map<std::string, std::string> observed_user_settings_;
  std::unique_ptr<NativeSearchEngineSetting> native_search_engine_setting_;
  std::map<std::string, std::string> browser_setting_inflight_;
  const std::shared_ptr<BrowserSettingConsent> browser_setting_consent_ =
      std::make_shared<BrowserSettingConsent>();
  std::shared_ptr<std::atomic<bool>> browser_settings_cancelled_ =
      std::make_shared<std::atomic<bool>>(false);
  uint64_t browser_settings_generation_ = 0;
  bool browser_settings_read_pending_ = false;
  bool browser_settings_read_again_ = false;
  DeviceTabsSnapshot snapshot_;
  SyncTransportStatus transport_status_;
  std::vector<PermittedSettingRecord> permitted_settings_;
  std::vector<ExtensionInventoryRecord> extension_inventory_;
  std::vector<DeveloperAssetRecord> developer_assets_;
  std::unique_ptr<NativeBookmarkSyncAdapter> bookmark_adapter_;
  bool bookmarks_seeded_ = false;
  BookmarkSyncIssue bookmark_sync_issue_ = BookmarkSyncIssue::kNone;
  base::RepeatingClosureList bookmark_status_callbacks_;
  base::ObserverList<Observer> observers_;
  base::OneShotTimer publish_timer_;
  base::RepeatingTimer sync_timer_;
  base::CancelableTaskTracker history_task_tracker_;
  PrefChangeRegistrar sync_pref_registrar_;
  bool sync_enabled_ = false;
  bool initialized_ = false;
  bool backend_ready_ = false;
  bool applying_product_state_ = false;
  bool appearance_publish_pending_ = false;
  bool permitted_settings_seeded_ = false;
  bool extension_inventory_seeded_ = false;
  int pending_remote_history_deletions_ = 0;
  bool shutting_down_ = false;
  base::WeakPtrFactory<ProfileSyncService> bookmark_weak_ptr_factory_{this};
  base::WeakPtrFactory<ProfileSyncService> backend_weak_ptr_factory_{this};
  base::WeakPtrFactory<ProfileSyncService> weak_ptr_factory_{this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_
