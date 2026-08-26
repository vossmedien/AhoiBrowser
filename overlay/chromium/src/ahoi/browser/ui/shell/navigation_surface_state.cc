// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/navigation_surface_state.h"

#include <utility>

#include "base/check.h"
#include "base/location.h"
#include "base/time/default_tick_clock.h"

namespace ahoi {

NavigationSurfaceState::NavigationSurfaceState(
    StateChangedCallback state_changed,
    const base::TickClock* tick_clock)
    : NavigationSurfaceState(std::move(state_changed),
                             Configuration(),
                             tick_clock) {}

NavigationSurfaceState::NavigationSurfaceState(
    StateChangedCallback state_changed,
    Configuration configuration,
    const base::TickClock* tick_clock)
    : state_changed_(std::move(state_changed)),
      auto_hide_delay_(configuration.auto_hide_delay),
      auto_hide_enabled_(configuration.auto_hide_enabled),
      reduced_motion_(configuration.reduced_motion),
      tick_clock_(tick_clock ? tick_clock
                             : base::DefaultTickClock::GetInstance()),
      auto_hide_timer_(tick_clock_) {
  CHECK(!auto_hide_delay_.is_negative());
}

NavigationSurfaceState::~NavigationSurfaceState() = default;

void NavigationSurfaceState::SetReasonActive(Reason reason, bool active) {
  CHECK_LT(ReasonIndex(reason), ReasonIndex(Reason::kCount));

  bool& reason_active = active_reasons_[ReasonIndex(reason)];
  if (reason_active == active) {
    return;
  }
  reason_active = active;

  if (active) {
    CancelAutoHide();
    if (state_ == State::kHidden) {
      TransitionTo(reduced_motion_ ? State::kVisible : State::kRevealing);
    }
    return;
  }

  if (HasActiveReason()) {
    return;
  }

  if (state_ == State::kRevealing) {
    TransitionTo(State::kHidden);
  } else if (state_ == State::kVisible) {
    ScheduleAutoHide();
  }
}

void NavigationSurfaceState::FinishReveal() {
  if (state_ != State::kRevealing) {
    return;
  }
  TransitionTo(HasActiveReason() ? State::kVisible : State::kHidden);
}

void NavigationSurfaceState::SetReducedMotion(bool reduced_motion) {
  if (reduced_motion_ == reduced_motion) {
    return;
  }
  reduced_motion_ = reduced_motion;
  if (reduced_motion_ && state_ == State::kRevealing) {
    FinishReveal();
  }
}

void NavigationSurfaceState::SetAutoHideDelay(base::TimeDelta auto_hide_delay) {
  CHECK(!auto_hide_delay.is_negative());
  if (auto_hide_delay_ == auto_hide_delay) {
    return;
  }
  auto_hide_delay_ = auto_hide_delay;
  if (auto_hide_timer_.IsRunning()) {
    ScheduleAutoHide();
  }
}

void NavigationSurfaceState::SetAutoHideEnabled(bool auto_hide_enabled) {
  if (auto_hide_enabled_ == auto_hide_enabled) {
    return;
  }
  auto_hide_enabled_ = auto_hide_enabled;
  CancelAutoHide();
  if (!auto_hide_enabled_) {
    if (state_ != State::kVisible) {
      TransitionTo(State::kVisible);
    }
    return;
  }
  if (!HasActiveReason() && state_ == State::kVisible) {
    ScheduleAutoHide();
  }
}

bool NavigationSurfaceState::RequestHide() {
  if (!auto_hide_enabled_ || HasActiveReason()) {
    return false;
  }
  CancelAutoHide();
  if (state_ != State::kHidden) {
    TransitionTo(State::kHidden);
  }
  return true;
}

bool NavigationSurfaceState::IsReasonActive(Reason reason) const {
  CHECK_LT(ReasonIndex(reason), ReasonIndex(Reason::kCount));
  return active_reasons_[ReasonIndex(reason)];
}

bool NavigationSurfaceState::HasActiveReason() const {
  for (bool active : active_reasons_) {
    if (active) {
      return true;
    }
  }
  return false;
}

void NavigationSurfaceState::TransitionTo(State state) {
  if (state_ == state) {
    return;
  }
  state_ = state;
  if (state_ != State::kVisible) {
    CancelAutoHide();
  }
  if (state_changed_) {
    state_changed_.Run(state_);
  }
}

void NavigationSurfaceState::ScheduleAutoHide() {
  if (!auto_hide_enabled_ || HasActiveReason() || state_ != State::kVisible) {
    return;
  }

  auto_hide_deadline_ = tick_clock_->NowTicks() + auto_hide_delay_;
  auto_hide_timer_.Start(FROM_HERE, auto_hide_delay_, this,
                         &NavigationSurfaceState::OnAutoHideTimer);
}

void NavigationSurfaceState::CancelAutoHide() {
  auto_hide_timer_.Stop();
  auto_hide_deadline_.reset();
}

void NavigationSurfaceState::OnAutoHideTimer() {
  auto_hide_deadline_.reset();
  RequestHide();
}

}  // namespace ahoi
