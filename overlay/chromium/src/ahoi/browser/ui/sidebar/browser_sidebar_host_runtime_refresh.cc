// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"

namespace ahoi::sidebar {

void BrowserSidebarHostView::ScheduleRuntimePresentationRefresh() {
  if (!runtime_refresh_gate_.TrySchedule(IsSidebarDragActive())) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&BrowserSidebarHostView::RefreshRuntimePresentation,
                     weak_ptr_factory_.GetWeakPtr()));
}

bool BrowserSidebarHostView::IsSidebarDragActive() const {
  return dragged_node_id_.has_value() ||
         dragged_runtime_tab_handle_.has_value();
}

void BrowserSidebarHostView::MaybeScheduleDeferredRuntimePresentationRefresh() {
  if (runtime_refresh_gate_.ConsumeDeferredAfterDrag(IsSidebarDragActive())) {
    ScheduleRuntimePresentationRefresh();
  }
}

}  // namespace ahoi::sidebar
