// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"

namespace ahoi::sidebar {

void BrowserSidebarHostView::ScheduleRuntimePresentationRefresh() {
  if (!runtime_refresh_gate_.TrySchedule(IsSidebarDragActive())) {
    return;
  }
  const uint64_t generation = ++runtime_refresh_generation_;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &BrowserSidebarHostView::RunScheduledRuntimePresentationRefresh,
          weak_ptr_factory_.GetWeakPtr(), generation));
}

void BrowserSidebarHostView::RunScheduledRuntimePresentationRefresh(
    uint64_t generation) {
  if (generation != runtime_refresh_generation_) {
    return;
  }
  RefreshRuntimePresentation();
}

void BrowserSidebarHostView::PrimeRuntimeAuxiliaryPresentation() {
  runtime_auxiliary_prime_scheduled_ = false;
  if (runtime_auxiliary_ready_) {
    return;
  }
  runtime_auxiliary_ready_ = true;
  if (profile_sync_service_ && !profile_sync_ui_attached_) {
    // Mark attachment before registering: AddObserver publishes its current
    // snapshots synchronously and may re-enter the sidebar refresh path.
    profile_sync_ui_attached_ = true;
    profile_sync_service_->AttachUiBridge(session_bridge_);
    profile_sync_service_->AddObserver(this);
  }
  ScheduleRuntimePresentationRefresh();
}

bool BrowserSidebarHostView::IsSidebarDragActive() const {
  return dragged_node_id_.has_value() ||
         dragged_runtime_tab_handle_.has_value() ||
         sidebar_split_resize_active_;
}

void BrowserSidebarHostView::MaybeScheduleDeferredRuntimePresentationRefresh() {
  if (runtime_refresh_gate_.ConsumeDeferredAfterDrag(IsSidebarDragActive())) {
    ScheduleRuntimePresentationRefresh();
  }
}

}  // namespace ahoi::sidebar
