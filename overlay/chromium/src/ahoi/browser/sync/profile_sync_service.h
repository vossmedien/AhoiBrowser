// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"
#include "ahoi/browser/sync/profile_sync_types.h"
#include "ahoi/browser/sync/profile_sync_ui_bridge.h"
#include "ahoi/browser/sync/remote_command_security.h"
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
  const std::vector<ExtensionInventoryRecord>& extension_inventory() const {
    return extension_inventory_;
  }
  const std::vector<DeveloperAssetRecord>& developer_assets() const {
    return developer_assets_;
  }

  // Each Browser window owns one key. Updating or removing a window publishes
  // the profile-wide union, so one window can never erase another's tabs.
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

  void StartBackend();
  void StopBackend();
  void ScheduleLocalPublish();
  void PublishCombinedLocalTabs();
  void OnLocalPublishComplete(std::optional<DeviceTabsSnapshot> snapshot);
  void OnSyncCompleted(std::optional<SyncStateSnapshot> snapshot);
  void OnCloudKitRecoveryConfirmed(bool confirmed);
  void OnSyncEnabledPrefChanged();
  void OnHistoryRetentionPrefChanged();
  void OnRemoteControlPolicyPrefChanged();
  void OnBackendState(std::optional<SyncStateSnapshot> snapshot);
  void OnBackendSnapshot(std::optional<DeviceTabsSnapshot> snapshot);
  void OnTabTreeSnapshotChanged(const tab_tree::TabTreeSnapshot& snapshot);
  void OnLocalTreeMerged(std::optional<SyncStateSnapshot> snapshot);
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
  void PublishPermittedProductSetting(std::string setting_id);
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
  const scoped_refptr<base::SequencedTaskRunner> backend_task_runner_;
  base::SequenceBound<ProfileSyncBackend> backend_;
  raw_ptr<Profile> profile_ = nullptr;
  base::WeakPtr<ProfileSyncUiBridge> ui_bridge_;
  raw_ptr<history::HistoryService> history_service_ = nullptr;
  raw_ptr<extensions::ExtensionRegistry> extension_registry_ = nullptr;
  base::CallbackListSubscription tab_tree_subscription_;
  size_t ui_bridge_attachment_count_ = 0;
  std::map<std::string, std::vector<LocalTabState>> window_tabs_;
  std::map<std::string, base::Uuid> tab_sync_ids_;
  std::map<base::Uuid, std::string> local_tab_keys_by_sync_id_;
  std::optional<tab_tree::TabTreeSnapshot> pending_tree_snapshot_;
  std::optional<tab_tree::TabTreeSnapshot> deferred_tree_snapshot_;
  std::map<base::Uuid, SyncVersion> applied_history_versions_;
  std::map<base::Uuid, SyncVersion> applied_appearance_versions_;
  std::map<base::Uuid, SyncVersion> applied_setting_versions_;
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
  bool initial_tree_merged_ = true;
  bool ui_tree_seeded_ = false;
  bool applying_synced_tree_ = false;
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
