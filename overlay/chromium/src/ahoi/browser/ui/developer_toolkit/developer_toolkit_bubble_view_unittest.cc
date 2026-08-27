// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_bubble_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class DeveloperToolkitBubbleViewTest : public views::ViewsTestBase {};

TEST_F(DeveloperToolkitBubbleViewTest,
       SavedPasswordsUsesDedicatedUpstreamRoute) {
  int execute_count = 0;
  int password_manager_count = 0;
  int other_open_count = 0;
  auto view = std::make_unique<DeveloperToolkitBubbleView>(
      u"https://example.test",
      developer_toolkit_prefs::ToolbarVisibility{
          .cookie = true, .cache = true, .toolkit = true},
      base::BindRepeating(
          [](int* count, DeveloperAction action) {
            ++*count;
            return DeveloperActionResult{action,
                                         DeveloperActionStatus::kUnavailable};
          },
          &execute_count),
      base::BindRepeating([](BrowsingDataClearOptions,
                             BrowsingDataClearCallback) { return false; }),
      base::BindRepeating(
          [](developer_toolkit_prefs::ToolbarVisibility) { return true; }),
      base::BindRepeating([](int* count) { ++*count; }, &other_open_count),
      base::BindRepeating([](int* count) { ++*count; },
                          &password_manager_count),
      base::BindRepeating([](int* count) { ++*count; }, &other_open_count),
      base::BindRepeating([](int* count) { ++*count; }, &other_open_count));

  ASSERT_TRUE(view->password_manager_button_for_testing());
  views::test::ButtonTestApi(view->password_manager_button_for_testing())
      .NotifyDefaultMouseClick();

  EXPECT_EQ(1, password_manager_count);
  EXPECT_EQ(0, execute_count);
  EXPECT_EQ(0, other_open_count);
}

}  // namespace
}  // namespace ahoi
