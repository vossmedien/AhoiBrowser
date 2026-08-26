// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SHELL_NAVIGATION_SURFACE_STATE_H_
#define AHOI_BROWSER_UI_SHELL_NAVIGATION_SURFACE_STATE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace ahoi {

// Browser-window-local state machine for the Arc-like floating navigation
// surface. It deliberately has no View/Widget dependency: a shell renderer
// can animate the state transitions while browser controllers only describe
// why the surface must stay available.
class NavigationSurfaceState final {
 public:
  enum class State : uint8_t {
    kHidden,
    kRevealing,
    kVisible,
  };

  // Reasons are independent leases. The surface remains available until the
  // last active reason is released. This prevents a focus loss or a drag-end
  // event from hiding a toolbar that is still needed by another surface.
  enum class Reason : uint8_t {
    kRevealNotchHover,
    kKeyboardOrOmniboxFocus,
    kToolbarBubble,
    kPermissionPrompt,
    kAuthPrompt,
    kActiveDrag,
    kFullscreen,
    kCount,
  };

  struct Configuration {
    base::TimeDelta auto_hide_delay = base::Seconds(2);
    bool auto_hide_enabled = true;
    bool reduced_motion = false;
  };

  using StateChangedCallback = base::RepeatingCallback<void(State)>;

  explicit NavigationSurfaceState(StateChangedCallback state_changed,
                                  const base::TickClock* tick_clock = nullptr);
  NavigationSurfaceState(StateChangedCallback state_changed,
                         Configuration configuration,
                         const base::TickClock* tick_clock = nullptr);
  NavigationSurfaceState(const NavigationSurfaceState&) = delete;
  NavigationSurfaceState& operator=(const NavigationSurfaceState&) = delete;
  ~NavigationSurfaceState();

  // Acquires or releases a reason lease. Acquiring the first lease starts a
  // reveal (or jumps straight to visible for reduced motion); releasing the
  // last lease arms the configured auto-hide deadline.
  void SetReasonActive(Reason reason, bool active);

  // Called by the renderer after its reveal animation has completed. If the
  // lease was released during the animation, the surface is hidden instead.
  void FinishReveal();

  // Changes motion policy at runtime. Enabling reduced motion completes an
  // in-flight reveal immediately and never changes the lease semantics.
  void SetReducedMotion(bool reduced_motion);

  // Updates the timeout used after the final reason is released. A running
  // deadline is restarted so callers can change this from settings safely.
  void SetAutoHideDelay(base::TimeDelta auto_hide_delay);

  // Disabling automatic hiding makes the navigation row persistently visible.
  // Explicit interaction leases continue to work and re-enabling the setting
  // arms a normal deadline once no lease remains.
  void SetAutoHideEnabled(bool auto_hide_enabled);

  // Attempts to hide without changing any reason leases. Returns false when
  // auto-hide is disabled or a focus, prompt, drag, fullscreen, or other
  // active reason still owns the surface.
  bool RequestHide();

  State state() const { return state_; }
  bool IsReasonActive(Reason reason) const;
  bool HasActiveReason() const;
  bool IsAutoHideScheduled() const { return auto_hide_timer_.IsRunning(); }
  std::optional<base::TimeTicks> auto_hide_deadline() const {
    return auto_hide_deadline_;
  }
  bool reduced_motion() const { return reduced_motion_; }
  bool auto_hide_enabled() const { return auto_hide_enabled_; }

 private:
  static constexpr size_t kReasonCount = static_cast<size_t>(Reason::kCount);

  static constexpr size_t ReasonIndex(Reason reason) {
    return static_cast<size_t>(reason);
  }

  void TransitionTo(State state);
  void ScheduleAutoHide();
  void CancelAutoHide();
  void OnAutoHideTimer();

  StateChangedCallback state_changed_;
  State state_ = State::kHidden;
  std::array<bool, kReasonCount> active_reasons_ = {};
  base::TimeDelta auto_hide_delay_;
  bool auto_hide_enabled_ = true;
  bool reduced_motion_ = false;
  const raw_ptr<const base::TickClock> tick_clock_;
  base::OneShotTimer auto_hide_timer_;
  std::optional<base::TimeTicks> auto_hide_deadline_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_SHELL_NAVIGATION_SURFACE_STATE_H_
