// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_CMD_SCROLL_TAB_SWITCHER_H_
#define AHOI_BROWSER_NAVIGATION_CMD_SCROLL_TAB_SWITCHER_H_

#include "base/time/time.h"
#include "ui/events/event_constants.h"

namespace ahoi {

// Converts a Cmd+scroll stream into one tab-selection request. The class is
// deliberately UI-agnostic so native event routing can be tested without a
// Browser or a live TabStripModel.
enum class CmdScrollTabDecision {
  kNone = 0,
  kPreviewPrevious,
  kPreviewNext,
  kSwitchPrevious,
  kSwitchNext,
  kConsume,
};

struct CmdScrollTabSettings {
  bool enabled = true;
  float threshold = 24.0f;
  base::TimeDelta minimum_interval = base::Milliseconds(250);

  bool operator==(const CmdScrollTabSettings&) const = default;
};

class CmdScrollTabSwitcher final {
 public:
  CmdScrollTabSwitcher() = default;
  explicit CmdScrollTabSwitcher(CmdScrollTabSettings settings);
  CmdScrollTabSwitcher(const CmdScrollTabSwitcher&) = delete;
  CmdScrollTabSwitcher& operator=(const CmdScrollTabSwitcher&) = delete;
  ~CmdScrollTabSwitcher() = default;

  CmdScrollTabDecision OnScroll(float x_offset,
                                float y_offset,
                                ui::ScrollEventPhase direct_phase,
                                ui::EventMomentumPhase momentum_phase,
                                base::TimeTicks event_time);
  [[nodiscard]] bool SetSettings(CmdScrollTabSettings settings);
  const CmdScrollTabSettings& settings() const { return settings_; }
  void Cancel();

 private:
  static bool AreSettingsValid(const CmdScrollTabSettings& settings);
  void ResetGesture();

  CmdScrollTabSettings settings_;
  float accumulated_offset_ = 0.0f;
  int direction_ = 0;
  bool active_ = false;
  bool switched_ = false;
  base::TimeTicks last_switch_time_;
  base::TimeTicks last_phase_less_event_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_NAVIGATION_CMD_SCROLL_TAB_SWITCHER_H_
