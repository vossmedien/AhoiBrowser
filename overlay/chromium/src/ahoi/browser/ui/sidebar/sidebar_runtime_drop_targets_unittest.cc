// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

class SidebarRuntimeDropTargetsTest : public views::ViewsTestBase {};

TEST_F(SidebarRuntimeDropTargetsTest,
       NewGroupFadeLayerIsTransparentAndBorderless) {
  auto target = CreateNewGroupDropTargetViewForTesting(u"Neue Gruppe");

  ASSERT_NE(nullptr, target->layer());
  EXPECT_FALSE(target->layer()->fills_bounds_opaquely());
  EXPECT_EQ(nullptr, target->GetBorder());
  EXPECT_FALSE(target->GetVisible());
  EXPECT_FALSE(target->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(visual_style::kSidebarActionCellHeight,
            target->GetPreferredSize().height());

  SetNewGroupDropTargetVisible(target.get(), true);
  EXPECT_TRUE(target->GetVisible());
  EXPECT_TRUE(target->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(nullptr, target->GetBorder());
  EXPECT_EQ(visual_style::kSidebarActionCellHeight,
            target->GetPreferredSize().height());

  SetNewGroupDropTargetVisible(target.get(), false);
  // The fade may remain paint-visible until its animation finishes, but it
  // must stop intercepting the workspace pill immediately on drop/cancel.
  EXPECT_FALSE(target->GetCanProcessEventsWithinSubtree());
  EXPECT_EQ(visual_style::kSidebarActionCellHeight,
            target->GetPreferredSize().height());
}

TEST_F(SidebarRuntimeDropTargetsTest,
       NewGroupGetsStableBoundsBeforeNativeDragLoop) {
  views::View host;
  host.SetBounds(0, 0, 320, 48);
  auto target = CreateNewGroupDropTargetViewForTesting(u"Neue Gruppe");
  views::View* const target_ptr = target.get();
  host.AddChildView(std::move(target));

  ASSERT_TRUE(target_ptr->bounds().IsEmpty());
  SetNewGroupDropTargetVisible(target_ptr, true);

  EXPECT_EQ(host.GetLocalBounds(), target_ptr->bounds());
  EXPECT_TRUE(target_ptr->GetCanProcessEventsWithinSubtree());
}

}  // namespace ahoi::sidebar
