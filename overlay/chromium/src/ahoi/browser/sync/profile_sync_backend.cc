// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_backend.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/device_tabs_service.h"
#include "ahoi/browser/sync/remote_command_security.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"
#include "ahoi/browser/sync/keychain_sync_key_mac.h"
#endif
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "crypto/sha2.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

bool IsShareableTab(const LocalTabState& tab) {
  const GURL url(tab.url);
  return !tab.stable_key.empty() && tab.sync_id.is_valid() && url.is_valid() &&
         url.SchemeIsHTTPOrHTTPS() && !url.host().empty() &&
         !url.has_username() && !url.has_password();
}

base::Uuid StableHistoryVisitId(const base::Uuid& device_id,
                                int64_t visit_id,
                                const std::string& url,
                                base::Time visit_time,
                                const std::string& transition) {
  std::string material = device_id.AsLowercaseString();
  material.push_back(':');
  if (visit_id > 0) {
    material.append(base::NumberToString(visit_id));
  } else {
    material.append(url);
    material.push_back(':');
    material.append(base::NumberToString(
        visit_time.ToDeltaSinceWindowsEpoch().InMicroseconds()));
    material.push_back(':');
    material.append(transition);
  }
  std::string hex =
      base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(material)));
  hex.resize(32);
  return base::Uuid::ParseLowercase(hex.substr(0, 8) + "-" + hex.substr(8, 4) +
                                    "-" + hex.substr(12, 4) + "-" +
                                    hex.substr(16, 4) + "-" + hex.substr(20));
}

template <typename Record>
std::vector<Record> RecordsOfType(const std::vector<SyncRecord>& source) {
  std::vector<Record> result;
  for (const SyncRecord& record : source) {
    if (const Record* value = std::get_if<Record>(&record)) {
      result.push_back(*value);
    }
  }
  return result;
}

}  // namespace

ProfileSyncBackend::ProfileSyncBackend(base::FilePath database_path,
                                       base::Uuid device_id,
                                       base::Uuid session_id,
                                       std::string device_name,
                                       bool transport_enabled,
                                       int history_retention_days)
    : database_path_(std::move(database_path)),
      device_id_(std::move(device_id)),
      session_id_(std::move(session_id)),
      device_name_(std::move(device_name)),
      transport_enabled_(transport_enabled),
      history_retention_days_(
          IsValidHistoryRetentionDays(history_retention_days)
              ? history_retention_days
              : kDefaultHistoryRetentionDays),
      clock_(device_id_.AsLowercaseString()) {}

ProfileSyncBackend::~ProfileSyncBackend() {
  CloseSession();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::Initialize() {
  if (!base::CreateDirectory(database_path_.DirName())) {
    return std::nullopt;
  }
  store_ = std::make_unique<SyncStore>();
  if (!store_->Initialize(database_path_)) {
    store_.reset();
    return std::nullopt;
  }

  // Restore the clock from every persisted data class, not merely the local
  // device row. This prevents a restart from issuing versions below a remote
  // future-skewed record that was already accepted.
  for (int raw = static_cast<int>(EntityType::kDevice);
       raw <= static_cast<int>(EntityType::kDeveloperAsset); ++raw) {
    std::vector<SyncRecord> records;
    if (store_->GetRecords(static_cast<EntityType>(raw), &records) !=
        SyncStore::Result::kOk) {
      return std::nullopt;
    }
    for (const SyncRecord& record : records) {
      clock_.Observe(GetVersion(record).stamp);
    }
  }

  SyncRecord existing;
  const bool has_existing_device =
      store_->GetRecord(EntityType::kDevice, device_id_, &existing) ==
      SyncStore::Result::kOk;
  const base::Time now = base::Time::Now();
  std::vector<SyncRecord> old_sessions;
  if (store_->GetRecords(EntityType::kDeviceSession, &old_sessions) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  for (SyncRecord& value : old_sessions) {
    DeviceSessionRecord* session = std::get_if<DeviceSessionRecord>(&value);
    if (!session || session->device_id != device_id_ || !session->active) {
      continue;
    }
    session->active = false;
    session->last_seen = now;
    session->version = {.stamp = clock_.Tick(now)};
    if (!Put(*session)) {
      return std::nullopt;
    }
  }
  DeviceRecord device{
      .id = device_id_,
      .type = DeviceType::kMacDesktop,
      .display_name = device_name_.empty() ? "Mac" : device_name_,
      .created_at = now,
      .last_seen = now,
      .version = {.stamp = clock_.Tick(now)}};
  if (has_existing_device) {
    if (const DeviceRecord* old = std::get_if<DeviceRecord>(&existing)) {
      device.created_at = old->created_at;
    }
  }
  if (!Put(device)) {
    return std::nullopt;
  }

  session_record_ = {.id = session_id_,
                     .device_id = device_id_,
                     .started_at = now,
                     .last_seen = now,
                     .version = {.stamp = clock_.Tick(now)}};
  if (!Put(session_record_)) {
    return std::nullopt;
  }

  tabs_service_ = std::make_unique<DeviceTabsService>(store_.get(), device_id_);
  std::vector<RemoteTabRecord> stored_tabs;
  if (store_->GetRemoteTabs(&stored_tabs) != SyncStore::Result::kOk) {
    return std::nullopt;
  }
  for (RemoteTabRecord old : stored_tabs) {
    if (old.device_id != device_id_ || old.tombstone) {
      continue;
    }
    old.tombstone = true;
    old.version = {.stamp = clock_.Tick(now)};
    if (tabs_service_->RemoveLocalTab(old) != SyncStore::Result::kOk) {
      return std::nullopt;
    }
  }
  if (!EnforceRetention(now)) {
    return std::nullopt;
  }
  if (transport_enabled_) {
    InitializeProviderIfAvailable();
  }
  return CurrentState();
}

std::optional<DeviceTabsSnapshot> ProfileSyncBackend::ReplaceLocalTabs(
    std::vector<LocalTabState> tabs) {
  if (!tabs_service_) {
    return std::nullopt;
  }
  std::map<std::string, LocalTabState> next;
  for (LocalTabState& tab : tabs) {
    if (IsShareableTab(tab)) {
      next.insert_or_assign(tab.stable_key, std::move(tab));
    }
  }
  const base::Time now = base::Time::Now();
  for (auto it = live_tabs_.begin(); it != live_tabs_.end();) {
    if (next.contains(it->first)) {
      ++it;
      continue;
    }
    RemoteTabRecord removed = it->second;
    removed.tombstone = true;
    removed.version = {.stamp = clock_.Tick(now)};
    if (tabs_service_->RemoveLocalTab(removed) != SyncStore::Result::kOk) {
      return std::nullopt;
    }
    it = live_tabs_.erase(it);
  }
  for (const auto& [key, state] : next) {
    auto existing = live_tabs_.find(key);
    if (existing == live_tabs_.end()) {
      RemoteTabRecord record{.id = state.sync_id,
                             .device_id = device_id_,
                             .session_id = session_id_,
                             .workspace_id = state.workspace_id,
                             .url = state.url,
                             .title = state.title,
                             .opened_at = now,
                             .last_active = now,
                             .pinned = state.pinned,
                             .version = {.stamp = clock_.Tick(now)}};
      if (tabs_service_->UpsertLocalTab(record) != SyncStore::Result::kOk) {
        return std::nullopt;
      }
      live_tabs_.emplace(key, std::move(record));
      continue;
    }
    RemoteTabRecord updated = existing->second;
    const bool touch_active =
        state.active && now - updated.last_active >= base::Seconds(5);
    if (updated.workspace_id == state.workspace_id &&
        updated.url == state.url && updated.title == state.title &&
        updated.pinned == state.pinned && !touch_active) {
      continue;
    }
    updated.workspace_id = state.workspace_id;
    updated.url = state.url;
    updated.title = state.title;
    updated.pinned = state.pinned;
    if (touch_active) {
      updated.last_active = now;
    }
    updated.version = {.stamp = clock_.Tick(now)};
    if (tabs_service_->UpsertLocalTab(updated) != SyncStore::Result::kOk) {
      return std::nullopt;
    }
    existing->second = std::move(updated);
  }
  if (tabs_service_->Refresh() != SyncStore::Result::kOk) {
    return std::nullopt;
  }
  return tabs_service_->GetSnapshot();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::MergeLocalTabTree(
    tab_tree::TabTreeSnapshot snapshot,
    bool initial_merge) {
  if (!store_) {
    return std::nullopt;
  }
  for (const tab_tree::Workspace& workspace : snapshot.workspaces) {
    SyncRecord existing;
    if (initial_merge &&
        store_->GetRecord(EntityType::kWorkspace, workspace.id, &existing) ==
            SyncStore::Result::kOk) {
      const WorkspaceRecord* old = std::get_if<WorkspaceRecord>(&existing);
      if (old && old->modified_at >= workspace.modified_at) {
        continue;
      }
    }
    WorkspaceRecord record = WorkspaceToSyncRecord(workspace, {});
    if (!PutDomainRecordIfChanged(std::move(record), workspace.modified_at)) {
      return std::nullopt;
    }
  }
  for (const tab_tree::TreeNode& node : snapshot.nodes) {
    SyncRecord existing;
    if (initial_merge &&
        store_->GetRecord(EntityType::kTreeNode, node.id, &existing) ==
            SyncStore::Result::kOk) {
      const TreeNodeRecord* old = std::get_if<TreeNodeRecord>(&existing);
      if (old && old->modified_at >= node.modified_at) {
        continue;
      }
    }
    TreeNodeRecord record = TreeNodeToSyncRecord(node, {});
    if (!PutDomainRecordIfChanged(std::move(record), node.modified_at)) {
      return std::nullopt;
    }
  }
  return CurrentState();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::AddHistoryVisit(
    std::string url,
    std::string title,
    base::Time visit_time,
    std::string transition,
    int64_t visit_id) {
  const base::Uuid record_id =
      StableHistoryVisitId(device_id_, visit_id, url, visit_time, transition);
  HistoryRecord record{.id = record_id,
                       .device_id = device_id_,
                       .url = std::move(url),
                       .title = std::move(title),
                       .last_visit = visit_time,
                       .visit_count = 1,
                       .transition = std::move(transition),
                       .version = {.stamp = clock_.Tick(visit_time)}};
  return Put(record) ? CurrentState() : std::nullopt;
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::TombstoneHistory(
    std::vector<std::string> urls,
    base::Time begin,
    base::Time end,
    bool all_history) {
  std::set<std::string> selected(urls.begin(), urls.end());
  std::vector<SyncRecord> records;
  if (!store_ || store_->GetRecords(EntityType::kHistoryEntry, &records) !=
                     SyncStore::Result::kOk) {
    return std::nullopt;
  }
  const base::Time now = base::Time::Now();
  for (SyncRecord& value : records) {
    HistoryRecord* record = std::get_if<HistoryRecord>(&value);
    if (!record || record->tombstone) {
      continue;
    }
    const bool in_time = begin.is_null() || (record->last_visit >= begin &&
                                             record->last_visit < end);
    if (!all_history &&
        (!in_time || (!selected.empty() && !selected.contains(record->url)))) {
      continue;
    }
    record->tombstone = true;
    record->version = {.stamp = clock_.Tick(now)};
    if (!Put(*record)) {
      return std::nullopt;
    }
  }
  return CurrentState();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::ApplyRemote(
    ProviderBatch batch) {
  if (!store_ || store_->ApplyRemoteBatch(batch) != SyncStore::Result::kOk) {
    return std::nullopt;
  }
  for (const SyncChange& change : batch.changes) {
    clock_.Observe(change.version.stamp);
  }
  return CurrentState();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::Refresh() {
  return CurrentState();
}

std::vector<RemoteCommandRecord> ProfileSyncBackend::ClaimRemoteCommands(
    RemoteCommandPolicy policy,
    base::Time now) {
  std::vector<RemoteCommandRecord> claimed;
  std::vector<SyncRecord> records;
  if (!store_ || store_->GetRecords(EntityType::kRemoteCommand, &records) !=
                     SyncStore::Result::kOk) {
    return claimed;
  }
  for (SyncRecord& value : records) {
    RemoteCommandRecord* command = std::get_if<RemoteCommandRecord>(&value);
    if (!command || command->tombstone ||
        command->status != RemoteCommandStatus::kQueued ||
        command->target_device_id != device_id_) {
      continue;
    }
    RemoteCommandValidationFailure failure =
        ValidateRemoteCommandForExecution(*command, device_id_, policy, now);
    if (failure == RemoteCommandValidationFailure::kNone) {
      const SyncStore::Result replay = store_->ConsumeRemoteCommand(
          command->id, command->source_device_id, command->nonce_base64,
          command->expires_at, now);
      if (replay != SyncStore::Result::kOk) {
        failure = replay == SyncStore::Result::kAlreadyApplied
                      ? RemoteCommandValidationFailure::kInvalidPayload
                      : RemoteCommandValidationFailure::kInvalidPayload;
      }
    }
    if (failure != RemoteCommandValidationFailure::kNone) {
      command->status = RemoteCommandStatus::kFailed;
      command->result_code =
          failure == RemoteCommandValidationFailure::kInvalidPayload
              ? "replay_rejected"
              : SafeRemoteCommandFailureCode(failure);
      std::ignore = PutDomainRecordIfChanged(*command, now);
      continue;
    }
    command->status = RemoteCommandStatus::kDelivered;
    command->result_code.clear();
    if (PutDomainRecordIfChanged(*command, now)) {
      claimed.push_back(*command);
    }
  }
  return claimed;
}

bool ProfileSyncBackend::CompleteRemoteCommand(base::Uuid command_id,
                                               bool executed,
                                               std::string result_code) {
  SyncRecord value;
  if (!store_ || store_->GetRecord(EntityType::kRemoteCommand, command_id,
                                   &value) != SyncStore::Result::kOk) {
    return false;
  }
  RemoteCommandRecord* command = std::get_if<RemoteCommandRecord>(&value);
  if (!command || command->target_device_id != device_id_ ||
      command->status != RemoteCommandStatus::kDelivered) {
    return false;
  }
  command->status =
      executed ? RemoteCommandStatus::kExecuted : RemoteCommandStatus::kFailed;
  command->result_code = std::move(result_code);
  return PutDomainRecordIfChanged(*command, base::Time::Now());
}

bool ProfileSyncBackend::ConfirmAccountTransition(bool allow_local_upload) {
  if (!provider_ || !store_ || !provider_->IsAccountTransitionPending()) {
    return false;
  }
  if (store_->PrepareOutboxForCloudRecovery(allow_local_upload) !=
      SyncStore::Result::kOk) {
    return false;
  }
  return provider_->ConfirmAccountTransition(allow_local_upload);
}

bool ProfileSyncBackend::ConfirmZoneRecovery() {
  if (!provider_ || !store_ || !provider_->IsZoneRecoveryPending()) {
    return false;
  }
  if (store_->PrepareOutboxForCloudRecovery(true) != SyncStore::Result::kOk) {
    return false;
  }
  return provider_->ConfirmZoneRecovery();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::SetTransportEnabled(
    bool enabled) {
  transport_enabled_ = enabled;
  if (!enabled) {
    pump_.reset();
    provider_.reset();
  } else {
    InitializeProviderIfAvailable();
  }
  return CurrentState();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::SetHistoryRetentionDays(
    int days) {
  if (!IsValidHistoryRetentionDays(days)) {
    return std::nullopt;
  }
  history_retention_days_ = days;
  last_retention_run_ = base::Time();
  return EnforceRetention(base::Time::Now()) ? CurrentState() : std::nullopt;
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::UpsertAppearance(
    AppearanceRecord record) {
  return PutDomainRecordIfChanged(std::move(record), base::Time::Now())
             ? CurrentState()
             : std::nullopt;
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::UpsertPermittedSetting(
    PermittedSettingRecord record) {
  return PutDomainRecordIfChanged(std::move(record), base::Time::Now())
             ? CurrentState()
             : std::nullopt;
}

std::optional<SyncStateSnapshot>
ProfileSyncBackend::ReplaceLocalExtensionInventory(
    std::vector<ExtensionInventoryRecord> records) {
  if (!store_) {
    return std::nullopt;
  }
  std::set<base::Uuid> live_ids;
  const base::Time now = base::Time::Now();
  for (ExtensionInventoryRecord& record : records) {
    if (record.device_id != device_id_ || record.tombstone ||
        !live_ids.insert(record.id).second ||
        !PutDomainRecordIfChanged(std::move(record), now)) {
      return std::nullopt;
    }
  }
  std::vector<SyncRecord> stored;
  if (store_->GetRecords(EntityType::kExtensionInventory, &stored) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  for (SyncRecord& value : stored) {
    ExtensionInventoryRecord* existing =
        std::get_if<ExtensionInventoryRecord>(&value);
    if (!existing || existing->device_id != device_id_ || existing->tombstone ||
        live_ids.contains(existing->id)) {
      continue;
    }
    existing->tombstone = true;
    if (!PutDomainRecordIfChanged(*existing, now)) {
      return std::nullopt;
    }
  }
  return CurrentState();
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::UpsertDeveloperAsset(
    DeveloperAssetRecord record) {
  return PutDomainRecordIfChanged(std::move(record), base::Time::Now())
             ? CurrentState()
             : std::nullopt;
}

void ProfileSyncBackend::SyncNow(
    base::OnceCallback<void(std::optional<SyncStateSnapshot>)> callback) {
  TouchSession();
  if (!pump_) {
    std::move(callback).Run(CurrentState());
    return;
  }
  std::ignore = pump_->SyncNow(
      base::BindOnce(&ProfileSyncBackend::OnSyncFinished,
                     base::Unretained(this), std::move(callback)));
}

void ProfileSyncBackend::CloseSession() {
  if (!store_ || !tabs_service_ || !session_record_.active) {
    return;
  }
  const base::Time now = base::Time::Now();
  for (const auto& [key, live] : live_tabs_) {
    RemoteTabRecord removed = live;
    removed.tombstone = true;
    removed.version = {.stamp = clock_.Tick(now)};
    std::ignore = tabs_service_->RemoveLocalTab(removed);
  }
  live_tabs_.clear();
  session_record_.active = false;
  session_record_.last_seen = now;
  session_record_.version = {.stamp = clock_.Tick(now)};
  std::ignore = Put(session_record_);
}

void ProfileSyncBackend::InitializeProviderIfAvailable() {
  if (!transport_enabled_ || !store_ || provider_) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  std::optional<CloudKitSyncConfigurationMac> configuration =
      CloudKitSyncConfigurationMac::FromMainBundle();
  if (!configuration) {
    return;
  }
  std::unique_ptr<SyncPayloadCryptor> cryptor =
      LoadKeychainSyncPayloadCryptor(*configuration);
  if (!cryptor) {
    return;
  }
  provider_ = CloudKitSyncProviderMac::Create(
      *configuration, database_path_.DirName().AppendASCII("cksync.state"),
      std::move(cryptor));
  if (provider_) {
    pump_ = std::make_unique<SyncPump>(store_.get(), provider_.get());
  }
#endif
}

bool ProfileSyncBackend::EnforceRetention(base::Time now) {
  if (!store_) {
    return false;
  }
  if (!last_retention_run_.is_null() &&
      now - last_retention_run_ < base::Days(1)) {
    return true;
  }
  if (history_retention_days_ >= 0) {
    std::vector<SyncRecord> history;
    if (store_->GetRecords(EntityType::kHistoryEntry, &history) !=
        SyncStore::Result::kOk) {
      return false;
    }
    const base::Time cutoff = now - base::Days(history_retention_days_);
    for (SyncRecord& value : history) {
      HistoryRecord* record = std::get_if<HistoryRecord>(&value);
      if (!record || record->tombstone || record->last_visit >= cutoff) {
        continue;
      }
      record->tombstone = true;
      record->version = {.stamp = clock_.Tick(now)};
      if (!Put(*record)) {
        return false;
      }
    }
  }
  if (store_->CompactExpiredTombstones(now, kTombstoneRetention) !=
      SyncStore::Result::kOk) {
    return false;
  }
  last_retention_run_ = now;
  return true;
}

template <typename Record>
bool ProfileSyncBackend::Put(const Record& record) {
  const SyncStore::Result result = store_->PutLocalRecord(record);
  return result == SyncStore::Result::kOk ||
         result == SyncStore::Result::kAlreadyApplied;
}

template <typename Record>
bool ProfileSyncBackend::PutDomainRecordIfChanged(Record record,
                                                  base::Time mutation_time) {
  SyncRecord existing;
  const SyncStore::Result found = store_->GetRecord(
      GetEntityType(SyncRecord(record)), record.id, &existing);
  if (found == SyncStore::Result::kOk) {
    const Record* old = std::get_if<Record>(&existing);
    if (!old) {
      return false;
    }
    record.version = old->version;
    record.field_versions = old->field_versions;
    if (record == *old) {
      return true;
    }
  } else if (found != SyncStore::Result::kNotFound) {
    return false;
  }
  record.version = {.stamp = clock_.Tick(mutation_time)};
  return Put(record);
}

void ProfileSyncBackend::TouchSession() {
  if (!store_ || !session_record_.active) {
    return;
  }
  const base::Time now = base::Time::Now();
  if (now - session_record_.last_seen < kSessionHeartbeatInterval) {
    return;
  }
  session_record_.last_seen = now;
  session_record_.version = {.stamp = clock_.Tick(now)};
  std::ignore = Put(session_record_);
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::CurrentState() {
  if (!EnforceRetention(base::Time::Now())) {
    return std::nullopt;
  }
  if (!tabs_service_ || tabs_service_->Refresh() != SyncStore::Result::kOk) {
    return std::nullopt;
  }
  SyncStateSnapshot state{
      .transport = {.enabled = transport_enabled_,
                    .provider_available = provider_ != nullptr,
                    .account_transition_pending =
                        provider_ && provider_->IsAccountTransitionPending(),
                    .zone_recovery_pending =
                        provider_ && provider_->IsZoneRecoveryPending(),
                    .pending_outbox =
                        base::saturated_cast<int>(store_->PendingOutboxCount()),
                    .retry = store_->GetRetryState()},
      .device_tabs = tabs_service_->GetSnapshot()};
  std::vector<SyncRecord> records;
  if (store_->GetRecords(EntityType::kWorkspace, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.workspaces = RecordsOfType<WorkspaceRecord>(records);
  if (store_->GetRecords(EntityType::kTreeNode, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.tree_nodes = RecordsOfType<TreeNodeRecord>(records);
  if (store_->GetRecords(EntityType::kHistoryEntry, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.history = RecordsOfType<HistoryRecord>(records);
  if (store_->GetRecords(EntityType::kAppearance, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.appearance = RecordsOfType<AppearanceRecord>(records);
  if (store_->GetRecords(EntityType::kPermittedSetting, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.permitted_settings = RecordsOfType<PermittedSettingRecord>(records);
  if (store_->GetRecords(EntityType::kExtensionInventory, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.extension_inventory = RecordsOfType<ExtensionInventoryRecord>(records);
  if (store_->GetRecords(EntityType::kDeveloperAsset, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  state.developer_assets = RecordsOfType<DeveloperAssetRecord>(records);
  return state;
}

void ProfileSyncBackend::OnSyncFinished(
    base::OnceCallback<void(std::optional<SyncStateSnapshot>)> callback,
    bool success,
    std::string safe_error) {
  std::ignore = success;
  std::ignore = safe_error;
  std::move(callback).Run(CurrentState());
}

}  // namespace ahoi::sync
