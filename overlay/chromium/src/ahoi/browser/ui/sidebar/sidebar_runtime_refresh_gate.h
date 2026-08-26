// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_REFRESH_GATE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_REFRESH_GATE_H_

namespace ahoi::sidebar {

// Coalesces asynchronous runtime-presentation refreshes without allowing them
// to replace the Views source of an active native AppKit drag session.
class SidebarRuntimeRefreshGate {
 public:
  bool TrySchedule(bool drag_active) {
    if (drag_active) {
      deferred_during_drag_ = true;
      return false;
    }
    if (scheduled_) {
      return false;
    }
    scheduled_ = true;
    return true;
  }

  // Called by the posted refresh task. Returns false when the task must defer
  // because a drag began after it was scheduled.
  bool BeginRefresh(bool drag_active) {
    scheduled_ = false;
    if (drag_active) {
      deferred_during_drag_ = true;
      return false;
    }
    deferred_during_drag_ = false;
    return true;
  }

  // Consumes one deferred refresh after the final drag source is released.
  bool ConsumeDeferredAfterDrag(bool drag_active) {
    if (drag_active || !deferred_during_drag_) {
      return false;
    }
    deferred_during_drag_ = false;
    return true;
  }

  bool scheduled_for_testing() const { return scheduled_; }
  bool deferred_for_testing() const { return deferred_during_drag_; }

 private:
  bool scheduled_ = false;
  bool deferred_during_drag_ = false;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_REFRESH_GATE_H_
