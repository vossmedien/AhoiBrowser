// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/cmd_scroll_tab_switcher.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(CmdScrollTabSwitcherTest, SwitchesOncePerTrackpadGesture) {
  CmdScrollTabSwitcher switcher;
  const base::TimeTicks start = base::TimeTicks() + base::Seconds(1);
  EXPECT_EQ(switcher.OnScroll(0, 10, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::MAY_BEGIN, start),
            CmdScrollTabDecision::kPreviewNext);
  EXPECT_EQ(switcher.OnScroll(0, 15, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::NONE,
                              start + base::Milliseconds(20)),
            CmdScrollTabDecision::kSwitchNext);
  EXPECT_EQ(switcher.OnScroll(0, 25, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::NONE,
                              start + base::Milliseconds(30)),
            CmdScrollTabDecision::kConsume);
  EXPECT_EQ(switcher.OnScroll(0, 5, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::END,
                              start + base::Milliseconds(40)),
            CmdScrollTabDecision::kConsume);
  EXPECT_EQ(switcher.OnScroll(0, -30, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::MAY_BEGIN,
                              start + base::Seconds(1)),
            CmdScrollTabDecision::kSwitchPrevious);
}

TEST(CmdScrollTabSwitcherTest, PhaseLessMouseWheelAccumulatesToThreshold) {
  CmdScrollTabSwitcher switcher;
  const base::TimeTicks start = base::TimeTicks() + base::Seconds(1);
  EXPECT_EQ(switcher.OnScroll(0, -12, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::NONE, start),
            CmdScrollTabDecision::kPreviewPrevious);
  EXPECT_EQ(switcher.OnScroll(0, -12, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::NONE,
                              start + base::Milliseconds(20)),
            CmdScrollTabDecision::kSwitchPrevious);
}

TEST(CmdScrollTabSwitcherTest, IgnoresMomentumWithoutDirectGesture) {
  CmdScrollTabSwitcher switcher;
  EXPECT_EQ(switcher.OnScroll(0, 80, ui::ScrollEventPhase::kNone,
                              ui::EventMomentumPhase::INERTIAL_UPDATE,
                              base::TimeTicks() + base::Seconds(1)),
            CmdScrollTabDecision::kNone);
}

TEST(CmdScrollTabSwitcherTest, CancelResetsGesture) {
  CmdScrollTabSwitcher switcher;
  const base::TimeTicks start = base::TimeTicks() + base::Seconds(1);
  EXPECT_EQ(switcher.OnScroll(30, 0, ui::ScrollEventPhase::kBegan,
                              ui::EventMomentumPhase::NONE, start),
            CmdScrollTabDecision::kSwitchNext);
  switcher.Cancel();
  EXPECT_EQ(
      switcher.OnScroll(0, 30, ui::ScrollEventPhase::kNone,
                        ui::EventMomentumPhase::NONE, start + base::Seconds(1)),
      CmdScrollTabDecision::kSwitchNext);
}

TEST(CmdScrollTabSwitcherTest, DisabledSettingNeverConsumesAndInvalidFails) {
  CmdScrollTabSwitcher switcher;
  CmdScrollTabSettings disabled;
  disabled.enabled = false;
  EXPECT_TRUE(switcher.SetSettings(disabled));
  EXPECT_EQ(switcher.OnScroll(0, 100, ui::ScrollEventPhase::kBegan,
                              ui::EventMomentumPhase::NONE,
                              base::TimeTicks() + base::Seconds(1)),
            CmdScrollTabDecision::kNone);

  CmdScrollTabSettings invalid;
  invalid.threshold = 0.0f;
  EXPECT_FALSE(switcher.SetSettings(invalid));
  EXPECT_EQ(disabled, switcher.settings());
}

TEST(CmdScrollTabSwitcherTest, RateLimitsSeparateGestures) {
  CmdScrollTabSwitcher switcher;
  const base::TimeTicks start = base::TimeTicks() + base::Seconds(1);
  EXPECT_EQ(switcher.OnScroll(0, 30, ui::ScrollEventPhase::kBegan,
                              ui::EventMomentumPhase::NONE, start),
            CmdScrollTabDecision::kSwitchNext);
  EXPECT_EQ(switcher.OnScroll(0, 30, ui::ScrollEventPhase::kBegan,
                              ui::EventMomentumPhase::NONE,
                              start + base::Milliseconds(100)),
            CmdScrollTabDecision::kConsume);
  EXPECT_EQ(switcher.OnScroll(0, 30, ui::ScrollEventPhase::kBegan,
                              ui::EventMomentumPhase::NONE,
                              start + base::Milliseconds(300)),
            CmdScrollTabDecision::kSwitchNext);
}

}  // namespace
}  // namespace ahoi
