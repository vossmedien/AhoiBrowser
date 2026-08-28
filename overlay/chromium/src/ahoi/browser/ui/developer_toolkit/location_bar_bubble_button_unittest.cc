// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/location_bar_bubble_button.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/test_event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace ahoi {
namespace {

ui::MouseEvent MouseEvent(ui::EventType type) {
  return ui::MouseEvent(type, gfx::Point(8, 8), gfx::Point(8, 8),
                        base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                        ui::EF_LEFT_MOUSE_BUTTON);
}

std::unique_ptr<LocationBarBubbleButton> MakeButton(bool* surface_showing,
                                                    int* activation_count) {
  auto button = std::make_unique<LocationBarBubbleButton>(
      base::BindRepeating(
          [](bool* showing, int* count, const ui::Event&) {
            *showing = !*showing;
            ++*count;
          },
          surface_showing, activation_count),
      base::BindRepeating([](const bool* showing) { return *showing; },
                          surface_showing));
  button->SetBounds(0, 0, 24, 24);
  return button;
}

LocationBarBubbleButton* MountButton(views::Widget* widget,
                                     bool* surface_showing,
                                     int* activation_count) {
  auto button = MakeButton(surface_showing, activation_count);
  auto* mounted_button = button.get();
  widget->SetContentsView(std::move(button));
  widget->Show();
  return mounted_button;
}

class LocationBarBubbleButtonTest : public views::ViewsTestBase {
 protected:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    // The constructor calls ConfigureVectorImageButton(), which requires a
    // live process-global provider. Pin that fixture invariant explicitly so
    // future harness drift fails here instead of in product code.
    test_views_delegate()->set_layout_provider(
        std::make_unique<views::LayoutProvider>());
    ASSERT_NE(nullptr, views::LayoutProvider::Get());
  }
};

TEST_F(LocationBarBubbleButtonTest,
       SecondMousePressClosesExactlyOnceWithoutReleaseReopen) {
  bool surface_showing = true;
  int activation_count = 0;
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* button = MountButton(widget.get(), &surface_showing, &activation_count);

  EXPECT_TRUE(button->OnMousePressed(MouseEvent(ui::EventType::kMousePressed)));
  EXPECT_EQ(1, activation_count);
  EXPECT_FALSE(surface_showing);
  button->OnMouseReleased(MouseEvent(ui::EventType::kMouseReleased));

  EXPECT_EQ(1, activation_count);
  EXPECT_FALSE(surface_showing);
}

TEST_F(LocationBarBubbleButtonTest,
       DisabledMousePressIsConsumedWithoutActivation) {
  bool surface_showing = false;
  int activation_count = 0;
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* button = MountButton(widget.get(), &surface_showing, &activation_count);
  button->SetEnabled(false);
  ui::MouseEvent event = MouseEvent(ui::EventType::kMousePressed);

  button->OnEvent(&event);

  EXPECT_TRUE(event.handled());
  EXPECT_EQ(0, activation_count);
  EXPECT_FALSE(surface_showing);
}

TEST_F(LocationBarBubbleButtonTest, ClosedSurfaceOpensOnMouseRelease) {
  bool surface_showing = false;
  int activation_count = 0;
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* button = MountButton(widget.get(), &surface_showing, &activation_count);

  EXPECT_TRUE(button->OnMousePressed(MouseEvent(ui::EventType::kMousePressed)));
  button->OnMouseReleased(MouseEvent(ui::EventType::kMouseReleased));

  EXPECT_EQ(1, activation_count);
  EXPECT_TRUE(surface_showing);
}

TEST_F(LocationBarBubbleButtonTest,
       NextMousePressClearsAnEarlierReleaseSuppression) {
  bool surface_showing = true;
  int activation_count = 0;
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* button = MountButton(widget.get(), &surface_showing, &activation_count);

  EXPECT_TRUE(button->OnMousePressed(MouseEvent(ui::EventType::kMousePressed)));
  button->OnMouseReleased(MouseEvent(ui::EventType::kMouseReleased));
  EXPECT_EQ(1, activation_count);
  EXPECT_FALSE(surface_showing);

  EXPECT_TRUE(button->OnMousePressed(MouseEvent(ui::EventType::kMousePressed)));
  button->OnMouseReleased(MouseEvent(ui::EventType::kMouseReleased));
  EXPECT_EQ(2, activation_count);
  EXPECT_TRUE(surface_showing);
}

TEST_F(LocationBarBubbleButtonTest,
       KeyboardAndTouchRemainTriggerableWhileSurfaceIsShowing) {
  bool surface_showing = true;
  int activation_count = 0;
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* button = MountButton(widget.get(), &surface_showing, &activation_count);
  ui::KeyEvent key_event(ui::EventType::kKeyPressed, ui::VKEY_SPACE,
                         ui::EF_NONE);
  ui::test::TestEvent touch_event(ui::EventType::kGestureTap);

  EXPECT_TRUE(button->IsTriggerableEvent(key_event));
  EXPECT_TRUE(button->IsTriggerableEvent(touch_event));
}

}  // namespace
}  // namespace ahoi
