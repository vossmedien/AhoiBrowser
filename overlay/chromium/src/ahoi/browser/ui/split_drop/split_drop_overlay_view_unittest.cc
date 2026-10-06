// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_overlay_view.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::split_drop {
namespace {

TEST(SplitDropOverlayViewTest, HiddenAndEventTransparentOutsideActiveDrag) {
  std::unique_ptr<SplitDropOverlayView> overlay = CreateSplitDropOverlayView();
  EXPECT_FALSE(overlay->GetVisible());
  EXPECT_FALSE(overlay->GetCanProcessEventsWithinSubtree());
  EXPECT_FALSE(overlay->intent_for_testing().has_value());

  DropIntent intent;
  intent.highlight_bounds = gfx::Rect(10, 20, 100, 80);
  overlay->SetIntent(intent);
  EXPECT_TRUE(overlay->GetVisible());
  EXPECT_TRUE(overlay->intent_for_testing().has_value());

  // Leaving a target clears the intent but keeps the drag presentation; only
  // the end of the drag hides the overlay and makes it event-transparent.
  overlay->ClearIntent();
  EXPECT_TRUE(overlay->GetVisible());
  EXPECT_FALSE(overlay->intent_for_testing().has_value());

  overlay->EndDragPresentation();
  EXPECT_FALSE(overlay->GetVisible());
  EXPECT_FALSE(overlay->GetCanProcessEventsWithinSubtree());
  EXPECT_FALSE(overlay->intent_for_testing().has_value());

  // Drag completion and native drag cancellation can both report cleanup.
  // The second call must remain a no-op rather than resurrecting stale intent.
  overlay->EndDragPresentation();
  overlay->ClearIntent();
  EXPECT_FALSE(overlay->GetVisible());
  EXPECT_FALSE(overlay->intent_for_testing().has_value());
}

}  // namespace
}  // namespace ahoi::split_drop
