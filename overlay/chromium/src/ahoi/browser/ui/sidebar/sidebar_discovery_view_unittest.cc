// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi::sidebar {
namespace {

class SidebarDiscoveryViewTest : public views::ViewsTestBase {
 protected:
  std::unique_ptr<SidebarDiscoveryView> CreateView(int* close_count) {
    return std::make_unique<SidebarDiscoveryView>(
        &model_, base::BindRepeating([](const CommandItem&) { return true; }),
        base::BindRepeating([](SessionID) { return true; }),
        base::BindRepeating(
            [](int* count) {
              ASSERT_NE(count, nullptr);
              ++*count;
            },
            close_count));
  }

  CommandService command_service_;
  SidebarDiscoveryModel model_{&command_service_, nullptr};
};

TEST_F(SidebarDiscoveryViewTest, TabAndShiftTabUseNativeFocusTraversal) {
  int close_count = 0;
  std::unique_ptr<SidebarDiscoveryView> view = CreateView(&close_count);
  views::Textfield* const field = view->search_field_for_testing();

  EXPECT_FALSE(view->HandleKeyEvent(
      field,
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_FALSE(view->HandleKeyEvent(
      field, ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_TAB,
                          ui::EF_SHIFT_DOWN)));
  EXPECT_EQ(close_count, 0);
}

TEST_F(SidebarDiscoveryViewTest, EmptyEscapeDefersClosingUntilDispatchEnds) {
  int close_count = 0;
  std::unique_ptr<SidebarDiscoveryView> view = CreateView(&close_count);
  views::Textfield* const field = view->search_field_for_testing();

  EXPECT_TRUE(view->HandleKeyEvent(
      field,
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_EQ(close_count, 0);

  task_environment()->RunUntilIdle();
  EXPECT_EQ(close_count, 1);
}

TEST_F(SidebarDiscoveryViewTest, FirstEscapeClearsQueryWithoutClosing) {
  int close_count = 0;
  std::unique_ptr<SidebarDiscoveryView> view = CreateView(&close_count);
  views::Textfield* const field = view->search_field_for_testing();
  field->SetText(u"project");

  EXPECT_TRUE(view->HandleKeyEvent(
      field,
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_TRUE(field->GetText().empty());
  EXPECT_EQ(close_count, 0);
}

}  // namespace
}  // namespace ahoi::sidebar
