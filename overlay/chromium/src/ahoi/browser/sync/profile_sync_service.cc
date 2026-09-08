// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service.h"

#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/sync/browser_setting_catalog.h"
#include "ahoi/browser/sync/history_sync_filter.h"
#include "ahoi/browser/sync/native_bookmark_sync_adapter.h"
#include "ahoi/browser/sync/native_extension_setup_controller.h"
#include "ahoi/browser/sync/native_extension_storage_controller.h"
#include "ahoi/browser/sync/native_search_engine_setting.h"
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
      browser_settings_clock_(local_device_id_.AsLowercaseString()),
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
      base::BindRepeating(&ProfileSyncService::OnRemoteControlPolicyPrefChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      kApprovedRemoteCommandKeysPref,
      base::BindRepeating(&ProfileSyncService::OnRemoteControlPolicyPrefChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  InitializeProductSync();
  InitializeBookmarkSync();
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
  extension_inventory_seeded_ = false;
  appearance_publish_pending_ = false;
  permitted_settings_seeded_ = false;
  InitializeNativeSearchEngineSetting();
  InitializeExtensionSetup();
  UpdateBrowserSettingConsent();
  if (profile_->GetPrefs()->GetString(kDeviceIdPref) !=
      local_device_id_.AsLowercaseString()) {
    profile_->GetPrefs()->SetString(kDeviceIdPref,
                                    local_device_id_.AsLowercaseString());
  }
  backend_.emplace(
      backend_task_runner_,
      profile_->GetPath()
          .AppendASCII("Ahoi Sync")
          .AppendASCII("sync-format3.sqlite"),
      local_device_id_, local_session_id_, DeviceDisplayName(*profile_),
      /*transport_enabled=*/true,
      profile_->GetPrefs()->GetInteger(kHistoryRetentionDaysPref),
      bookmark_sync_enabled(), StartProfileAuthorization(),
      base::BindRepeating(
          [](std::shared_ptr<BrowserSettingConsent> consent,
             const base::Uuid& id) { return consent->Capture(id); },
          browser_setting_consent_));
  backend_.AsyncCall(&ProfileSyncBackend::Initialize)
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));
  UpdateSharedTabNativeSupport();

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
  RevokeProfileAuthorization();
  native_search_engine_setting_.reset();
  observed_user_settings_.erase(kBrowserSearchEngineSettingId);
  browser_setting_consent_->SetAllowed({});
  ResetBrowserSettingsWork();
  StopSharedTabs();
  StopBookmarkSync();
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
  applying_product_state_ = false;
  appearance_publish_pending_ = false;
  permitted_settings_seeded_ = false;
  extension_inventory_seeded_ = false;
  pending_remote_history_deletions_ = 0;
  window_tabs_.clear();
  tab_sync_ids_.clear();
  local_tab_keys_by_sync_id_.clear();
  applied_history_versions_.clear();
  applied_appearance_versions_.clear();
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
  observer->OnAhoiSharedTabSyncStateChanged(shared_tab_state_);
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
  ResetBrowserSettingsWork();
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
  UpdateSharedTabNativeSupport();
  InitializeExtensionSetup();
  RefreshBrowserSettings();
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
  ResetBrowserSettingsWork();
  ui_bridge_.reset();
  ++native_tree_revision_;
  native_tree_cancelled_->store(true, std::memory_order_release);
  shared_projection_requested_ = false;
  CancelSharedTabCapture();
  SetSharedTabState({.issue = SharedTabSyncIssue::kNativeNotReady});
  UpdateSharedTabNativeSupport();
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

void ProfileSyncService::RetrySyncKeySetup() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::RetrySyncKeySetup)
      .Then(base::BindOnce(&ProfileSyncService::OnCloudKitRecoveryConfirmed,
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
  if (!allow_local_upload) {
    // Preserve local values/intents, but do not reuse the previous account's
    // per-setting approval after the user explicitly refused local upload.
    profile_->GetPrefs()->SetList(kPermittedSettingIdsPref, base::ListValue());
    profile_->GetPrefs()->SetBoolean(kExtensionSetupSyncEnabledPref, false);
    profile_->GetPrefs()->SetBoolean(kExtensionSettingsSyncEnabledPref, false);
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
  RevokeProfileAuthorization();
  StopSharedTabs();
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
  StopBookmarkSync();
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
  SetSharedTabState(state->shared_tabs);
  const bool first_initialization = !initialized_;
  initialized_ = true;
  const bool transport_changed = transport_status_ != state->transport;
  transport_status_ = state->transport;
  if ((transport_status_.account_transition_pending ||
       transport_status_.bookmark_consent_revoked) &&
      bookmark_sync_enabled()) {
    SetBookmarkSyncEnabled(false);
  }
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
  std::ignore = snapshot;  // Export the current tree and receipt atomically.
  ++native_tree_revision_;
  native_tree_cancelled_->store(true, std::memory_order_release);
  native_tree_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  CancelSharedTabCapture();
  capture_after_projection_ = true;
  RefreshSharedTabProjection();
}

void ProfileSyncService::ApplyDomainState(const SyncStateSnapshot& state) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  RefreshSharedTabProjection();
  ApplyRemoteHistory(state.history);
  ApplyProductState(state);
  RefreshBookmarkProjection();
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
    // ClaimRemoteCommands() runs on the blocking backend sequence with a
    // by-value policy snapshot. Re-read and cryptographically validate the
    // current UI-sequence authority immediately before dispatch so a disable,
    // transport/recovery transition, or sender-key revoke/rotation that occurs
    // while the claim is in flight always wins. The backend remains the sole
    // owner of queued -> delivered and replay-consumption semantics.
    const RemoteCommandValidationFailure authorization =
        RevalidateDeliveredRemoteCommandForExecution(
            command, local_device_id_, CurrentRemoteCommandPolicy(),
            base::Time::Now());
    if (authorization != RemoteCommandValidationFailure::kNone) {
      CompleteRemoteCommand(command, false,
                            SafeRemoteCommandFailureCode(authorization));
      continue;
    }
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

void ProfileSyncService::NotifyObservers() {
  bookmark_status_callbacks_.Notify();
  for (Observer& observer : observers_) {
    observer.OnAhoiDeviceTabsChanged(snapshot_);
    observer.OnAhoiSyncStatusChanged(transport_status_);
  }
}

}  // namespace ahoi::sync
