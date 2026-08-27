// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/navigation_surface_controller.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"

namespace ahoi {
namespace {

class NavigationSurfaceControllerTest : public views::ViewsTestBase {};

TEST_F(NavigationSurfaceControllerTest,
       OnlyToolbarAnchoredNonModalBubbleKeepsNavigationVisible) {
  views::View root;
  views::View* const toolbar =
      root.AddChildView(std::make_unique<views::View>());
  views::View* const page = root.AddChildView(std::make_unique<views::View>());
  views::View* const toolbar_anchor =
      toolbar->AddChildView(std::make_unique<views::View>());
  views::View* const page_anchor =
      page->AddChildView(std::make_unique<views::View>());

  EXPECT_TRUE(
      IsToolbarAnchoredNavigationBubble(false, toolbar_anchor, toolbar));
  EXPECT_FALSE(IsToolbarAnchoredNavigationBubble(false, page_anchor, toolbar));
  EXPECT_FALSE(IsToolbarAnchoredNavigationBubble(false, nullptr, toolbar));
  EXPECT_FALSE(
      IsToolbarAnchoredNavigationBubble(true, toolbar_anchor, toolbar));
}

}  // namespace
}  // namespace ahoi
