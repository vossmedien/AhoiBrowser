// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <set>
#include <utility>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/check.h"
#include "base/functional/bind.h"

namespace ahoi::sync {

void ProfileSyncService::CancelSharedTabCapture() {
  shared_capture_cancelled_->store(true, std::memory_order_release);
  pending_window_captures_.clear();
  shared_capture_submitted_ = false;
}

void ProfileSyncService::StopSharedTabs() {
  CancelSharedTabCapture();
  shared_tab_provenance_.clear();
  registered_window_keys_.clear();
  shared_projection_pending_ = false;
  shared_projection_requested_ = false;
  capture_after_projection_ = false;
  native_tree_cancelled_->store(true, std::memory_order_release);
  native_tree_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  ++native_tree_revision_;
  SetSharedTabState({});
}

void ProfileSyncService::SetSharedTabState(SharedTabSyncState state) {
  if (state.projection_ready &&
      (!ui_bridge_ || !ui_bridge_->GetSharedTabNativeSupport().projection)) {
    state = {.issue = SharedTabSyncIssue::kNativeNotReady};
  }
  if (state == shared_tab_state_) {
    return;
  }
  const bool reopened = !shared_tab_state_.write_allowed && state.write_allowed;
  shared_tab_state_ = std::move(state);
  if (!shared_tab_state_.write_allowed) {
    CancelSharedTabCapture();
  }
  if (!shared_tab_state_.projection_ready) {
    shared_tab_provenance_.clear();
  }
  for (Observer& observer : observers_) {
    observer.OnAhoiSharedTabSyncStateChanged(shared_tab_state_);
  }
  if (reopened) {
    ScheduleLocalPublish();  // Fresh capture, never replay pre-recovery
                             // vectors.
  }
}

void ProfileSyncService::UpdateSharedTabNativeSupport() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  auto support = ui_bridge_ ? ui_bridge_->GetSharedTabNativeSupport()
                            : SharedTabNativeSupport();
  // Implementation support is not the readiness of a particular snapshot.
  // Pending local disk persistence defers Export, not capability admission;
  // treating it as unsupported here could latch the initial bridge off forever.
  // Default bridges still explicitly report no implemented native support.
  backend_.AsyncCall(&ProfileSyncBackend::SetSharedTabNativeSupport)
      .WithArgs(support)
      .Then(base::BindOnce(&ProfileSyncService::OnSharedTabSupportUpdated,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnSharedTabSupportUpdated(
    std::optional<SyncStateSnapshot> state) {
  OnBackendState(std::move(state));
  SyncNow();  // Device/capability ACK is a real transport result, not queue
              // size.
}

void ProfileSyncService::RequestSharedTabCapture(std::string window_key) {
  if (shutting_down_ || window_key.empty()) {
    return;
  }
  registered_window_keys_.insert(std::move(window_key));
  CancelSharedTabCapture();
  ScheduleLocalPublish();
}

void ProfileSyncService::PublishWindowTabs(std::string window_key,
                                           std::vector<LocalTabState> tabs) {
  // This obsolete caller cannot attest generation/completeness. In particular,
  // an empty vector must not become a close-all event during native
  // integration.
  std::ignore = tabs;
  RequestSharedTabCapture(std::move(window_key));
}

void ProfileSyncService::RemoveWindowTabs(const std::string& window_key) {
  if (!registered_window_keys_.erase(window_key)) {
    return;
  }
  CancelSharedTabCapture();
  publish_timer_.Stop();
  // Keep the accepted window rows and identity reservations. The backend keeps
  // that window's Presence rows until explicit close or normal session expiry.
  // Commands cannot target runtime keys whose host is no longer attached.
  const auto detached = window_tabs_.find(window_key);
  if (detached != window_tabs_.end()) {
    for (const auto& tab : detached->second) {
      local_tab_keys_by_sync_id_.erase(tab.sync_id);
    }
  }
  ScheduleLocalPublish();
}

void ProfileSyncService::ScheduleLocalPublish() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      !shared_tab_state_.write_allowed || registered_window_keys_.empty()) {
    return;
  }
  publish_timer_.Start(FROM_HERE, base::Milliseconds(80), this,
                       &ProfileSyncService::PublishCombinedLocalTabs);
}

void ProfileSyncService::PublishCombinedLocalTabs() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !ui_bridge_ ||
      !shared_tab_state_.write_allowed || registered_window_keys_.empty()) {
    return;
  }
  if (capture_after_projection_ || shared_projection_pending_) {
    capture_after_projection_ = true;
    RefreshSharedTabProjection();
    return;  // Publish only after local Page intents/remote projection settle.
  }
  CancelSharedTabCapture();
  CHECK_LT(shared_capture_generation_, std::numeric_limits<uint64_t>::max());
  ++shared_capture_generation_;
  shared_capture_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  // SequenceBound preserves this ordering even when native hosts answer
  // synchronously. Begin captures the original backend account/key authority.
  backend_.AsyncCall(&ProfileSyncBackend::BeginSharedTabCapture)
      .WithArgs(shared_capture_generation_);
  ui_bridge_->RequestSharedTabCapture(shared_capture_generation_);
}

void ProfileSyncService::PublishSharedTabCapture(std::string window_key,
                                                 LocalTabCapture capture) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      !registered_window_keys_.contains(window_key) || !capture.generation ||
      capture.generation != shared_capture_generation_ ||
      shared_capture_submitted_ ||
      shared_capture_cancelled_->load(std::memory_order_acquire)) {
    return;
  }
  if (capture.status != LocalTabCaptureStatus::kComplete) {
    CancelSharedTabCapture();
    auto state = shared_tab_state_;
    state.issue = SharedTabSyncIssue::kCaptureDeferred;
    SetSharedTabState(std::move(state));
    return;
  }
  pending_window_captures_.insert_or_assign(std::move(window_key),
                                            std::move(capture));
  if (pending_window_captures_.size() != registered_window_keys_.size()) {
    return;
  }
  SharedTabCaptureRequest request;
  request.capture.generation = shared_capture_generation_;
  request.capture.status = LocalTabCaptureStatus::kComplete;
  std::set<base::Uuid> identities;
  std::set<std::string> keys;
  bool valid = true;
  for (auto& [window, response] : pending_window_captures_) {
    auto& window_keys = request.window_keys[window];
    for (auto& tab : response.tabs) {
      auto found = tab_sync_ids_.find(tab.stable_key);
      if (found == tab_sync_ids_.end()) {
        const auto id = tab.sync_id.is_valid() ? tab.sync_id
                                               : base::Uuid::GenerateRandomV4();
        found = tab_sync_ids_.emplace(tab.stable_key, id).first;
      } else if (tab.sync_id.is_valid() && tab.sync_id != found->second) {
        valid = false;
      }
      tab.sync_id = found->second;
      valid &= !tab.stable_key.empty() && keys.insert(tab.stable_key).second &&
               identities.insert(tab.sync_id).second;
      window_keys.insert(tab.stable_key);
      request.capture.tabs.push_back(tab);
    }
  }
  if (!valid) {
    CancelSharedTabCapture();
    auto state = shared_tab_state_;
    state.issue = SharedTabSyncIssue::kInvalidCapture;
    SetSharedTabState(std::move(state));
    return;
  }
  request.authorization = base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> profile_cancelled,
         std::shared_ptr<std::atomic<bool>> capture_cancelled) {
        return !profile_cancelled->load(std::memory_order_acquire) &&
               !capture_cancelled->load(std::memory_order_acquire);
      },
      profile_scope_cancelled_, shared_capture_cancelled_);
  shared_capture_submitted_ = true;
  backend_.AsyncCall(&ProfileSyncBackend::ApplySharedTabCapture)
      .WithArgs(std::move(request))
      .Then(base::BindOnce(&ProfileSyncService::OnSharedTabCaptureApplied,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnSharedTabCaptureApplied(
    SharedTabCaptureResult result) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      result.generation != shared_capture_generation_ ||
      shared_capture_cancelled_->load(std::memory_order_acquire)) {
    return;
  }
  if (result.disposition != SharedTabCaptureDisposition::kApplied ||
      !result.authorization || !result.authorization.Run()) {
    CancelSharedTabCapture();
    SetSharedTabState(std::move(result.readiness));
    return;
  }
  std::set<std::string> replaced_keys;
  for (auto& [window, response] : pending_window_captures_) {
    const auto old = window_tabs_.find(window);
    if (old != window_tabs_.end()) {
      for (const auto& tab : old->second) {
        replaced_keys.insert(tab.stable_key);
      }
    }
    window_tabs_.insert_or_assign(window, std::move(response.tabs));
  }
  local_tab_keys_by_sync_id_.clear();
  for (const auto& [window, tabs] : window_tabs_) {
    for (const auto& tab : tabs) {
      replaced_keys.erase(tab.stable_key);
      if (registered_window_keys_.contains(window)) {
        local_tab_keys_by_sync_id_[tab.sync_id] = tab.stable_key;
      }
    }
  }
  for (const auto& key : replaced_keys) {
    tab_sync_ids_.erase(key);
  }
  pending_window_captures_.clear();
  SetSharedTabState(std::move(result.readiness));
  OnBackendSnapshot(std::move(result.snapshot));
  if (result.needs_sync) {
    SyncNow();
  }
}

}  // namespace ahoi::sync
