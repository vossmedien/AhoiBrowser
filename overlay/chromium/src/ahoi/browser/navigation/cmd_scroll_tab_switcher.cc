// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/cmd_scroll_tab_switcher.h"

#include <cmath>
#include <utility>

#include "base/check.h"

namespace ahoi {

namespace {
constexpr base::TimeDelta kPhaseLessSequenceTimeout = base::Milliseconds(400);
}

CmdScrollTabSwitcher::CmdScrollTabSwitcher(CmdScrollTabSettings settings)
    : settings_(std::move(settings)) {
  CHECK(AreSettingsValid(settings_));
}

CmdScrollTabDecision CmdScrollTabSwitcher::OnScroll(
    float x_offset,
    float y_offset,
    ui::ScrollEventPhase direct_phase,
    ui::EventMomentumPhase momentum_phase,
    base::TimeTicks event_time) {
  if (!settings_.enabled) {
    ResetGesture();
    return CmdScrollTabDecision::kNone;
  }
  const bool begins = direct_phase == ui::ScrollEventPhase::kBegan ||
                      momentum_phase == ui::EventMomentumPhase::MAY_BEGIN;
  const bool ends = direct_phase == ui::ScrollEventPhase::kEnd ||
                    momentum_phase == ui::EventMomentumPhase::END ||
                    momentum_phase == ui::EventMomentumPhase::BLOCKED;
  if (begins) {
    active_ = true;
    switched_ = false;
    accumulated_offset_ = 0.0f;
    direction_ = 0;
  }

  // Cmd+scroll on a traditional mouse is commonly delivered as independent
  // phase-less wheel events. Treat each such event as its own request. A
  // trackpad stream has MAY_BEGIN/END and is switched only once per gesture.
  const bool phase_less = direct_phase == ui::ScrollEventPhase::kNone &&
                          momentum_phase == ui::EventMomentumPhase::NONE;
  if (phase_less && !last_phase_less_event_.is_null() &&
      event_time - last_phase_less_event_ > kPhaseLessSequenceTimeout) {
    accumulated_offset_ = 0.0f;
    direction_ = 0;
  }
  if (phase_less) {
    last_phase_less_event_ = event_time;
  }
  if (!active_ && !phase_less) {
    return CmdScrollTabDecision::kNone;
  }

  const bool momentum_only =
      momentum_phase == ui::EventMomentumPhase::BEGAN ||
      momentum_phase == ui::EventMomentumPhase::INERTIAL_UPDATE;
  if (momentum_only && !switched_) {
    return CmdScrollTabDecision::kNone;
  }

  if (active_ && switched_) {
    if (ends) {
      ResetGesture();
    }
    return CmdScrollTabDecision::kConsume;
  }

  const float dominant_offset =
      std::abs(x_offset) >= std::abs(y_offset) ? x_offset : y_offset;
  if (dominant_offset == 0.0f) {
    if (ends) {
      ResetGesture();
      return CmdScrollTabDecision::kConsume;
    }
    return CmdScrollTabDecision::kNone;
  }

  const int direction = dominant_offset > 0.0f ? 1 : -1;
  if (direction_ != 0 && direction_ != direction) {
    accumulated_offset_ = 0.0f;
  }
  direction_ = direction;
  accumulated_offset_ += std::abs(dominant_offset);

  if (accumulated_offset_ < settings_.threshold) {
    return direction > 0 ? CmdScrollTabDecision::kPreviewNext
                         : CmdScrollTabDecision::kPreviewPrevious;
  }

  const bool rate_limited =
      !last_switch_time_.is_null() &&
      event_time - last_switch_time_ < settings_.minimum_interval;
  if (rate_limited) {
    switched_ = active_;
    if (ends) {
      ResetGesture();
    }
    return CmdScrollTabDecision::kConsume;
  }

  last_switch_time_ = event_time;
  switched_ = active_;
  if (phase_less || ends) {
    accumulated_offset_ = 0.0f;
    direction_ = 0;
  }
  if (ends) {
    active_ = false;
  }
  return direction > 0 ? CmdScrollTabDecision::kSwitchNext
                       : CmdScrollTabDecision::kSwitchPrevious;
}

bool CmdScrollTabSwitcher::SetSettings(CmdScrollTabSettings settings) {
  if (!AreSettingsValid(settings)) {
    return false;
  }
  settings_ = std::move(settings);
  ResetGesture();
  last_switch_time_ = base::TimeTicks();
  last_phase_less_event_ = base::TimeTicks();
  return true;
}

void CmdScrollTabSwitcher::ResetGesture() {
  accumulated_offset_ = 0.0f;
  direction_ = 0;
  active_ = false;
  switched_ = false;
}

void CmdScrollTabSwitcher::Cancel() {
  ResetGesture();
  last_phase_less_event_ = base::TimeTicks();
}

bool CmdScrollTabSwitcher::AreSettingsValid(
    const CmdScrollTabSettings& settings) {
  return std::isfinite(settings.threshold) && settings.threshold > 0.0f &&
         !settings.minimum_interval.is_negative();
}

}  // namespace ahoi
