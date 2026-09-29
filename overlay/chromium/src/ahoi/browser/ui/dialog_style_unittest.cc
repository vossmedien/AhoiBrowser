// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/dialog_style.h"

#include <memory>

#include "ahoi/browser/ui/visual_style.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/font.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi::dialog_style {

namespace {

using DialogStyleTest = views::ViewsTestBase;

// DESIGN_SPEC 2026-09-29: Workspace dialog / HTTP Auth.
TEST_F(DialogStyleTest, TokensMatchTheDesignSpec) {
  EXPECT_EQ(420, visual_style::kWorkspaceDialogWidth);
  EXPECT_EQ(480, visual_style::kAuthDialogWidth);
  EXPECT_EQ(24, visual_style::kDialogPadding);
  EXPECT_EQ(36, visual_style::kDialogFieldHeight);
  EXPECT_EQ(32, visual_style::kDialogButtonHeight);
  EXPECT_EQ(18, visual_style::kDialogCornerRadius);
}

TEST_F(DialogStyleTest, FrameUsesSpecWidthPaddingAndRadius) {
  views::BubbleDialogDelegate delegate(nullptr, views::BubbleBorder::FLOAT);
  ApplyDialogFrame(delegate, visual_style::kWorkspaceDialogWidth);

  EXPECT_EQ(420, delegate.fixed_width());
  EXPECT_EQ(18, delegate.corner_radius());
  const gfx::Insets contents = delegate.frame_margins().contents;
  EXPECT_EQ(24, contents.left());
  EXPECT_EQ(24, contents.right());
  EXPECT_EQ(24, contents.bottom());
  const gfx::Insets title = delegate.frame_margins().title;
  EXPECT_EQ(24, title.top());
  EXPECT_EQ(24, title.left());
  EXPECT_EQ(24, title.right());
  EXPECT_EQ(24, DialogButtonRowInsets().bottom());
}

TEST_F(DialogStyleTest, TitleIsTwentyPointSemibold) {
  views::Label title(u"Workspace anlegen");
  StyleDialogTitle(title);
  EXPECT_EQ(20, title.font_list().GetFontSize());
  EXPECT_EQ(gfx::Font::Weight::SEMIBOLD, title.font_list().GetFontWeight());
  EXPECT_EQ(25, title.GetLineHeight());
}

TEST_F(DialogStyleTest, FieldsAndButtonsGetSpecHeights) {
  views::Textfield field;
  StyleDialogField(field);
  EXPECT_EQ(36, field.GetPreferredSize().height());

  views::LabelButton button(views::Button::PressedCallback(), u"Anlegen");
  StyleDialogButton(button);
  EXPECT_EQ(32, button.GetPreferredSize().height());
}

TEST_F(DialogStyleTest, FocusRingHasTwoPointStrokeAndTwoPointGap) {
  // Textfield installs its own FocusRing; the style only tunes it.
  views::Textfield field;
  StyleDialogField(field);
  const views::FocusRing* ring = views::FocusRing::Get(&field);
  ASSERT_TRUE(ring);
  EXPECT_FLOAT_EQ(2.0f, ring->GetHaloThickness());
  // Stroke centre: 2 pt gap plus half of the 2 pt stroke outside the edge.
  EXPECT_FLOAT_EQ(-3.0f, ring->GetHaloInset());
}

TEST_F(DialogStyleTest, FieldErrorIsWrittenOutAndClearable) {
  views::Textfield field;
  std::unique_ptr<views::Label> error = CreateFieldErrorLabel();
  EXPECT_FALSE(error->GetVisible());

  ShowFieldError(field, *error, u"Gib einen Namen ein.");
  EXPECT_TRUE(field.GetInvalid());
  EXPECT_TRUE(error->GetVisible());
  EXPECT_EQ(u"Gib einen Namen ein.", error->GetText());

  ShowFieldError(field, *error, std::u16string());
  EXPECT_FALSE(field.GetInvalid());
  EXPECT_FALSE(error->GetVisible());
}

}  // namespace

}  // namespace ahoi::dialog_style
