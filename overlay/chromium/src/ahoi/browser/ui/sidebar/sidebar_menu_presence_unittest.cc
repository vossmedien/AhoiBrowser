// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_menu_presence.h"

#include <memory>
#include <optional>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {
namespace {

TEST(SidebarMenuPresenceTest, CountsOnlyLiveMenusAnchoredInsideTheRoot) {
  views::View root;
  views::View* const row = root.AddChildView(std::make_unique<views::View>());
  views::View outside;
  EXPECT_FALSE(IsSidebarMenuShowingWithin(&root));

  std::optional<ScopedSidebarMenu> menu(std::in_place, row);
  EXPECT_TRUE(IsSidebarMenuShowingWithin(&root));
  EXPECT_TRUE(IsSidebarMenuShowingWithin(row));
  EXPECT_FALSE(IsSidebarMenuShowingWithin(&outside));
  EXPECT_FALSE(IsSidebarMenuShowingWithin(nullptr));

  {
    ScopedSidebarMenu nested(&root);
    menu.reset();
    EXPECT_TRUE(IsSidebarMenuShowingWithin(&root));
    EXPECT_FALSE(IsSidebarMenuShowingWithin(row));
  }
  EXPECT_FALSE(IsSidebarMenuShowingWithin(&root));
}

TEST(SidebarMenuPresenceTest, StopsCountingWhenTheAnchorIsDestroyed) {
  views::View root;
  views::View* const row = root.AddChildView(std::make_unique<views::View>());
  ScopedSidebarMenu menu(row);
  ASSERT_TRUE(IsSidebarMenuShowingWithin(&root));

  std::unique_ptr<views::View> removed = root.RemoveChildViewT(row);
  removed.reset();
  EXPECT_EQ(nullptr, menu.anchor());
  EXPECT_FALSE(IsSidebarMenuShowingWithin(&root));
}

}  // namespace
}  // namespace ahoi::sidebar
