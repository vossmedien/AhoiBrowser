// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/navigation_surface_state.h"

#include <vector>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {

namespace {

class NavigationSurfaceStateTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::vector<NavigationSurfaceState::State> transitions;
};

TEST_F(NavigationSurfaceStateTest, ReasonsHavePriorityOverAutoHide) {
  NavigationSurfaceState state(base::BindRepeating(
      [](std::vector<NavigationSurfaceState::State>* transitions,
         NavigationSurfaceState::State new_state) {
        transitions->push_back(new_state);
      },
      &transitions));

  state.SetReasonActive(NavigationSurfaceState::Reason::kRevealNotchHover,
                        true);
  EXPECT_EQ(NavigationSurfaceState::State::kRevealing, state.state());
  state.FinishReveal();
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());

  state.SetReasonActive(NavigationSurfaceState::Reason::kKeyboardOrOmniboxFocus,
                        true);
  state.SetReasonActive(NavigationSurfaceState::Reason::kRevealNotchHover,
                        false);
  EXPECT_FALSE(state.IsAutoHideScheduled());
  EXPECT_FALSE(state.RequestHide());
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());

  state.SetReasonActive(NavigationSurfaceState::Reason::kKeyboardOrOmniboxFocus,
                        false);
  EXPECT_TRUE(state.IsAutoHideScheduled());
  EXPECT_TRUE(state.auto_hide_deadline().has_value());
}

TEST_F(NavigationSurfaceStateTest, AutoHideDeadlineHidesAfterConfiguredDelay) {
  constexpr base::TimeDelta kDelay = base::Milliseconds(250);
  NavigationSurfaceState state(
      base::RepeatingCallback<void(NavigationSurfaceState::State)>(),
      NavigationSurfaceState::Configuration{.auto_hide_delay = kDelay},
      task_environment.GetMockTickClock());
  state.SetReasonActive(NavigationSurfaceState::Reason::kToolbarBubble, true);
  state.FinishReveal();
  state.SetReasonActive(NavigationSurfaceState::Reason::kToolbarBubble, false);

  task_environment.FastForwardBy(kDelay - base::Milliseconds(1));
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());
  task_environment.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(NavigationSurfaceState::State::kHidden, state.state());
  EXPECT_FALSE(state.auto_hide_deadline().has_value());
}

TEST_F(NavigationSurfaceStateTest, FocusAndPromptsPreventHideUntilAllReleased) {
  NavigationSurfaceState state{
      base::RepeatingCallback<void(NavigationSurfaceState::State)>()};
  state.SetReasonActive(NavigationSurfaceState::Reason::kKeyboardOrOmniboxFocus,
                        true);
  state.FinishReveal();
  state.SetReasonActive(NavigationSurfaceState::Reason::kAuthPrompt, true);

  state.SetReasonActive(NavigationSurfaceState::Reason::kKeyboardOrOmniboxFocus,
                        false);
  EXPECT_FALSE(state.IsAutoHideScheduled());
  EXPECT_FALSE(state.RequestHide());

  state.SetReasonActive(NavigationSurfaceState::Reason::kAuthPrompt, false);
  EXPECT_TRUE(state.IsAutoHideScheduled());
  EXPECT_TRUE(state.RequestHide());
  EXPECT_EQ(NavigationSurfaceState::State::kHidden, state.state());
}

TEST_F(NavigationSurfaceStateTest, ReducedMotionSkipsRevealState) {
  NavigationSurfaceState state(
      base::RepeatingCallback<void(NavigationSurfaceState::State)>(),
      NavigationSurfaceState::Configuration{.reduced_motion = true},
      task_environment.GetMockTickClock());

  state.SetReasonActive(NavigationSurfaceState::Reason::kActiveDrag, true);
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());
  state.FinishReveal();
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());
}

TEST_F(NavigationSurfaceStateTest, EnablingReducedMotionCompletesReveal) {
  NavigationSurfaceState state{
      base::RepeatingCallback<void(NavigationSurfaceState::State)>()};
  state.SetReasonActive(NavigationSurfaceState::Reason::kFullscreen, true);
  EXPECT_EQ(NavigationSurfaceState::State::kRevealing, state.state());

  state.SetReducedMotion(true);
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());
}

TEST_F(NavigationSurfaceStateTest, DisablingAutoHideKeepsSurfaceVisible) {
  NavigationSurfaceState state(
      base::RepeatingCallback<void(NavigationSurfaceState::State)>(),
      NavigationSurfaceState::Configuration{},
      task_environment.GetMockTickClock());

  state.SetAutoHideEnabled(false);
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());
  EXPECT_FALSE(state.IsAutoHideScheduled());

  state.SetReasonActive(NavigationSurfaceState::Reason::kToolbarBubble, true);
  state.SetReasonActive(NavigationSurfaceState::Reason::kToolbarBubble, false);
  EXPECT_EQ(NavigationSurfaceState::State::kVisible, state.state());
  EXPECT_FALSE(state.IsAutoHideScheduled());
  EXPECT_FALSE(state.RequestHide());

  state.SetAutoHideEnabled(true);
  EXPECT_TRUE(state.IsAutoHideScheduled());
}

}  // namespace

}  // namespace ahoi
