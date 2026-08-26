// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_WORKSPACE_SWIPE_TRACKER_H_
#define AHOI_BROWSER_NAVIGATION_WORKSPACE_SWIPE_TRACKER_H_

namespace ahoi {

struct WorkspaceSwipeSettings {
  bool enabled = true;
  bool reverse_direction = false;
  float threshold = 80.0f;
  float axis_bias = 1.3f;
  float reject_vertical_distance = 24.0f;

  bool operator==(const WorkspaceSwipeSettings&) const = default;
};

enum class WorkspaceSwipeDecision {
  kNone = 0,
  kSwitchPrevious = 1,
  kSwitchNext = 2,
  kConsume = 3,
};

// Pure gesture state machine shared by the macOS event adapter and unit tests.
// It commits at most one workspace transition per physical scroll sequence and
// rejects diagonal vertical scrolling before it reaches the threshold. macOS
// reports an END for the direct phase before it knows whether momentum follows,
// so direct-end state is retained until momentum ends or the next begin/cancel.
class WorkspaceSwipeTracker {
 public:
  WorkspaceSwipeTracker();
  explicit WorkspaceSwipeTracker(WorkspaceSwipeSettings settings);
  WorkspaceSwipeTracker(const WorkspaceSwipeTracker&) = delete;
  WorkspaceSwipeTracker& operator=(const WorkspaceSwipeTracker&) = delete;
  ~WorkspaceSwipeTracker();

  [[nodiscard]] bool SetSettings(WorkspaceSwipeSettings settings);
  const WorkspaceSwipeSettings& settings() const { return settings_; }
  bool active() const { return active_; }
  bool saw_momentum() const { return saw_momentum_; }

  void OnGestureBegin();
  WorkspaceSwipeDecision OnGestureUpdate(float horizontal_delta,
                                         float vertical_delta);
  // Momentum never initiates a workspace switch. It is consumed only after a
  // direct gesture committed, preventing an inertial stream from advancing.
  WorkspaceSwipeDecision OnMomentum();
  WorkspaceSwipeDecision OnGestureEnd();
  void OnGestureCancel();

 private:
  WorkspaceSwipeDecision EvaluateDelta(float horizontal_delta,
                                       float vertical_delta);
  void Reset();
  static bool AreSettingsValid(const WorkspaceSwipeSettings& settings);

  WorkspaceSwipeSettings settings_;
  float accumulated_x_ = 0.0f;
  float accumulated_y_ = 0.0f;
  bool active_ = false;
  bool switched_ = false;
  bool rejected_ = false;
  bool saw_momentum_ = false;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_NAVIGATION_WORKSPACE_SWIPE_TRACKER_H_
