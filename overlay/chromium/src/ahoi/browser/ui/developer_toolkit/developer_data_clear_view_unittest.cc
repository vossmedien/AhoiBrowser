// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_data_clear_view.h"

#include <memory>
#include <optional>

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/combobox_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class DeveloperDataClearViewTest : public views::ViewsTestBase {};

TEST_F(DeveloperDataClearViewTest, ToolkitButtonsUseCompactFocusableStates) {
  auto button = std::make_unique<DeveloperToolkitButton>(
      base::BindRepeating([]() {}), u"Action");
  EXPECT_EQ(visual_style::kDeveloperToolkitRowHeight,
            button->GetPreferredSize().height());
  EXPECT_EQ(views::View::FocusBehavior::ALWAYS, button->GetFocusBehavior());
  EXPECT_EQ(views::Button::STATE_NORMAL, button->GetState());
  button->SetState(views::Button::STATE_HOVERED);
  EXPECT_EQ(views::Button::STATE_HOVERED, button->GetState());
  button->SetState(views::Button::STATE_PRESSED);
  EXPECT_EQ(views::Button::STATE_PRESSED, button->GetState());
  button->SetSelected(true);
  EXPECT_TRUE(button->GetSelected());
}

TEST_F(DeveloperDataClearViewTest, AdvancedCleanupIsCollapsedUntilRequested) {
  auto view = std::make_unique<DeveloperDataClearView>(
      base::BindRepeating([](BrowsingDataClearOptions,
                             BrowsingDataClearCallback) { return false; }));
  EXPECT_FALSE(view->expanded_for_testing());
  views::test::ButtonTestApi(view->expand_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_TRUE(view->expanded_for_testing());
  views::test::ButtonTestApi(view->expand_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_FALSE(view->expanded_for_testing());
}

bool RecordClear(int* callback_count,
                 std::optional<BrowsingDataClearOptions>* callback_options,
                 BrowsingDataClearOptions options,
                 BrowsingDataClearCallback completion) {
  ++*callback_count;
  *callback_options = options;
  std::move(completion).Run(BrowsingDataClearResult{.options = options});
  return true;
}

bool RecordReentrantClear(int* callback_count,
                          DeveloperDataClearView** view,
                          BrowsingDataClearOptions options,
                          BrowsingDataClearCallback completion) {
  ++*callback_count;
  // Simulate a second callback arriving while the first request is still on
  // the stack. The view must reject it while the request is in flight.
  views::test::ButtonTestApi((*view)->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  std::move(completion).Run(BrowsingDataClearResult{.options = options});
  return true;
}

bool HoldClear(std::optional<BrowsingDataClearOptions>* callback_options,
               BrowsingDataClearCallback* held_completion,
               BrowsingDataClearOptions options,
               BrowsingDataClearCallback completion) {
  *callback_options = options;
  *held_completion = std::move(completion);
  return true;
}

TEST_F(DeveloperDataClearViewTest,
       ConfirmationFreezesOptionsUntilSelectionChanges) {
  int callback_count = 0;
  std::optional<BrowsingDataClearOptions> callback_options;
  auto view = std::make_unique<DeveloperDataClearView>(
      base::BindRepeating(&RecordClear, &callback_count, &callback_options));

  ASSERT_TRUE(view->type_checkbox_for_testing(BrowsingDataType::kCookies));
  view->type_checkbox_for_testing(BrowsingDataType::kCookies)->SetChecked(true);
  const BrowsingDataClearOptions initial = view->options_for_testing();
  ASSERT_NE(initial.data_type_mask, ToMask(BrowsingDataType::kCache));

  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(view->pending_options_for_testing());
  EXPECT_EQ(initial, *view->pending_options_for_testing());

  // Changing the time range invalidates the old confirmation. The next click
  // must arm a new snapshot, not execute the request from the first click.
  views::test::ComboboxTestApi(view->time_range_combobox_for_testing())
      .PerformActionAt(0);
  EXPECT_FALSE(view->pending_options_for_testing());
  const BrowsingDataClearOptions changed = view->options_for_testing();
  EXPECT_NE(initial, changed);

  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(0, callback_count);
  ASSERT_TRUE(view->pending_options_for_testing());
  EXPECT_EQ(changed, *view->pending_options_for_testing());

  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(callback_options);
  EXPECT_EQ(changed, *callback_options);
}

TEST_F(DeveloperDataClearViewTest, GlobalSuccessNamesScopeTypesAndRange) {
  int callback_count = 0;
  std::optional<BrowsingDataClearOptions> callback_options;
  auto view = std::make_unique<DeveloperDataClearView>(
      base::BindRepeating(&RecordClear, &callback_count, &callback_options));
  view->type_checkbox_for_testing(BrowsingDataType::kCookies)->SetChecked(true);
  views::test::ComboboxTestApi(view->scope_combobox_for_testing())
      .PerformActionAt(1);
  views::test::ComboboxTestApi(view->time_range_combobox_for_testing())
      .PerformActionAt(1);

  // Global cleanup always has a confirmation step.
  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(view->pending_options_for_testing());
  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();

  ASSERT_EQ(1, callback_count);
  ASSERT_TRUE(callback_options);
  EXPECT_EQ(BrowsingDataTarget::kGlobal, callback_options->target);
  EXPECT_EQ(BrowsingDataTimeRange::kLast24Hours, callback_options->time_range);
  EXPECT_NE(
      callback_options->data_type_mask & ToMask(BrowsingDataType::kCookies),
      0u);
  EXPECT_TRUE(view->status_label_for_testing()->GetVisible());
}

TEST_F(DeveloperDataClearViewTest, ReentrantClickDoesNotExecuteTwice) {
  int callback_count = 0;
  DeveloperDataClearView* view_ptr = nullptr;
  auto view = std::make_unique<DeveloperDataClearView>(
      base::BindRepeating(&RecordReentrantClear, &callback_count, &view_ptr));
  view_ptr = view.get();
  view->type_checkbox_for_testing(BrowsingDataType::kCookies)->SetChecked(true);
  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(1, callback_count);
}

TEST_F(DeveloperDataClearViewTest,
       AsyncCompletionKeepsFrozenRequestAndRestoresAction) {
  std::optional<BrowsingDataClearOptions> callback_options;
  BrowsingDataClearCallback held_completion;
  auto view = std::make_unique<DeveloperDataClearView>(
      base::BindRepeating(&HoldClear, &callback_options, &held_completion));
  view->type_checkbox_for_testing(BrowsingDataType::kCookies)->SetChecked(true);

  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  views::test::ButtonTestApi(view->clear_button_for_testing())
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(callback_options);
  ASSERT_FALSE(held_completion.is_null());
  EXPECT_TRUE(view->clear_request_in_flight_for_testing());
  EXPECT_FALSE(view->clear_button_for_testing()->GetEnabled());

  views::test::ComboboxTestApi(view->scope_combobox_for_testing())
      .PerformActionAt(1);
  EXPECT_EQ(BrowsingDataTarget::kCurrentSite, callback_options->target);
  std::move(held_completion)
      .Run(BrowsingDataClearResult{
          .options = *callback_options,
          .failed_data_type_mask = ToMask(BrowsingDataType::kCookies),
      });
  EXPECT_FALSE(view->clear_request_in_flight_for_testing());
  EXPECT_TRUE(view->clear_button_for_testing()->GetEnabled());
  EXPECT_TRUE(view->status_label_for_testing()->GetVisible());
}

}  // namespace
}  // namespace ahoi
