// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_BACKEND_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/profile_sync_types.h"
#include "ahoi/browser/sync/remote_command_security.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"

namespace ahoi::sync {

class DeviceTabsService;
class SyncPayloadCryptor;
class SyncProvider;
class SyncPump;
class SyncStore;

// Blocking profile-local implementation owned by one SequenceBound task
// runner. No PrefService, HistoryService, SessionBridge or browser UI object
// crosses this boundary.
class ProfileSyncBackend {
 public:
  ProfileSyncBackend(base::FilePath database_path,
                     base::Uuid device_id,
                     base::Uuid session_id,
                     std::string device_name,
                     bool transport_enabled,
                     int history_retention_days);
  ProfileSyncBackend(const ProfileSyncBackend&) = delete;
  ProfileSyncBackend& operator=(const ProfileSyncBackend&) = delete;
  ~ProfileSyncBackend();

  std::optional<SyncStateSnapshot> Initialize();
  std::optional<DeviceTabsSnapshot> ReplaceLocalTabs(
      std::vector<LocalTabState> tabs);
  std::optional<SyncStateSnapshot> MergeLocalTabTree(
      tab_tree::TabTreeSnapshot snapshot,
      bool initial_merge);
  std::optional<SyncStateSnapshot> AddHistoryVisit(std::string url,
                                                   std::string title,
                                                   base::Time visit_time,
                                                   std::string transition,
                                                   int64_t visit_id);
  std::optional<SyncStateSnapshot> TombstoneHistory(
      std::vector<std::string> urls,
      base::Time begin,
      base::Time end,
      bool all_history);
  std::optional<SyncStateSnapshot> ApplyRemote(ProviderBatch batch);
  std::optional<SyncStateSnapshot> Refresh();

  std::vector<RemoteCommandRecord> ClaimRemoteCommands(
      RemoteCommandPolicy policy,
      base::Time now);
  bool CompleteRemoteCommand(base::Uuid command_id,
                             bool executed,
                             std::string result_code);
  bool ConfirmAccountTransition(bool allow_local_upload);
  bool ConfirmZoneRecovery();
  std::optional<SyncStateSnapshot> SetTransportEnabled(bool enabled);
  std::optional<SyncStateSnapshot> SetHistoryRetentionDays(int days);
  std::optional<SyncStateSnapshot> UpsertAppearance(AppearanceRecord record);
  std::optional<SyncStateSnapshot> UpsertPermittedSetting(
      PermittedSettingRecord record);
  std::optional<SyncStateSnapshot> ReplaceLocalExtensionInventory(
      std::vector<ExtensionInventoryRecord> records);
  std::optional<SyncStateSnapshot> UpsertDeveloperAsset(
      DeveloperAssetRecord record);

  void SyncNow(
      base::OnceCallback<void(std::optional<SyncStateSnapshot>)> callback);
  void CloseSession();

 private:
  template <typename Record>
  bool Put(const Record& record);
  template <typename Record>
  bool PutDomainRecordIfChanged(Record record, base::Time mutation_time);

  void TouchSession();
  void InitializeProviderIfAvailable();
  bool EnforceRetention(base::Time now);
  std::optional<SyncStateSnapshot> CurrentState();
  void OnSyncFinished(
      base::OnceCallback<void(std::optional<SyncStateSnapshot>)> callback,
      bool success,
      std::string safe_error);

  const base::FilePath database_path_;
  const base::Uuid device_id_;
  const base::Uuid session_id_;
  const std::string device_name_;
  bool transport_enabled_ = false;
  int history_retention_days_ = 90;
  base::Time last_retention_run_;
  HybridLogicalClock clock_;
  std::unique_ptr<SyncStore> store_;
  std::unique_ptr<DeviceTabsService> tabs_service_;
  DeviceSessionRecord session_record_;
  std::map<std::string, RemoteTabRecord> live_tabs_;
  std::unique_ptr<SyncProvider> provider_;
  std::unique_ptr<SyncPump> pump_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_BACKEND_H_
