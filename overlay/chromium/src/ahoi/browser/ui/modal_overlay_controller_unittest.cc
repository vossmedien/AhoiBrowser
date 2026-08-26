// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/modal_overlay_controller.h"

#include <memory>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi {

namespace {

class ModalOverlayControllerTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    zero_duration_ = std::make_unique<gfx::ScopedAnimationDurationScaleMode>(
        gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);

    host_widget_ =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    host_widget_->SetBounds(gfx::Rect(0, 0, 900, 600));
    auto host = std::make_unique<views::View>();
    window_host_ = host.get();
    center_anchor_ = host->AddChildView(std::make_unique<views::View>());
    center_anchor_->SetBoundsRect(gfx::Rect(240, 80, 660, 520));
    focus_view_ = host->AddChildView(std::make_unique<views::View>());
    focus_view_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    host_widget_->SetContentsView(std::move(host));
    host_widget_->Show();
    focus_view_->RequestFocus();

    controller_ =
        std::make_unique<ModalOverlayController>(window_host_, center_anchor_);

    auto panel_params =
        CreateParamsForTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                  views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    panel_params.parent = host_widget_->GetNativeView();
    panel_widget_ = CreateTestWidget(std::move(panel_params));
    panel_widget_->SetContentsView(std::make_unique<views::View>());
  }

  void TearDown() override {
    controller_.reset();
    panel_widget_.reset();
    host_widget_.reset();
    zero_duration_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  bool ShowPanel() {
    return controller_->ShowPanel(
        panel_widget_.get(),
        base::BindRepeating(&ModalOverlayControllerTest::ClosePanel,
                            base::Unretained(this)));
  }

  void ClosePanel() {
    ++close_count_;
    panel_widget_->Close();
  }

  std::unique_ptr<gfx::ScopedAnimationDurationScaleMode> zero_duration_;
  std::unique_ptr<views::Widget> host_widget_;
  std::unique_ptr<views::Widget> panel_widget_;
  raw_ptr<views::View> window_host_ = nullptr;
  raw_ptr<views::View> center_anchor_ = nullptr;
  raw_ptr<views::View> focus_view_ = nullptr;
  std::unique_ptr<ModalOverlayController> controller_;
  int close_count_ = 0;
};

TEST_F(ModalOverlayControllerTest,
       ScrimCoversWindowAndUsesContentsCenterAnchor) {
  ASSERT_TRUE(ShowPanel());
  views::View* const scrim = controller_->scrim_view_for_testing();
  ASSERT_TRUE(scrim);
  EXPECT_TRUE(scrim->GetVisible());
  EXPECT_EQ(window_host_->GetLocalBounds(), scrim->bounds());
  EXPECT_EQ(center_anchor_, controller_->center_anchor());
  EXPECT_TRUE(scrim->GetViewAccessibility().GetIsIgnored());

  window_host_->SetBoundsRect(gfx::Rect(0, 0, 1024, 720));
  EXPECT_EQ(window_host_->GetLocalBounds(), scrim->bounds());
}

TEST_F(ModalOverlayControllerTest,
       OutsidePressClosesExactlyOnceAndRestoresFocus) {
  ASSERT_TRUE(ShowPanel());
  views::View* const scrim = controller_->scrim_view_for_testing();
  ASSERT_TRUE(scrim);
  const ui::MouseEvent press(ui::EventType::kMousePressed, gfx::Point(),
                             gfx::Point(), ui::EventTimeForNow(),
                             ui::EF_LEFT_MOUSE_BUTTON,
                             ui::EF_LEFT_MOUSE_BUTTON);

  EXPECT_TRUE(scrim->OnMousePressed(press));
  EXPECT_TRUE(scrim->OnMousePressed(press));
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, close_count_);
  EXPECT_FALSE(scrim->GetVisible());
  EXPECT_FALSE(controller_->IsShowingPanel(panel_widget_.get()));
  EXPECT_EQ(focus_view_, host_widget_->GetFocusManager()->GetFocusedView());
}

TEST_F(ModalOverlayControllerTest, RejectsConcurrentPanel) {
  ASSERT_TRUE(ShowPanel());
  EXPECT_FALSE(
      controller_->ShowPanel(panel_widget_.get(), base::BindRepeating([] {})));
  EXPECT_EQ(0, close_count_);
}

TEST_F(ModalOverlayControllerTest, ImmediateDismissDoesNotInvokeClose) {
  ASSERT_TRUE(ShowPanel());
  controller_->DismissPanelImmediately(panel_widget_.get());
  task_environment()->RunUntilIdle();

  EXPECT_EQ(0, close_count_);
  EXPECT_FALSE(controller_->scrim_view_for_testing()->GetVisible());
  EXPECT_FALSE(controller_->IsShowingPanel(panel_widget_.get()));
}

}  // namespace

}  // namespace ahoi
