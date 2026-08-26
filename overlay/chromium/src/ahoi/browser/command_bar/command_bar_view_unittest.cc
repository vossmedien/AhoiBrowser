// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_view.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/test_event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi {

namespace {

class CommandBarViewTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    CreateView(CommandBarDisposition::kCurrentTab);
  }

  void TearDown() override {
    view_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  void CreateView(CommandBarDisposition disposition) {
    view_ = std::make_unique<CommandBarView>(
        disposition, u"Search or type a URL",
        base::BindRepeating(&CommandBarViewTest::BuildSuggestions,
                            base::Unretained(this)),
        base::BindRepeating(&CommandBarViewTest::Execute,
                            base::Unretained(this)),
        base::BindRepeating(&CommandBarViewTest::RequestClose,
                            base::Unretained(this)),
        base::BindOnce(&CommandBarViewTest::OnDestroyed,
                       base::Unretained(this)));
  }

  std::vector<CommandBarSuggestion> BuildSuggestions(
      std::u16string_view input) {
    last_query_ = std::u16string(input);
    std::vector<CommandBarSuggestion> suggestions;
    if (return_many_suggestions_) {
      for (size_t index = 0; index < 8; ++index) {
        suggestions.push_back({
            .kind = CommandBarSuggestionKind::kLocalItem,
            .title = u"Suggestion",
            .item =
                CommandItem{
                    .type = CommandItemType::kSavedPage,
                    .stable_id = "page-" + std::to_string(index),
                    .title = u"Suggestion",
                    .url = GURL("https://example.test/"),
                },
        });
      }
      return suggestions;
    }
    if (input.empty() || input == u"project") {
      suggestions.push_back({
          .kind = CommandBarSuggestionKind::kLocalItem,
          .title = u"Project documentation",
          .secondary_text = u"docs.test",
          .item =
              CommandItem{
                  .type = CommandItemType::kSavedPage,
                  .stable_id = "page-1",
                  .title = u"Project documentation",
                  .url = GURL("https://docs.test/"),
              },
      });
    }
    if (!input.empty()) {
      suggestions.push_back({
          .kind = CommandBarSuggestionKind::kInputFallback,
          .title = std::u16string(input),
          .secondary_text = u"Local Search",
      });
    }
    return suggestions;
  }

  bool Execute(const CommandBarSuggestion& suggestion,
               std::u16string_view original_input) {
    executed_suggestion_ = suggestion;
    executed_input_ = std::u16string(original_input);
    if (destroy_view_on_execute_) {
      view_.reset();
    }
    return true;
  }

  void OnDestroyed() { destroyed_ = true; }
  void RequestClose() { ++close_request_count_; }

  std::unique_ptr<CommandBarView> view_;
  std::u16string last_query_;
  std::optional<CommandBarSuggestion> executed_suggestion_;
  std::u16string executed_input_;
  bool destroyed_ = false;
  bool destroy_view_on_execute_ = false;
  bool return_many_suggestions_ = false;
  int close_request_count_ = 0;
};

TEST_F(CommandBarViewTest, ExposesListboxAndOptionSemantics) {
  view_->SetInitialQuery(u"", /*prefer_input_fallback=*/false);

  EXPECT_EQ(view_->GetViewAccessibility().GetCachedRole(),
            ax::mojom::Role::kGroup);
  EXPECT_EQ(
      view_->results_view_for_testing()->GetViewAccessibility().GetCachedRole(),
      ax::mojom::Role::kListBox);
  ASSERT_EQ(view_->suggestion_count_for_testing(), 1u);
  ASSERT_TRUE(view_->row_for_testing(0));
  EXPECT_EQ(view_->row_for_testing(0)->GetViewAccessibility().GetCachedRole(),
            ax::mojom::Role::kListBoxOption);
  EXPECT_EQ(view_->row_for_testing(0)->GetViewAccessibility().GetCachedName(),
            u"Project documentation, docs.test");
  EXPECT_EQ(view_->row_for_testing(0)->GetFocusBehavior(),
            views::View::FocusBehavior::ALWAYS);
  EXPECT_EQ(view_->textfield_for_testing()->GetAccessibleName(),
            u"Search or type a URL");
  EXPECT_EQ(view_->textfield_for_testing()
                ->GetViewAccessibility()
                .GetActiveDescendantView(),
            &view_->row_for_testing(0)->GetViewAccessibility());
}

TEST_F(CommandBarViewTest, InitialCurrentQueryPrefersInputFallback) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/true);
  ASSERT_EQ(view_->suggestion_count_for_testing(), 2u);
  EXPECT_EQ(view_->selected_index_for_testing(), 1u);
  EXPECT_EQ(last_query_, u"project");
}

TEST_F(CommandBarViewTest, ArrowKeysWrapAndEnterExecutesSelection) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);
  ASSERT_EQ(view_->suggestion_count_for_testing(), 2u);
  EXPECT_EQ(view_->selected_index_for_testing(), 0u);

  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE)));
  EXPECT_EQ(view_->selected_index_for_testing(), 1u);
  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE)));
  EXPECT_EQ(view_->selected_index_for_testing(), 0u);
  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_UP, ui::EF_NONE)));
  EXPECT_EQ(view_->selected_index_for_testing(), 1u);

  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE)));
  ASSERT_TRUE(executed_suggestion_.has_value());
  EXPECT_EQ(executed_suggestion_->kind,
            CommandBarSuggestionKind::kInputFallback);
  EXPECT_EQ(executed_input_, u"project");
}

TEST_F(CommandBarViewTest, UserEditsRebuildAndSelectFirstResult) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/true);
  ASSERT_EQ(view_->selected_index_for_testing(), 1u);

  view_->textfield_for_testing()->SetText(u"another query");
  view_->ContentsChanged(view_->textfield_for_testing(), u"another query");
  EXPECT_EQ(last_query_, u"another query");
  ASSERT_EQ(view_->suggestion_count_for_testing(), 1u);
  EXPECT_EQ(view_->selected_index_for_testing(), 0u);
  EXPECT_EQ(view_->textfield_for_testing()
                ->GetViewAccessibility()
                .GetActiveDescendantView(),
            &view_->row_for_testing(0)->GetViewAccessibility());
}

TEST_F(CommandBarViewTest, EscapeIsHandled) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);

  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_EQ(close_request_count_, 1);
}

TEST_F(CommandBarViewTest,
       TabAndShiftTabCycleOnlyVisibleSuggestionsAndSyncAccessibility) {
  return_many_suggestions_ = true;
  view_->SetInitialQuery(u"many", /*prefer_input_fallback=*/false);
  ASSERT_EQ(view_->suggestion_count_for_testing(), 5u);
  ASSERT_EQ(view_->selected_index_for_testing(), 0u);

  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_EQ(view_->selected_index_for_testing(), 1u);
  EXPECT_FALSE(view_->row_selected_for_testing(0));
  EXPECT_TRUE(view_->row_selected_for_testing(1));
  EXPECT_EQ(view_->textfield_for_testing()
                ->GetViewAccessibility()
                .GetActiveDescendantView(),
            &view_->row_for_testing(1)->GetViewAccessibility());

  EXPECT_TRUE(view_->HandleKeyEventForTesting(ui::KeyEvent(
      ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_SHIFT_DOWN)));
  EXPECT_EQ(view_->selected_index_for_testing(), 0u);
  EXPECT_TRUE(view_->row_selected_for_testing(0));

  EXPECT_TRUE(view_->HandleKeyEventForTesting(ui::KeyEvent(
      ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_SHIFT_DOWN)));
  EXPECT_EQ(view_->selected_index_for_testing(), 4u);
  EXPECT_TRUE(view_->row_selected_for_testing(4));
}

TEST_F(CommandBarViewTest, EnterExecutesSuggestionSelectedByTab) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);
  ASSERT_EQ(view_->suggestion_count_for_testing(), 2u);

  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_NONE)));
  ASSERT_EQ(view_->selected_index_for_testing(), 1u);
  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE)));

  ASSERT_TRUE(executed_suggestion_.has_value());
  EXPECT_EQ(executed_suggestion_->kind,
            CommandBarSuggestionKind::kInputFallback);
  EXPECT_EQ(executed_input_, u"project");
}

TEST_F(CommandBarViewTest, TabTraversalKeepsInputFocusedForContinuedTyping) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->Show();
  auto* command_bar =
      static_cast<CommandBarView*>(widget->SetContentsView(std::move(view_)));
  command_bar->FocusInput();
  ASSERT_TRUE(command_bar->textfield_for_testing()->HasFocus());

  EXPECT_TRUE(command_bar->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_TRUE(command_bar->textfield_for_testing()->HasFocus());
  EXPECT_FALSE(command_bar->row_for_testing(1)->HasFocus());

  command_bar->textfield_for_testing()->SetText(u"continued typing");
  command_bar->ContentsChanged(command_bar->textfield_for_testing(),
                               u"continued typing");
  EXPECT_EQ(last_query_, u"continued typing");
  EXPECT_EQ(command_bar->selected_index_for_testing(), 0u);
}

TEST_F(CommandBarViewTest, FocusedRowTabAndEnterUseSynchronizedSelection) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);
  ASSERT_EQ(view_->suggestion_count_for_testing(), 2u);

  EXPECT_TRUE(view_->HandleResultKeyEventForTesting(
      0u, ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_EQ(view_->selected_index_for_testing(), 1u);
  EXPECT_TRUE(view_->row_selected_for_testing(1));

  EXPECT_TRUE(view_->HandleResultKeyEventForTesting(
      1u,
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE)));
  EXPECT_FALSE(executed_suggestion_.has_value());
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(executed_suggestion_.has_value());
  EXPECT_EQ(executed_suggestion_->kind,
            CommandBarSuggestionKind::kInputFallback);
}

TEST_F(CommandBarViewTest, ModifiedTabTraversalRemainsUnhandled) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);

  EXPECT_FALSE(view_->HandleKeyEventForTesting(ui::KeyEvent(
      ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_CONTROL_DOWN)));
  EXPECT_FALSE(view_->HandleKeyEventForTesting(ui::KeyEvent(
      ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_COMMAND_DOWN)));
}

TEST_F(CommandBarViewTest, DestroyedCallbackRunsExactlyWithViewLifetime) {
  EXPECT_FALSE(destroyed_);
  view_.reset();
  EXPECT_TRUE(destroyed_);
}

TEST_F(CommandBarViewTest, ExecutionMaySynchronouslyDestroyBubble) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);
  destroy_view_on_execute_ = true;

  EXPECT_TRUE(view_->HandleKeyEventForTesting(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE)));
  EXPECT_FALSE(view_);
  EXPECT_TRUE(destroyed_);
  ASSERT_TRUE(executed_suggestion_.has_value());
  EXPECT_EQ(executed_suggestion_->kind, CommandBarSuggestionKind::kLocalItem);
}

TEST_F(CommandBarViewTest, CapsResultsAtFiveWithoutScrollableContainer) {
  return_many_suggestions_ = true;
  view_->SetInitialQuery(u"many", /*prefer_input_fallback=*/false);

  EXPECT_EQ(5u, view_->suggestion_count_for_testing());
  EXPECT_EQ(5u, view_->results_view_for_testing()->children().size());
  EXPECT_EQ(view_.get(), view_->results_view_for_testing()->parent());
}

TEST_F(CommandBarViewTest,
       MouseClickDefersDestructiveExecutionUntilDispatchEnds) {
  view_->SetInitialQuery(u"project", /*prefer_input_fallback=*/false);
  destroy_view_on_execute_ = true;
  views::test::ButtonTestApi(
      static_cast<views::Button*>(view_->row_for_testing(0)))
      .NotifyClick(ui::test::TestEvent());

  EXPECT_TRUE(view_);
  EXPECT_FALSE(executed_suggestion_.has_value());
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(view_);
  EXPECT_TRUE(destroyed_);
  ASSERT_TRUE(executed_suggestion_.has_value());
  EXPECT_EQ(executed_suggestion_->kind, CommandBarSuggestionKind::kLocalItem);
}

TEST_F(CommandBarViewTest, SeparateDelegateOutlivesClientOwnedWidget) {
  auto anchor_widget =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  views::View* const anchor =
      anchor_widget->SetContentsView(std::make_unique<views::View>());
  bool extra_view_destroyed = false;
  auto content = std::make_unique<CommandBarView>(
      CommandBarDisposition::kCurrentTab, u"Search or type a URL",
      base::BindRepeating([](std::u16string_view) {
        return std::vector<CommandBarSuggestion>();
      }),
      base::BindRepeating([](const CommandBarSuggestion&, std::u16string_view) {
        return true;
      }),
      base::BindRepeating([] {}),
      base::BindOnce([](bool* destroyed) { *destroyed = true; },
                     &extra_view_destroyed));
  views::Textfield* const textfield = content->textfield_for_testing();

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::TOP_CENTER,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetInitiallyFocusedView(textfield);
  delegate->SetContentsView(std::move(content));

  bool closed = false;
  std::unique_ptr<views::Widget> widget;
  widget = views::BubbleDialogDelegate::CreateBubble(
      delegate.get(), base::BindOnce(
                          [](std::unique_ptr<views::Widget>* owner,
                             bool* closed, views::Widget::ClosedReason) {
                            *closed = true;
                            owner->reset();
                          },
                          &widget, &closed));
  ASSERT_TRUE(widget);
  EXPECT_EQ(widget->widget_delegate(), delegate.get());

  widget->Show();
  widget->Close();
  EXPECT_FALSE(widget);
  EXPECT_TRUE(closed);
  EXPECT_TRUE(extra_view_destroyed);
}

}  // namespace
}  // namespace ahoi
