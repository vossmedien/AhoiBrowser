// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/quick_window.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::quick_window {
namespace {

TEST(QuickWindowPolicyTest, AcceptsOnlyUnmodifiedAltSpace) {
  EXPECT_TRUE(IsQuickWindowAccelerator(
      ui::Accelerator(ui::VKEY_SPACE, ui::EF_ALT_DOWN)));
  EXPECT_FALSE(IsQuickWindowAccelerator(
      ui::Accelerator(ui::VKEY_SPACE, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN)));
  EXPECT_FALSE(IsQuickWindowAccelerator(
      ui::Accelerator(ui::VKEY_SPACE, ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN)));
  EXPECT_FALSE(IsQuickWindowAccelerator(
      ui::Accelerator(ui::VKEY_SPACE, ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN)));
  EXPECT_FALSE(
      IsQuickWindowAccelerator(ui::Accelerator(ui::VKEY_SPACE, ui::EF_NONE)));
  EXPECT_FALSE(IsQuickWindowAccelerator(
      ui::Accelerator(ui::VKEY_RETURN, ui::EF_ALT_DOWN)));
}

TEST(QuickWindowPolicyTest, EnforcesActivationCooldownAtExactBoundary) {
  const base::TimeTicks previous = base::TimeTicks() + base::Milliseconds(1000);

  EXPECT_TRUE(ShouldActivateQuickWindow(base::TimeTicks(), previous));
  EXPECT_FALSE(ShouldActivateQuickWindow(
      previous, previous + kActivationCooldown - base::Milliseconds(1)));
  EXPECT_TRUE(
      ShouldActivateQuickWindow(previous, previous + kActivationCooldown));
}

TEST(QuickWindowPolicyTest, CentersFixedSizeBoundsOnAnchor) {
  EXPECT_EQ(gfx::Rect(240, 130, kWidth, kHeight),
            CalculateQuickWindowBounds(gfx::Rect(100, 50, 1000, 700)));
}

}  // namespace
}  // namespace ahoi::quick_window
