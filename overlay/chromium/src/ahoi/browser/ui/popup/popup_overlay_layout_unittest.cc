// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/popup/popup_overlay_layout.h"

#include "ahoi/browser/ui/visual_style.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::popup_ui {

TEST(PopupOverlayLayoutTest, CentersDefaultCardAndExternalRail) {
  const PopupOverlayLayout layout =
      CalculatePopupOverlayLayout(gfx::Rect(0, 0, 1200, 800), gfx::Size());

  EXPECT_EQ(
      gfx::Size(visual_style::kPopupCardWidth, visual_style::kPopupCardHeight),
      layout.card_bounds.size());
  EXPECT_EQ(visual_style::kPopupActionRailWidth,
            layout.action_rail_bounds.width());
  EXPECT_EQ(visual_style::kPopupActionRailHeight,
            layout.action_rail_bounds.height());
  EXPECT_EQ(layout.card_bounds.right() + visual_style::kPopupActionRailGap,
            layout.action_rail_bounds.x());
  EXPECT_EQ((1200 - (visual_style::kPopupCardWidth +
                     visual_style::kPopupActionRailGap +
                     visual_style::kPopupActionRailWidth)) /
                2,
            layout.card_bounds.x());
}

TEST(PopupOverlayLayoutTest, RespectsAndClampsWindowFeatureSize) {
  const PopupOverlayLayout requested = CalculatePopupOverlayLayout(
      gfx::Rect(10, 20, 1000, 700), gfx::Size(460, 340));
  EXPECT_EQ(gfx::Size(460, 340), requested.card_bounds.size());

  const PopupOverlayLayout too_small = CalculatePopupOverlayLayout(
      gfx::Rect(0, 0, 1000, 700), gfx::Size(100, 80));
  EXPECT_EQ(gfx::Size(visual_style::kPopupCardMinimumWidth,
                      visual_style::kPopupCardMinimumHeight),
            too_small.card_bounds.size());

  const PopupOverlayLayout too_large = CalculatePopupOverlayLayout(
      gfx::Rect(0, 0, 500, 300), gfx::Size(2000, 2000));
  EXPECT_LE(too_large.action_rail_bounds.right(), 500);
  EXPECT_LE(too_large.card_bounds.bottom(), 300);
}

TEST(PopupOverlayLayoutTest, KeepsTinyPaneGeometryNonNegative) {
  const PopupOverlayLayout layout =
      CalculatePopupOverlayLayout(gfx::Rect(4, 8, 32, 24), gfx::Size());

  EXPECT_GE(layout.card_bounds.width(), 0);
  EXPECT_GE(layout.card_bounds.height(), 0);
  EXPECT_GE(layout.card_bounds.x(), 4);
  EXPECT_GE(layout.card_bounds.y(), 8);
  EXPECT_LE(layout.card_bounds.right(), 36);
  EXPECT_LE(layout.card_bounds.bottom(), 32);
  EXPECT_GE(layout.action_rail_bounds.x(), 4);
  EXPECT_LE(layout.action_rail_bounds.right(), 36);
  EXPECT_LE(layout.action_rail_bounds.bottom(), 32);
}

}  // namespace ahoi::popup_ui
