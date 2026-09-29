// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_resize_area.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {
namespace {

constexpr double kInitialRatio = 0.4;
constexpr int kRatioExtent = 200;

struct ResizeUpdate {
  size_t divider_index;
  double ratio;
  bool done_resizing;
};

SidebarSplitDivider MakeDivider(bool resizes_horizontally) {
  return {
      .divider_index = 0,
      .start = resizes_horizontally ? gfx::PointF(10.0f, 0.0f)
                                    : gfx::PointF(0.0f, 10.0f),
      .end = resizes_horizontally ? gfx::PointF(10.0f, 40.0f)
                                  : gfx::PointF(120.0f, 10.0f),
      .ratio = kInitialRatio,
      .ratio_extent = kRatioExtent,
  };
}

SidebarSplitResizeCallback RecordUpdates(std::vector<ResizeUpdate>* updates,
                                         bool* accept_update) {
  return base::BindLambdaForTesting(
      [updates, accept_update](size_t divider_index, double ratio,
                               bool done_resizing) {
        updates->push_back({divider_index, ratio, done_resizing});
        return *accept_update;
      });
}

ui::MouseEvent MakeMouseEvent(ui::EventType type,
                              int x,
                              int flags,
                              int changed_button_flags) {
  const gfx::Point location(x, 10);
  return ui::MouseEvent(type, location, location, base::TimeTicks(), flags,
                        changed_button_flags);
}

class SidebarSplitResizeAreaTest : public views::ViewsTestBase {
 protected:
  void TearDown() override {
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  SidebarSplitResizeArea* InstallArea(SidebarSplitDivider divider,
                                      SidebarSplitResizeCallback callback) {
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget_->SetSize(gfx::Size(120, 40));
    auto area = std::make_unique<SidebarSplitResizeArea>(std::move(divider),
                                                         std::move(callback));
    SidebarSplitResizeArea* const area_ptr = area.get();
    widget_->SetContentsView(std::move(area));
    widget_->Show();
    return area_ptr;
  }

 private:
  std::unique_ptr<views::Widget> widget_;
};

TEST_F(SidebarSplitResizeAreaTest,
       PointerResizeEmitsIntermediateThenFinalUpdate) {
  std::vector<ResizeUpdate> updates;
  bool accept_update = true;
  SidebarSplitResizeArea* const area =
      InstallArea(MakeDivider(/*resizes_horizontally=*/true),
                  RecordUpdates(&updates, &accept_update));

  EXPECT_TRUE(area->OnMousePressed(MakeMouseEvent(ui::EventType::kMousePressed,
                                                  10, ui::EF_LEFT_MOUSE_BUTTON,
                                                  ui::EF_LEFT_MOUSE_BUTTON)));
  EXPECT_TRUE(area->OnMouseDragged(MakeMouseEvent(
      ui::EventType::kMouseDragged, 40, ui::EF_LEFT_MOUSE_BUTTON, 0)));
  area->OnMouseReleased(MakeMouseEvent(ui::EventType::kMouseReleased, 40,
                                       ui::EF_NONE, ui::EF_LEFT_MOUSE_BUTTON));

  ASSERT_EQ(2u, updates.size());
  EXPECT_EQ(0u, updates[0].divider_index);
  EXPECT_NEAR(0.55, updates[0].ratio, 1e-9);
  EXPECT_FALSE(updates[0].done_resizing);
  EXPECT_NEAR(0.55, updates[1].ratio, 1e-9);
  EXPECT_TRUE(updates[1].done_resizing);
}

TEST_F(SidebarSplitResizeAreaTest,
       RejectedCallbackDoesNotAdvanceAcceptedRatio) {
  std::vector<ResizeUpdate> updates;
  bool accept_update = false;
  SidebarSplitResizeArea area(MakeDivider(/*resizes_horizontally=*/true),
                              RecordUpdates(&updates, &accept_update));
  const ui::KeyEvent move_right(ui::EventType::kKeyPressed, ui::VKEY_RIGHT,
                                ui::EF_NONE);

  EXPECT_FALSE(area.OnKeyPressed(move_right));
  accept_update = true;
  EXPECT_TRUE(area.OnKeyPressed(move_right));

  ASSERT_EQ(2u, updates.size());
  EXPECT_NEAR(0.45, updates[0].ratio, 1e-9);
  EXPECT_NEAR(0.45, updates[1].ratio, 1e-9);
  EXPECT_TRUE(updates[0].done_resizing);
  EXPECT_TRUE(updates[1].done_resizing);
}

TEST_F(SidebarSplitResizeAreaTest, CaptureLossRollsBackToPressRatio) {
  std::vector<ResizeUpdate> updates;
  bool accept_update = true;
  SidebarSplitResizeArea* const area =
      InstallArea(MakeDivider(/*resizes_horizontally=*/true),
                  RecordUpdates(&updates, &accept_update));

  EXPECT_TRUE(area->OnMousePressed(MakeMouseEvent(ui::EventType::kMousePressed,
                                                  10, ui::EF_LEFT_MOUSE_BUTTON,
                                                  ui::EF_LEFT_MOUSE_BUTTON)));
  EXPECT_TRUE(area->OnMouseDragged(MakeMouseEvent(
      ui::EventType::kMouseDragged, 40, ui::EF_LEFT_MOUSE_BUTTON, 0)));
  area->OnMouseCaptureLost();

  ASSERT_EQ(2u, updates.size());
  EXPECT_NEAR(0.55, updates[0].ratio, 1e-9);
  EXPECT_FALSE(updates[0].done_resizing);
  EXPECT_NEAR(kInitialRatio, updates[1].ratio, 1e-9);
  EXPECT_TRUE(updates[1].done_resizing);
}

TEST_F(SidebarSplitResizeAreaTest,
       KeyboardAndAccessibleActionsCoverBothDividerAxes) {
  std::vector<ResizeUpdate> horizontal_updates;
  std::vector<ResizeUpdate> vertical_updates;
  bool accept_update = true;
  SidebarSplitResizeArea horizontal(
      MakeDivider(/*resizes_horizontally=*/true),
      RecordUpdates(&horizontal_updates, &accept_update));
  SidebarSplitResizeArea vertical(
      MakeDivider(/*resizes_horizontally=*/false),
      RecordUpdates(&vertical_updates, &accept_update));

  EXPECT_TRUE(horizontal.OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RIGHT, ui::EF_NONE)));
  ui::AXActionData decrement;
  decrement.action = ax::mojom::Action::kDecrement;
  EXPECT_TRUE(horizontal.HandleAccessibleAction(decrement));

  EXPECT_TRUE(vertical.OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE)));
  ui::AXActionData increment;
  increment.action = ax::mojom::Action::kIncrement;
  EXPECT_TRUE(vertical.HandleAccessibleAction(increment));

  ASSERT_EQ(2u, horizontal_updates.size());
  EXPECT_NEAR(0.45, horizontal_updates[0].ratio, 1e-9);
  EXPECT_NEAR(kInitialRatio, horizontal_updates[1].ratio, 1e-9);
  EXPECT_TRUE(horizontal_updates[0].done_resizing);
  EXPECT_TRUE(horizontal_updates[1].done_resizing);

  ASSERT_EQ(2u, vertical_updates.size());
  EXPECT_NEAR(0.45, vertical_updates[0].ratio, 1e-9);
  EXPECT_NEAR(0.5, vertical_updates[1].ratio, 1e-9);
  EXPECT_TRUE(vertical_updates[0].done_resizing);
  EXPECT_TRUE(vertical_updates[1].done_resizing);
}

}  // namespace
}  // namespace ahoi::sidebar
