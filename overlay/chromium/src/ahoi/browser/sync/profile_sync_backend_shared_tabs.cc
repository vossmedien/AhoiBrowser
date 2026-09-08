// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/sync/device_tabs_service.h"
#include "ahoi/browser/sync/native_tree_sync_journal.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_unified_validation.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/functional/bind.h"

namespace ahoi::sync {
namespace {

constexpr char kFeature[] = "shared-normal-tabs-v3";

bool ValidId(const base::Uuid& id) {
  return IsCanonicalSyncDeviceId(id.AsLowercaseString());
}

SyncAuthorization Both(SyncAuthorization first, SyncAuthorization second) {
  return base::BindRepeating(
      [](SyncAuthorization a, SyncAuthorization b) {
        return a && b && a.Run() && b.Run();
      },
      std::move(first), std::move(second));
}

}  // namespace

void ProfileSyncBackend::RevokeSharedProjection() {
  shared_projection_cancelled_->store(true, std::memory_order_release);
  shared_projection_cancelled_ = std::make_shared<std::atomic<bool>>(false);
}

void ProfileSyncBackend::OnSyncStoreChanged() {
  std::ignore = RefreshBrowserSettingScopes();
  // A delayed projection must never overwrite a newer local or remote row.
  RevokeSharedProjection();
  if (!SharedTabState().write_allowed) {
    RevokeSharedCapture();
  }
}

void ProfileSyncBackend::RevokeSharedCapture() {
  shared_capture_cancelled_->store(true, std::memory_order_release);
  original_capture_authorization_.Reset();
}

bool ProfileSyncBackend::PublishLocalCapability() {
  if (!store_ || !ProfileScopeActive()) {
    return false;
  }
  DeviceCapabilityRecord record{.id = CapabilityIdForDevice(device_id_),
                                .device_id = device_id_};
  if (shared_native_support_.projection && shared_native_support_.capture) {
    record.features = {kFeature};
  }
  SyncRecord previous;
  const auto found =
      store_->GetRecord(EntityType::kDeviceCapability, record.id, &previous);
  if (found == SyncStore::Result::kOk) {
    const auto& old = std::get<DeviceCapabilityRecord>(previous);
    if (old.tombstone) {
      return false;
    }
    if (ValidateCapability(old) &&
        std::ranges::contains(old.features, kFeature) &&
        (!shared_native_support_.projection ||
         !shared_native_support_.capture)) {
      // Window/bridge availability is local readiness, not a downgrade of the
      // installed client's admitted format support. Closing the last Mac
      // window must not disable shared-tab editing on the other devices.
      return true;
    }
    if (old.features == record.features &&
        old.readable_models == record.readable_models &&
        old.writable_models == record.writable_models) {
      return true;
    }
  } else if (found != SyncStore::Result::kNotFound) {
    return false;
  }
  record.version = {.stamp = clock_.Tick(base::Time::Now())};
  const auto result = store_->PutLocalRecord(record);
  return result == SyncStore::Result::kOk ||
         result == SyncStore::Result::kAlreadyApplied;
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::SetSharedTabNativeSupport(
    SharedTabNativeSupport support) {
  if (support != shared_native_support_) {
    RevokeSharedProjection();
    RevokeSharedCapture();
    shared_native_support_ = support;
  }
  if (!store_ || !PublishLocalCapability()) {
    return std::nullopt;
  }
  return CurrentState();
}

SharedTabSyncState ProfileSyncBackend::SharedTabState() {
  SharedTabSyncState state;
  if (!store_ || !transport_enabled_ || !ProfileScopeActive()) {
    return state;
  }
  if (!shared_native_support_.projection) {
    state.issue = SharedTabSyncIssue::kNativeNotReady;
    return state;
  }
  if (provider_ && (provider_->IsAccountTransitionPending() ||
                    provider_->IsZoneRecoveryPending())) {
    state.issue = SharedTabSyncIssue::kRecoveryPending;
    return state;
  }
  auto provider_scope =
      provider_ ? provider_->GetTransportAuthorization() : SyncAuthorization();
  if (!provider_scope || !provider_scope.Run() ||
      !store_->HasCompletedInitialFetch()) {
    state.issue = SharedTabSyncIssue::kBootstrapPending;
    return state;
  }
  state.projection_ready = true;
  if (!shared_native_support_.capture) {
    state.issue = SharedTabSyncIssue::kNativeNotReady;
    return state;
  }
  std::vector<SyncRecord> devices;
  std::vector<SyncRecord> capabilities;
  if (store_->GetRecords(EntityType::kDevice, &devices) !=
          SyncStore::Result::kOk ||
      store_->GetRecords(EntityType::kDeviceCapability, &capabilities) !=
          SyncStore::Result::kOk) {
    state.issue = SharedTabSyncIssue::kStoreError;
    state.projection_ready = false;
    return state;
  }
  std::map<base::Uuid, const DeviceRecord*> known;
  for (const auto& value : devices) {
    const auto& device = std::get<DeviceRecord>(value);
    known.emplace(device.id, &device);
  }
  std::map<base::Uuid, const DeviceCapabilityRecord*> declared;
  std::set<base::Uuid> blocking;
  for (const auto& value : capabilities) {
    const auto& capability = std::get<DeviceCapabilityRecord>(value);
    if (!known.contains(capability.device_id)) {
      blocking.insert(
          capability
              .device_id);  // Retained, but not admitted by its own assertion.
      continue;
    }
    declared.emplace(capability.device_id, &capability);
  }
  bool local_acknowledged = false;
  for (const auto& value : devices) {
    const auto& device = std::get<DeviceRecord>(value);
    if (device.retired || device.tombstone) {
      continue;  // Explicit retirement only; offline age is not retirement.
    }
    const auto found = declared.find(device.id);
    if (found == declared.end() || found->second->tombstone ||
        !ValidateCapability(*found->second) ||
        !std::ranges::contains(found->second->features, kFeature)) {
      blocking.insert(device.id);
      continue;
    }
    if (device.id == device_id_) {
      local_acknowledged = store_->IsRecordAcknowledged(value) &&
                           store_->IsRecordAcknowledged(*found->second);
      if (!local_acknowledged) {
        blocking.insert(device.id);
      }
    }
  }
  if (!local_acknowledged) {
    blocking.insert(device_id_);
  }
  state.blocking_devices.assign(blocking.begin(), blocking.end());
  state.write_allowed = blocking.empty() && local_acknowledged;
  state.issue = state.write_allowed ? SharedTabSyncIssue::kNone
                                    : SharedTabSyncIssue::kBootstrapPending;
  return state;
}

SyncAuthorization ProfileSyncBackend::CaptureSharedAuthorization(
    bool require_write) {
  const auto state = SharedTabState();
  if (require_write ? !state.write_allowed : !state.projection_ready) {
    return {};
  }
  auto provider_scope = provider_->GetTransportAuthorization();
  if (!provider_scope || !provider_scope.Run()) {
    return {};
  }
  return Both(profile_authorization_, std::move(provider_scope));
}

void ProfileSyncBackend::BeginSharedTabCapture(uint64_t generation) {
  if (generation > expected_capture_generation_) {
    RevokeSharedCapture();
    expected_capture_generation_ = generation;
    shared_capture_cancelled_ = std::make_shared<std::atomic<bool>>(false);
    auto cancellation = base::BindRepeating(
        [](std::shared_ptr<std::atomic<bool>> cancelled) {
          return !cancelled->load(std::memory_order_acquire);
        },
        shared_capture_cancelled_);
    // Preserve the original account/key generation even if the UI never sees
    // an intervening account transition before a fresh provider becomes ready.
    original_capture_authorization_ =
        Both(CaptureSharedAuthorization(true), std::move(cancellation));
  }
}

SharedTabCaptureResult ProfileSyncBackend::ApplySharedTabCapture(
    SharedTabCaptureRequest request) {
  SharedTabCaptureResult result;
  result.generation = request.capture.generation;
  result.readiness = SharedTabState();
  auto authority =
      Both(std::move(request.authorization), original_capture_authorization_);
  if (!tabs_service_ || !request.capture.generation ||
      request.capture.generation != expected_capture_generation_ ||
      request.capture.generation <= applied_capture_generation_ ||
      request.capture.status != LocalTabCaptureStatus::kComplete ||
      !result.readiness.write_allowed || !authority.Run()) {
    result.readiness.issue = SharedTabSyncIssue::kCaptureDeferred;
    return result;
  }
  std::map<std::string, LocalTabState> next;
  std::set<base::Uuid> identities;
  std::set<base::Uuid> pages;
  std::map<std::string, std::string> captured_windows;
  for (const auto& [window, keys] : request.window_keys) {
    if (window.empty()) {
      result.disposition = SharedTabCaptureDisposition::kInvalid;
      return result;
    }
    for (const auto& key : keys) {
      if (!captured_windows.emplace(key, window).second) {
        result.disposition = SharedTabCaptureDisposition::kInvalid;
        return result;
      }
    }
  }
  if (request.window_keys.empty() ||
      captured_windows.size() != request.capture.tabs.size()) {
    result.disposition = SharedTabCaptureDisposition::kInvalid;
    return result;
  }
  for (auto& tab : request.capture.tabs) {
    if (tab.stable_key.empty() || !ValidId(tab.sync_id) || !tab.tree_node_id ||
        !ValidId(*tab.tree_node_id) || tab.sync_id == *tab.tree_node_id ||
        !identities.insert(tab.sync_id).second ||
        !pages.insert(*tab.tree_node_id).second ||
        next.contains(tab.stable_key) ||
        !captured_windows.contains(tab.stable_key) ||
        !ValidateSharedTarget(tab.url, tab.target_kind, tab.local_scheme)) {
      result.disposition = SharedTabCaptureDisposition::kInvalid;
      result.readiness.issue = SharedTabSyncIssue::kInvalidCapture;
      return result;
    }
    SyncRecord stored;
    if (store_->GetRecord(EntityType::kTreeNode, *tab.tree_node_id, &stored) !=
        SyncStore::Result::kOk) {
      result.readiness.issue = SharedTabSyncIssue::kCaptureDeferred;
      return result;
    }
    const auto& page = std::get<TreeNodeRecord>(stored);
    if (page.tombstone || page.kind != TreeNodeKind::kPage ||
        page.url != tab.url || page.target_kind != tab.target_kind ||
        page.local_scheme != tab.local_scheme ||
        (tab.workspace_id && *tab.workspace_id != page.workspace_id)) {
      result.readiness.issue = SharedTabSyncIssue::kCaptureDeferred;
      return result;
    }
    const auto old = live_tabs_.find(tab.stable_key);
    if (old != live_tabs_.end() && old->second.id != tab.sync_id) {
      result.disposition = SharedTabCaptureDisposition::kInvalid;
      result.readiness.issue = SharedTabSyncIssue::kInvalidCapture;
      return result;
    }
    tab.workspace_id = page.workspace_id;
    tab.pinned = !page.is_temporary;
    next.emplace(tab.stable_key, std::move(tab));
  }
  const auto now = base::Time::Now();
  auto committed_live = live_tabs_;
  auto committed_windows = live_tab_windows_;
  std::vector<SyncRecord> changes;
  for (const auto& [key, window] : live_tab_windows_) {
    if (!request.window_keys.contains(window)) {
      continue;  // Detached window: preserve, never interpret as a user close.
    }
    const auto old = committed_live.find(key);
    if (old == committed_live.end() || next.contains(key)) {
      continue;
    }
    auto closed = old->second;
    closed.tombstone = true;
    closed.version = {.stamp = clock_.Tick(now)};
    changes.emplace_back(std::move(closed));
    committed_live.erase(old);
    committed_windows.erase(key);
  }
  for (const auto& [key, tab] : next) {
    committed_windows.insert_or_assign(key, captured_windows.at(key));
    const auto old = committed_live.find(key);
    RemoteTabRecord value = old == committed_live.end()
                                ? RemoteTabRecord{.id = tab.sync_id,
                                                  .device_id = device_id_,
                                                  .session_id = session_id_,
                                                  .opened_at = now,
                                                  .last_active = now}
                                : old->second;
    const auto previous = value;
    value.workspace_id = tab.workspace_id;
    value.tree_node_id = tab.tree_node_id;
    value.url = tab.url;
    value.target_kind = tab.target_kind;
    value.local_scheme = tab.local_scheme;
    value.title = tab.title;
    value.pinned = tab.pinned;
    if (tab.active && now - value.last_active >= base::Seconds(5)) {
      value.last_active = now;
    }
    if (old != committed_live.end() && value == previous) {
      continue;
    }
    value.version = {.stamp = clock_.Tick(now)};
    changes.emplace_back(value);
    committed_live.insert_or_assign(key, std::move(value));
  }
  identities.clear();
  pages.clear();
  for (const auto& [key, tab] : committed_live) {
    if (!identities.insert(tab.id).second || !tab.tree_node_id ||
        !pages.insert(*tab.tree_node_id).second) {
      result.disposition = SharedTabCaptureDisposition::kInvalid;
      result.readiness.issue = SharedTabSyncIssue::kInvalidCapture;
      return result;
    }
  }
  const auto stored = store_->PutLocalBatch(changes, authority);
  if (stored != SyncStore::Result::kOk) {
    result.disposition = stored == SyncStore::Result::kNotAuthorized
                             ? SharedTabCaptureDisposition::kDeferred
                         : stored == SyncStore::Result::kDatabaseError
                             ? SharedTabCaptureDisposition::kStoreError
                             : SharedTabCaptureDisposition::kInvalid;
    result.readiness.issue = stored == SyncStore::Result::kDatabaseError
                                 ? SharedTabSyncIssue::kStoreError
                                 : SharedTabSyncIssue::kCaptureDeferred;
    return result;
  }
  live_tabs_ = std::move(committed_live);
  live_tab_windows_ = std::move(committed_windows);
  applied_capture_generation_ = request.capture.generation;
  result.disposition = SharedTabCaptureDisposition::kApplied;
  result.needs_sync = !changes.empty();
  result.authorization = std::move(authority);
  result.readiness = SharedTabState();
  if (tabs_service_->Refresh() == SyncStore::Result::kOk) {
    result.snapshot = tabs_service_->GetSnapshot();
  }
  return result;
}

std::optional<SharedTabProjection>
ProfileSyncBackend::ReadSharedTabProjection() {
  auto snapshot = CurrentState();
  auto authority = CaptureSharedAuthorization(false);
  if (!snapshot || !authority || !authority.Run()) {
    return std::nullopt;
  }
  std::vector<SyncRecord> device_records;
  if (store_->GetRecords(EntityType::kDevice, &device_records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  std::vector<DeviceRecord> devices;
  for (const auto& record : device_records) {
    devices.push_back(std::get<DeviceRecord>(record));
  }
  auto revision = base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire);
      },
      shared_projection_cancelled_);
  return SharedTabProjection{
      .workspaces = std::move(snapshot->workspaces),
      .tree_nodes = std::move(snapshot->tree_nodes),
      .devices = std::move(devices),
      .readiness = snapshot->shared_tabs,
      .scope = authority,
      .authorization = Both(std::move(authority), std::move(revision))};
}

std::optional<PreparedSharedTabProjection>
ProfileSyncBackend::PrepareSharedTabProjection(NativeTreeSyncSnapshot native,
                                               SharedTabProjection projection) {
  auto read_authority = Both(projection.authorization, native.authorization);
  auto original_scope = Both(projection.scope, native.authorization);
  if (!store_ || !read_authority.Run() || !SharedTabState().projection_ready) {
    return std::nullopt;
  }
  const auto write_authority =
      SharedTabState().write_allowed ? original_scope : SyncAuthorization();
  NativeTreeSyncJournal journal(store_.get());
  bool wrote_records = false;
  if (!journal.ReconcileLocal(native, &clock_, read_authority, write_authority,
                              &wrote_records) ||
      !original_scope.Run()) {
    return std::nullopt;
  }
  // Re-read after our own atomic local merge. Its Notify invalidates the old
  // store revision, but NEVER replaces the original account/key/native scope.
  auto current = CurrentState();
  if (!current || !current->shared_tabs.projection_ready) {
    return std::nullopt;
  }
  auto tree = ReconcileTabTreeRecords(native.tree, current->workspaces,
                                      current->tree_nodes);
  if (!tree) {
    return std::nullopt;
  }
  auto receipt = *tree == native.tree && !native.baseline_receipt.empty()
                     ? std::make_optional(native.baseline_receipt)
                     : journal.PlanProjection(*tree, original_scope);
  if (!receipt || !original_scope.Run()) {
    return std::nullopt;
  }
  auto revision = base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire);
      },
      shared_projection_cancelled_);
  return PreparedSharedTabProjection{
      .tree = std::move(*tree),
      .baseline_receipt = std::move(*receipt),
      .tree_nodes = std::move(current->tree_nodes),
      .devices = std::move(projection.devices),
      .readiness = std::move(current->shared_tabs),
      .needs_sync = wrote_records,
      .authorization = Both(std::move(original_scope), std::move(revision))};
}

}  // namespace ahoi::sync
