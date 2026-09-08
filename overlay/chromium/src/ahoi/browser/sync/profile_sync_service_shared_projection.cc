// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>
#include <utility>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "base/functional/bind.h"

namespace ahoi::sync {
namespace {

std::optional<base::Uuid> KnownAuthor(const FieldVersionMap& clocks,
                                      const std::string& field,
                                      const std::set<base::Uuid>& devices) {
  const auto found = clocks.find(field);
  if (found == clocks.end() || !IsValidSyncClock(found->second) ||
      found->second.device_tiebreak == "9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86") {
    return std::nullopt;
  }
  const auto device = base::Uuid::ParseLowercase(found->second.device_tiebreak);
  return devices.contains(device) ? std::make_optional(device) : std::nullopt;
}

}  // namespace

SharedTabProvenance ProfileSyncService::GetSharedTabProvenance(
    const base::Uuid& tree_node_id) const {
  const auto found = shared_tab_provenance_.find(tree_node_id);
  return shared_tab_state_.projection_ready &&
                 found != shared_tab_provenance_.end()
             ? found->second
             : SharedTabProvenance();
}

void ProfileSyncService::RefreshSharedTabProjection() {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !ui_bridge_ ||
      !shared_tab_state_.projection_ready) {
    return;
  }
  if (shared_projection_pending_) {
    shared_projection_requested_ = true;
    return;
  }
  shared_projection_pending_ = true;
  backend_.AsyncCall(&ProfileSyncBackend::ReadSharedTabProjection)
      .Then(base::BindOnce(&ProfileSyncService::OnSharedTabProjection,
                           backend_weak_ptr_factory_.GetWeakPtr(),
                           native_tree_revision_));
}

void ProfileSyncService::OnSharedTabProjection(
    uint64_t native_revision,
    std::optional<SharedTabProjection> projection) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !ui_bridge_ ||
      native_revision != native_tree_revision_ || !projection ||
      !projection->authorization || !projection->authorization.Run()) {
    FinishSharedTabProjection();
    return;
  }
  NativeTreeSyncSnapshot native;
  if (!ui_bridge_->ExportTabTreeSyncSnapshot(&native.tree,
                                             &native.baseline_receipt)) {
    FinishSharedTabProjection();
    return;  // Native receipt support is required, never an old-snapshot
             // fallback.
  }
  native.authorization = base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> profile_cancelled,
         std::shared_ptr<std::atomic<bool>> native_cancelled) {
        return !profile_cancelled->load(std::memory_order_acquire) &&
               !native_cancelled->load(std::memory_order_acquire);
      },
      profile_scope_cancelled_, native_tree_cancelled_);
  backend_.AsyncCall(&ProfileSyncBackend::PrepareSharedTabProjection)
      .WithArgs(std::move(native), std::move(*projection))
      .Then(base::BindOnce(&ProfileSyncService::OnSharedTabProjectionPrepared,
                           backend_weak_ptr_factory_.GetWeakPtr(),
                           native_revision));
}

void ProfileSyncService::OnSharedTabProjectionPrepared(
    uint64_t native_revision,
    std::optional<PreparedSharedTabProjection> projection) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() || !ui_bridge_ ||
      native_revision != native_tree_revision_ || !projection ||
      !projection->authorization || !projection->authorization.Run()) {
    FinishSharedTabProjection();
    return;
  }
  tab_tree::TabTreeSnapshot current;
  std::string current_receipt;
  if (!ui_bridge_->ExportTabTreeSyncSnapshot(&current, &current_receipt)) {
    FinishSharedTabProjection();
    return;
  }
  const bool changed = current != projection->tree ||
                       current_receipt != projection->baseline_receipt;
  bool applied = true;
  if (changed) {
    // Native projection owns persistence, preserving runtime/focus/scroll and
    // local-only originals. Common code does not navigate any WebContents.
    applying_synced_tree_ = true;
    applied =
        ui_bridge_->ApplySyncedTabTreeSnapshotWithReceipt(
            projection->tree, projection->baseline_receipt,
            projection->authorization) == tab_tree::TabTreeStore::Result::kOk;
    applying_synced_tree_ = false;
    if (applied) {
      applied =
          ui_bridge_->ExportTabTreeSyncSnapshot(&current, &current_receipt) &&
          current == projection->tree &&
          current_receipt == projection->baseline_receipt;
    }
  }
  if (applied && projection->authorization.Run()) {
    std::set<base::Uuid> known_devices;
    for (const auto& device : projection->devices) {
      if (!device.tombstone) {
        known_devices.insert(device.id);
      }
    }
    shared_tab_provenance_.clear();
    for (const auto& node : projection->tree_nodes) {
      if (node.tombstone || node.kind != TreeNodeKind::kPage) {
        continue;
      }
      shared_tab_provenance_.emplace(
          node.id, SharedTabProvenance{
                       .creation_device = KnownAuthor(
                           node.field_versions, "created_at", known_devices),
                       .saved_device =
                           node.is_temporary
                               ? std::nullopt
                               : KnownAuthor(node.field_versions,
                                             "is_temporary", known_devices)});
    }
    SetSharedTabState(std::move(projection->readiness));
    const bool capture_requested =
        std::exchange(capture_after_projection_, false);
    if (changed || capture_requested) {
      ScheduleLocalPublish();
    }
  } else if (!applied) {
    auto state = shared_tab_state_;
    state.issue = SharedTabSyncIssue::kStoreError;
    SetSharedTabState(std::move(state));
  }
  if (projection->needs_sync) {
    SyncNow();
  }
  FinishSharedTabProjection();
}

void ProfileSyncService::FinishSharedTabProjection() {
  shared_projection_pending_ = false;
  if (std::exchange(shared_projection_requested_, false)) {
    RefreshSharedTabProjection();
  }
}

}  // namespace ahoi::sync
