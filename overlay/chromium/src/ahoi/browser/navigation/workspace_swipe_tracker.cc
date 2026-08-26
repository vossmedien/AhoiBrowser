// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_swipe_tracker.h"

#include <cmath>
#include <utility>

#include "base/check.h"

namespace ahoi {

WorkspaceSwipeTracker::WorkspaceSwipeTracker() = default;

WorkspaceSwipeTracker::WorkspaceSwipeTracker(WorkspaceSwipeSettings settings)
    : settings_(std::move(settings)) {
  CHECK(AreSettingsValid(settings_));
}

WorkspaceSwipeTracker::~WorkspaceSwipeTracker() = default;

bool WorkspaceSwipeTracker::SetSettings(WorkspaceSwipeSettings settings) {
  if (!AreSettingsValid(settings)) {
    return false;
  }
  settings_ = std::move(settings);
  Reset();
  return true;
}

void WorkspaceSwipeTracker::OnGestureBegin() {
  Reset();
  active_ = true;
}

WorkspaceSwipeDecision WorkspaceSwipeTracker::OnGestureUpdate(
    float horizontal_delta,
    float vertical_delta) {
  return EvaluateDelta(horizontal_delta, vertical_delta);
}

WorkspaceSwipeDecision WorkspaceSwipeTracker::OnMomentum() {
  if (!active_) {
    return WorkspaceSwipeDecision::kNone;
  }
  saw_momentum_ = true;
  return switched_ ? WorkspaceSwipeDecision::kConsume
                   : WorkspaceSwipeDecision::kNone;
}

WorkspaceSwipeDecision WorkspaceSwipeTracker::OnGestureEnd() {
  if (!active_) {
    return WorkspaceSwipeDecision::kNone;
  }
  const WorkspaceSwipeDecision decision = switched_
                                              ? WorkspaceSwipeDecision::kConsume
                                              : WorkspaceSwipeDecision::kNone;
  if (saw_momentum_) {
    Reset();
  }
  return decision;
}

void WorkspaceSwipeTracker::OnGestureCancel() {
  Reset();
}

WorkspaceSwipeDecision WorkspaceSwipeTracker::EvaluateDelta(
    float horizontal_delta,
    float vertical_delta) {
  if (!settings_.enabled || !active_ || rejected_) {
    return WorkspaceSwipeDecision::kNone;
  }
  if (switched_) {
    return WorkspaceSwipeDecision::kConsume;
  }
  if (!std::isfinite(horizontal_delta) || !std::isfinite(vertical_delta)) {
    rejected_ = true;
    return WorkspaceSwipeDecision::kNone;
  }

  accumulated_x_ += horizontal_delta;
  accumulated_y_ += std::abs(vertical_delta);
  const float horizontal_distance = std::abs(accumulated_x_);

  if (accumulated_y_ > settings_.reject_vertical_distance &&
      accumulated_y_ * settings_.axis_bias > horizontal_distance) {
    rejected_ = true;
    return WorkspaceSwipeDecision::kNone;
  }
  if (horizontal_distance < settings_.threshold ||
      horizontal_distance < accumulated_y_ * settings_.axis_bias) {
    return WorkspaceSwipeDecision::kNone;
  }

  switched_ = true;
  const bool physical_next = accumulated_x_ < 0.0f;
  const bool switch_next = physical_next != settings_.reverse_direction;
  return switch_next ? WorkspaceSwipeDecision::kSwitchNext
                     : WorkspaceSwipeDecision::kSwitchPrevious;
}

void WorkspaceSwipeTracker::Reset() {
  accumulated_x_ = 0.0f;
  accumulated_y_ = 0.0f;
  active_ = false;
  switched_ = false;
  rejected_ = false;
  saw_momentum_ = false;
}

bool WorkspaceSwipeTracker::AreSettingsValid(
    const WorkspaceSwipeSettings& settings) {
  return std::isfinite(settings.threshold) && settings.threshold > 0.0f &&
         std::isfinite(settings.axis_bias) && settings.axis_bias >= 1.0f &&
         std::isfinite(settings.reject_vertical_distance) &&
         settings.reject_vertical_distance >= 0.0f;
}

}  // namespace ahoi
