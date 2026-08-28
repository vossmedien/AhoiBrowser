// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service.h"

#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/sync/history_sync_filter.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "net/base/network_interfaces.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

constexpr base::TimeDelta kLocalPublishDelay = base::Milliseconds(80);
constexpr base::TimeDelta kAutomaticSyncInterval = base::Minutes(5);

base::Uuid LoadOrGenerateDeviceId(Profile& profile, bool persist_if_created) {
  PrefService* const prefs = profile.GetPrefs();
  base::Uuid id = base::Uuid::ParseLowercase(prefs->GetString(kDeviceIdPref));
  if (!id.is_valid()) {
    id = base::Uuid::GenerateRandomV4();
    if (persist_if_created) {
      prefs->SetString(kDeviceIdPref, id.AsLowercaseString());
    }
  }
  return id;
}

std::string DeviceDisplayName(Profile& profile) {
  const std::string configured =
      profile.GetPrefs()->GetString(kDeviceDisplayNamePref);
  return configured.empty() ? net::GetHostName() : configured;
}

}  // namespace

ProfileSyncService::ProfileSyncService(Profile* profile)
    : local_device_id_(LoadOrGenerateDeviceId(
          *profile,
          profile->GetPrefs()->GetBoolean(kSyncEnabledPref))),
      local_session_id_(base::Uuid::GenerateRandomV4()),
      backend_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      profile_(profile),
      history_service_(HistoryServiceFactory::GetForProfile(
          profile,
          ServiceAccessType::EXPLICIT_ACCESS)),
      sync_enabled_(profile->GetPrefs()->GetBoolean(kSyncEnabledPref)) {
  sync_pref_registrar_.Init(profile->GetPrefs());
  sync_pref_registrar_.Add(
      kSyncEnabledPref,
      base::BindRepeating(&ProfileSyncService::OnSyncEnabledPrefChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      kHistoryRetentionDaysPref,
      base::BindRepeating(&ProfileSyncService::OnHistoryRetentionPrefChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      kRemoteControlEnabledPref,
      base::BindRepeating(
          &ProfileSyncService::OnRemoteControlPolicyPrefChanged,
          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      kApprovedRemoteCommandKeysPref,
      base::BindRepeating(
          &ProfileSyncService::OnRemoteControlPolicyPrefChanged,
          weak_ptr_factory_.GetWeakPtr()));
  InitializeProductSync();
  if (history_service_) {
    history_service_->AddObserver(this);
  }
  if (sync_enabled_) {
    StartBackend();
  }
}

ProfileSyncService::~ProfileSyncService() = default;

void ProfileSyncService::StartBackend() {
  if (!profile_ || shutting_down_ || !sync_enabled_ || !backend_.is_null()) {
    return;
  }

  backend_weak_ptr_factory_.InvalidateWeakPtrs();
  backend_ready_ = false;
  initialized_ = false;
  initial_tree_merged_ = true;
  ui_tree_seeded_ = false;
  extension_inventory_seeded_ = false;
  appearance_publish_pending_ = false;
  permitted_settings_seeded_ = false;
  if (profile_->GetPrefs()->GetString(kDeviceIdPref) !=
      local_device_id_.AsLowercaseString()) {
    profile_->GetPrefs()->SetString(kDeviceIdPref,
                                    local_device_id_.AsLowercaseString());
  }
  backend_.emplace(
      backend_task_runner_,
      profile_->GetPath().AppendASCII("Ahoi Sync").AppendASCII("sync.sqlite"),
      local_device_id_, local_session_id_, DeviceDisplayName(*profile_),
      /*transport_enabled=*/true,
      profile_->GetPrefs()->GetInteger(kHistoryRetentionDaysPref));
  backend_.AsyncCall(&ProfileSyncBackend::Initialize)
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));

  // Capture the current local tree as part of the explicit opt-in even if no
  // subsequent sidebar mutation occurs. It stays pending until SQLite is
  // ready and does not depend on CloudKit provider availability.
  if (ui_bridge_) {
    tab_tree::TabTreeSnapshot snapshot;
    if (ui_bridge_->ExportTabTreeSnapshot(&snapshot)) {
      OnTabTreeSnapshotChanged(snapshot);
    }
    ui_bridge_->RequestLocalTabCapture();
  }
}

void ProfileSyncService::StopBackend() {
  publish_timer_.Stop();
  sync_timer_.Stop();
  history_task_tracker_.TryCancelAll();
  backend_weak_ptr_factory_.InvalidateWeakPtrs();
  if (!backend_.is_null()) {
    backend_.AsyncCall(&ProfileSyncBackend::SuspendWithoutPersisting);
    backend_.Reset();
  }

  backend_ready_ = false;
  initialized_ = false;
  initial_tree_merged_ = true;
  ui_tree_seeded_ = false;
  applying_synced_tree_ = false;
  applying_product_state_ = false;
  appearance_publish_pending_ = false;
  permitted_settings_seeded_ = false;
  extension_inventory_seeded_ = false;
  pending_remote_history_deletions_ = 0;
  pending_tree_snapshot_.reset();
  deferred_tree_snapshot_.reset();
  window_tabs_.clear();
  tab_sync_ids_.clear();
  local_tab_keys_by_sync_id_.clear();
  applied_history_versions_.clear();
  applied_appearance_versions_.clear();
  applied_setting_versions_.clear();
  snapshot_ = {};
  transport_status_ = {};
  permitted_settings_.clear();
  extension_inventory_.clear();
  developer_assets_.clear();
  NotifyObservers();
}

void ProfileSyncService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
  observer->OnAhoiDeviceTabsChanged(snapshot_);
  observer->OnAhoiSyncStatusChanged(transport_status_);
}

void ProfileSyncService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ProfileSyncService::AttachUiBridge(ProfileSyncUiBridge* bridge) {
  if (shutting_down_ || !bridge) {
    return;
  }
  if (ui_bridge_) {
    CHECK_EQ(ui_bridge_.get(), bridge);
    ++ui_bridge_attachment_count_;
    return;
  }
  tab_tree_subscription_ = {};
  ui_bridge_attachment_count_ = 0;
  ui_bridge_ = bridge->GetWeakPtrForSync();
  if (!ui_bridge_) {
    return;
  }
  ui_bridge_attachment_count_ = 1;
  tab_tree_subscription_ = bridge->AddTabTreeSnapshotChangedCallback(
      base::BindRepeating(&ProfileSyncService::OnTabTreeSnapshotChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  tab_tree::TabTreeSnapshot snapshot;
  if (bridge->ExportTabTreeSnapshot(&snapshot)) {
    OnTabTreeSnapshotChanged(snapshot);
  }
  ClaimRemoteCommands();
}

void ProfileSyncService::DetachUiBridge(ProfileSyncUiBridge* bridge) {
  if (shutting_down_ || bridge != ui_bridge_.get() ||
      ui_bridge_attachment_count_ == 0) {
    return;
  }
  if (--ui_bridge_attachment_count_ > 0) {
    return;
  }
  tab_tree_subscription_ = {};
  ui_bridge_.reset();
  deferred_tree_snapshot_.reset();
}

void ProfileSyncService::PublishWindowTabs(std::string window_key,
                                           std::vector<LocalTabState> tabs) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      window_key.empty()) {
    return;
  }
  for (LocalTabState& tab : tabs) {
    auto [it, inserted] = tab_sync_ids_.try_emplace(
        tab.stable_key, base::Uuid::GenerateRandomV4());
    tab.sync_id = it->second;
  }
  window_tabs_.insert_or_assign(std::move(window_key), std::move(tabs));
  ScheduleLocalPublish();
}

void ProfileSyncService::RemoveWindowTabs(const std::string& window_key) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      window_tabs_.erase(window_key) == 0u) {
    return;
  }
  if (window_tabs_.empty()) {
    publish_timer_.Stop();
    PublishCombinedLocalTabs();
    return;
  }
  ScheduleLocalPublish();
}

void ProfileSyncService::ApplyRemoteBatch(ProviderBatch batch) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::ApplyRemote)
      .WithArgs(std::move(batch))
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::Refresh() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::Refresh)
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::SyncNow() {
  if (shutting_down_ || !initialized_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::SyncNow)
      .WithArgs(base::BindPostTaskToCurrentDefault(
          base::BindOnce(&ProfileSyncService::OnSyncCompleted,
                         backend_weak_ptr_factory_.GetWeakPtr())));
}

void ProfileSyncService::SetSyncEnabled(bool enabled) {
  if (!profile_ || shutting_down_) {
    return;
  }
  profile_->GetPrefs()->SetBoolean(kSyncEnabledPref, enabled);
}

bool ProfileSyncService::SetHistoryRetentionDays(int days) {
  if (!profile_ || shutting_down_ || !IsValidHistoryRetentionDays(days)) {
    return false;
  }
  profile_->GetPrefs()->SetInteger(kHistoryRetentionDaysPref, days);
  return true;
}

bool ProfileSyncService::SetRemoteControlEnabled(bool enabled) {
  if (!profile_ || shutting_down_) {
    return false;
  }
  if (!enabled) {
    profile_->GetPrefs()->SetBoolean(kRemoteControlEnabledPref, false);
    return true;
  }
  if (remote_control_prerequisite() != RemoteControlPrerequisite::kReady) {
    return false;
  }
  profile_->GetPrefs()->SetBoolean(kRemoteControlEnabledPref, true);
  SyncNow();
  return true;
}

bool ProfileSyncService::ApproveRemoteControlDevice(
    const base::Uuid& device_id,
    std::string public_key_base64) {
  if (!can_pair_remote_control_device() || !device_id.is_valid() ||
      !IsValidRemoteControlPublicKeyBase64(public_key_base64)) {
    return false;
  }
  base::DictValue keys =
      profile_->GetPrefs()->GetDict(kApprovedRemoteCommandKeysPref).Clone();
  keys.Set(device_id.AsLowercaseString(), std::move(public_key_base64));
  profile_->GetPrefs()->SetDict(kApprovedRemoteCommandKeysPref,
                                std::move(keys));
  return true;
}

void ProfileSyncService::RevokeRemoteControlDevice(
    const base::Uuid& device_id) {
  // Revocation is a local fail-closed operation and therefore remains
  // available during an outage or after Sync is disabled.
  if (!profile_ || shutting_down_ || !device_id.is_valid()) {
    return;
  }
  base::DictValue keys =
      profile_->GetPrefs()->GetDict(kApprovedRemoteCommandKeysPref).Clone();
  keys.Remove(device_id.AsLowercaseString());
  profile_->GetPrefs()->SetDict(kApprovedRemoteCommandKeysPref,
                                std::move(keys));
}

void ProfileSyncService::ConfirmCloudKitAccountTransition(
    bool allow_local_upload) {
  if (shutting_down_ || !initialized_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::ConfirmAccountTransition)
      .WithArgs(allow_local_upload)
      .Then(base::BindOnce(&ProfileSyncService::OnCloudKitRecoveryConfirmed,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::ConfirmCloudKitZoneRecovery() {
  if (shutting_down_ || !initialized_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::ConfirmZoneRecovery)
      .Then(base::BindOnce(&ProfileSyncService::OnCloudKitRecoveryConfirmed,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::Shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  publish_timer_.Stop();
  sync_timer_.Stop();
  history_task_tracker_.TryCancelAll();
  sync_pref_registrar_.RemoveAll();
  tab_tree_subscription_ = {};
  if (history_service_) {
    history_service_->RemoveObserver(this);
  }
  history_service_ = nullptr;
  ShutdownProductSync();
  ui_bridge_.reset();
  ui_bridge_attachment_count_ = 0;
  profile_ = nullptr;
  backend_weak_ptr_factory_.InvalidateWeakPtrs();
  weak_ptr_factory_.InvalidateWeakPtrs();
  observers_.Clear();
  window_tabs_.clear();
  if (!backend_.is_null()) {
    backend_.AsyncCall(&ProfileSyncBackend::CloseSession);
    backend_.Reset();
  }
}

void ProfileSyncService::ScheduleLocalPublish() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  publish_timer_.Start(FROM_HERE, kLocalPublishDelay, this,
                       &ProfileSyncService::PublishCombinedLocalTabs);
}

void ProfileSyncService::PublishCombinedLocalTabs() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  std::vector<LocalTabState> combined;
  std::set<std::string> live_keys;
  local_tab_keys_by_sync_id_.clear();
  for (const auto& [window, tabs] : window_tabs_) {
    for (const LocalTabState& tab : tabs) {
      combined.push_back(tab);
      live_keys.insert(tab.stable_key);
      local_tab_keys_by_sync_id_[tab.sync_id] = tab.stable_key;
    }
  }
  for (auto it = tab_sync_ids_.begin(); it != tab_sync_ids_.end();) {
    it =
        live_keys.contains(it->first) ? std::next(it) : tab_sync_ids_.erase(it);
  }
  backend_.AsyncCall(&ProfileSyncBackend::ReplaceLocalTabs)
      .WithArgs(std::move(combined))
      .Then(base::BindOnce(&ProfileSyncService::OnLocalPublishComplete,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnLocalPublishComplete(
    std::optional<DeviceTabsSnapshot> snapshot) {
  if (!sync_enabled_ || backend_.is_null()) {
    return;
  }
  OnBackendSnapshot(std::move(snapshot));
  SyncNow();
}

void ProfileSyncService::OnSyncCompleted(
    std::optional<SyncStateSnapshot> snapshot) {
  if (!sync_enabled_ || backend_.is_null()) {
    return;
  }
  OnBackendState(std::move(snapshot));
}

void ProfileSyncService::OnCloudKitRecoveryConfirmed(bool confirmed) {
  if (confirmed) {
    SyncNow();
  }
}

void ProfileSyncService::OnSyncEnabledPrefChanged() {
  if (!profile_ || shutting_down_) {
    return;
  }
  const bool enabled = profile_->GetPrefs()->GetBoolean(kSyncEnabledPref);
  if (enabled == sync_enabled_) {
    return;
  }
  sync_enabled_ = enabled;
  if (enabled) {
    StartBackend();
  } else {
    // Opting out of the local sync authority also revokes receive mode. The
    // approved local keys remain so the user can deliberately re-enable after
    // transport recovery without repeating pairing.
    profile_->GetPrefs()->SetBoolean(kRemoteControlEnabledPref, false);
    StopBackend();
  }
}

void ProfileSyncService::OnHistoryRetentionPrefChanged() {
  if (!profile_ || shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  const int days = profile_->GetPrefs()->GetInteger(kHistoryRetentionDaysPref);
  if (!IsValidHistoryRetentionDays(days)) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::SetHistoryRetentionDays)
      .WithArgs(days)
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnRemoteControlPolicyPrefChanged() {
  if (!profile_ || shutting_down_) {
    return;
  }
  PrefService* const prefs = profile_->GetPrefs();
  if (prefs->GetBoolean(kRemoteControlEnabledPref) &&
      remote_control_prerequisite() != RemoteControlPrerequisite::kReady) {
    // Preferences can be written by Settings or restored from an older
    // profile. Never retain an apparently enabled receive policy unless the
    // local database, CloudKit transport and a verified sender key all exist.
    prefs->SetBoolean(kRemoteControlEnabledPref, false);
    return;
  }
  NotifyObservers();
}

void ProfileSyncService::OnBackendState(
    std::optional<SyncStateSnapshot> state) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !state) {
    return;
  }
  backend_ready_ = true;
  if (!initial_tree_merged_) {
    if (pending_tree_snapshot_) {
      tab_tree::TabTreeSnapshot tree = std::move(*pending_tree_snapshot_);
      pending_tree_snapshot_.reset();
      backend_.AsyncCall(&ProfileSyncBackend::MergeLocalTabTree)
          .WithArgs(std::move(tree), true)
          .Then(base::BindOnce(&ProfileSyncService::OnLocalTreeMerged,
                               backend_weak_ptr_factory_.GetWeakPtr()));
    }
    return;
  }

  const bool first_initialization = !initialized_;
  initialized_ = true;
  const bool transport_changed = transport_status_ != state->transport;
  transport_status_ = state->transport;
  if (profile_->GetPrefs()->GetBoolean(kRemoteControlEnabledPref) &&
      remote_control_prerequisite() != RemoteControlPrerequisite::kReady) {
    profile_->GetPrefs()->SetBoolean(kRemoteControlEnabledPref, false);
  }
  ApplyDomainState(*state);
  OnBackendSnapshot(std::move(state->device_tabs));
  if (transport_changed) {
    NotifyObservers();
  }
  if (ui_bridge_) {
    ClaimRemoteCommands();
  }
  if (sync_enabled_ && !sync_timer_.IsRunning()) {
    sync_timer_.Start(FROM_HERE, kAutomaticSyncInterval, this,
                      &ProfileSyncService::SyncNow);
  }
  if (first_initialization && sync_enabled_) {
    SyncNow();
  }
}

void ProfileSyncService::OnBackendSnapshot(
    std::optional<DeviceTabsSnapshot> snapshot) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !snapshot ||
      *snapshot == snapshot_) {
    return;
  }
  snapshot_ = std::move(*snapshot);
  NotifyObservers();
}

void ProfileSyncService::OnTabTreeSnapshotChanged(
    const tab_tree::TabTreeSnapshot& snapshot) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !ui_bridge_) {
    return;
  }
  if (applying_synced_tree_) {
    deferred_tree_snapshot_ = snapshot;
    return;
  }
  const bool initial = !ui_tree_seeded_;
  if (initial) {
    initial_tree_merged_ = false;
  }
  if (!backend_ready_) {
    pending_tree_snapshot_ = snapshot;
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::MergeLocalTabTree)
      .WithArgs(snapshot, initial)
      .Then(base::BindOnce(&ProfileSyncService::OnLocalTreeMerged,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnLocalTreeMerged(
    std::optional<SyncStateSnapshot> snapshot) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !snapshot) {
    return;
  }
  const bool was_initialized = initialized_;
  const bool was_initial = !ui_tree_seeded_;
  initial_tree_merged_ = true;
  ui_tree_seeded_ = true;
  OnBackendState(std::move(snapshot));
  if (!was_initial || was_initialized) {
    SyncNow();
  }
}

void ProfileSyncService::ApplyDomainState(const SyncStateSnapshot& state) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  if (ui_bridge_) {
    tab_tree::TabTreeSnapshot local;
    if (ui_bridge_->ExportTabTreeSnapshot(&local)) {
      std::optional<tab_tree::TabTreeSnapshot> reconciled =
          ReconcileTabTreeRecords(local, state.workspaces, state.tree_nodes);
      if (reconciled) {
        applying_synced_tree_ = true;
        deferred_tree_snapshot_.reset();
        std::ignore =
            ui_bridge_->ApplySyncedTabTreeSnapshot(std::move(*reconciled));
        applying_synced_tree_ = false;
        if (deferred_tree_snapshot_) {
          tab_tree::TabTreeSnapshot deferred =
              std::move(*deferred_tree_snapshot_);
          deferred_tree_snapshot_.reset();
          OnTabTreeSnapshotChanged(deferred);
        }
      }
    }
  }
  ApplyRemoteHistory(state.history);
  ApplyProductState(state);
}

RemoteCommandPolicy ProfileSyncService::CurrentRemoteCommandPolicy() const {
  RemoteCommandPolicy policy;
  if (!profile_ || shutting_down_ ||
      remote_control_prerequisite() != RemoteControlPrerequisite::kReady) {
    return policy;
  }
  policy.enabled = remote_control_enabled();
  for (const auto [device, value] :
       profile_->GetPrefs()->GetDict(kApprovedRemoteCommandKeysPref)) {
    const base::Uuid id = base::Uuid::ParseLowercase(device);
    if (id.is_valid() && value.is_string() &&
        IsValidRemoteControlPublicKeyBase64(value.GetString())) {
      policy.approved_public_keys_base64.emplace(id, value.GetString());
    }
  }
  return policy;
}

void ProfileSyncService::ClaimRemoteCommands() {
  if (shutting_down_ || !sync_enabled_ || !initialized_ || backend_.is_null() ||
      !transport_status_.provider_available) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::ClaimRemoteCommands)
      .WithArgs(CurrentRemoteCommandPolicy(), base::Time::Now())
      .Then(base::BindOnce(&ProfileSyncService::OnRemoteCommandsClaimed,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnRemoteCommandsClaimed(
    std::vector<RemoteCommandRecord> commands) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  for (const RemoteCommandRecord& command : commands) {
    bool executed = false;
    if (ui_bridge_) {
      switch (command.kind) {
        case RemoteCommandKind::kOpen:
          executed = ui_bridge_->OpenNormalTabFromRemoteCommand(
              GURL(command.url), command.workspace_id);
          break;
        case RemoteCommandKind::kFocus:
        case RemoteCommandKind::kClose: {
          const auto key =
              command.tab_id ? local_tab_keys_by_sync_id_.find(*command.tab_id)
                             : local_tab_keys_by_sync_id_.end();
          if (key != local_tab_keys_by_sync_id_.end()) {
            executed =
                command.kind == RemoteCommandKind::kFocus
                    ? ui_bridge_->FocusNormalTabFromRemoteCommand(key->second)
                    : ui_bridge_->CloseNormalTabFromRemoteCommand(key->second);
          }
          break;
        }
      }
    }
    CompleteRemoteCommand(command, executed,
                          executed ? "executed" : "not_found");
  }
  if (!commands.empty()) {
    SyncNow();
  }
}

void ProfileSyncService::CompleteRemoteCommand(
    const RemoteCommandRecord& command,
    bool executed,
    std::string result_code) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::CompleteRemoteCommand)
      .WithArgs(command.id, executed, std::move(result_code));
}

void ProfileSyncService::ApplyRemoteHistory(
    const std::vector<HistoryRecord>& records) {
  if (!sync_enabled_ || backend_.is_null() || !history_service_) {
    return;
  }
  for (const HistoryRecord& record : records) {
    auto applied = applied_history_versions_.find(record.id);
    if (applied != applied_history_versions_.end() &&
        applied->second >= record.version) {
      continue;
    }
    applied_history_versions_[record.id] = record.version;
    if (record.version.stamp.device_tiebreak ==
        local_device_id_.AsLowercaseString()) {
      continue;
    }
    const GURL url(record.url);
    if (!IsSafeHistoryUrlForSync(url)) {
      continue;
    }
    if (!record.tombstone) {
      history_service_->AddPageWithDetails(url, base::UTF8ToUTF16(record.title),
                                           1, 0, record.last_visit, false,
                                           history::SOURCE_SYNCED);
      continue;
    }
    ++pending_remote_history_deletions_;
    history_service_->ExpireHistoryBetween(
        {url}, std::nullopt, record.last_visit,
        record.last_visit + base::Microseconds(1), false,
        base::BindOnce(&ProfileSyncService::OnRemoteHistoryExpired,
                       weak_ptr_factory_.GetWeakPtr()),
        &history_task_tracker_);
  }
}

void ProfileSyncService::OnRemoteHistoryExpired() {
  if (pending_remote_history_deletions_ > 0) {
    --pending_remote_history_deletions_;
  }
}

void ProfileSyncService::OnURLVisited(history::HistoryService* history_service,
                                      const history::VisitedURLInfo& info) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      history_service != history_service_ ||
      !ShouldSyncHistoryVisit({
          .url = info.url_row.url(),
          .hidden = info.url_row.hidden(),
          .response_is_404 = info.response_code_category ==
                             history::VisitResponseCodeCategory::k404,
          .source_is_browsed =
              !info.visit_row.source ||
              *info.visit_row.source == history::SOURCE_BROWSED,
      })) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::AddHistoryVisit)
      .WithArgs(info.url_row.url().spec(),
                base::UTF16ToUTF8(info.url_row.title()),
                info.visit_row.visit_time,
                std::string(ui::PageTransitionGetCoreTransitionString(
                    info.visit_row.transition)),
                info.visit_row.visit_id)
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> service,
             std::optional<SyncStateSnapshot> state) {
            if (!service) {
              return;
            }
            service->OnBackendState(std::move(state));
            service->SyncNow();
          },
          backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnHistoryDeletions(
    history::HistoryService* history_service,
    const history::DeletionInfo& info) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      history_service != history_service_ ||
      pending_remote_history_deletions_ > 0 || info.is_from_expiration() ||
      info.deletion_reason() ==
          history::DeletionInfo::Reason::kDeleteAllForeignVisits) {
    return;
  }
  std::set<std::string> urls;
  if (!info.IsAllHistory()) {
    for (const history::URLRow& row : info.deleted_rows()) {
      if (IsSafeHistoryUrlForSync(row.url())) {
        urls.insert(row.url().spec());
      }
    }
    if (info.restrict_urls()) {
      for (const GURL& url : *info.restrict_urls()) {
        if (IsSafeHistoryUrlForSync(url)) {
          urls.insert(url.spec());
        }
      }
    }
  }
  const bool valid_range = info.time_range().IsValid();
  if (!info.IsAllHistory() && urls.empty() && !valid_range) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::TombstoneHistory)
      .WithArgs(std::vector<std::string>(urls.begin(), urls.end()),
                valid_range ? info.time_range().begin() : base::Time(),
                valid_range ? info.time_range().end() : base::Time(),
                info.IsAllHistory())
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> service,
             std::optional<SyncStateSnapshot> state) {
            if (!service) {
              return;
            }
            service->OnBackendState(std::move(state));
            service->SyncNow();
          },
          backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::HistoryServiceBeingDeleted(
    history::HistoryService* history_service) {
  if (history_service == history_service_) {
    history_service_ = nullptr;
  }
}

void ProfileSyncService::NotifyObservers() {
  for (Observer& observer : observers_) {
    observer.OnAhoiDeviceTabsChanged(snapshot_);
    observer.OnAhoiSyncStatusChanged(transport_status_);
  }
}

}  // namespace ahoi::sync
