// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/dialog_style.h"

#include "ahoi/browser/ui/appearance/opaque_palette.h"
#include "ahoi/browser/ui/visual_style.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/window/dialog_delegate.h"

namespace ahoi::dialog_style {

namespace {

namespace palette_tokens = appearance::palette_tokens;

// The focus ring stroke is centred on the inset path, so its centre sits the
// gap plus half the stroke outside the control edge.
constexpr float kFocusRingHaloThickness = palette_tokens::kFocusRingThickness;
constexpr float kFocusRingHaloInset =
    -(palette_tokens::kFocusRingGap + kFocusRingHaloThickness / 2.0f);

}  // namespace

gfx::Insets DialogContentMargins() {
  return gfx::Insets::TLBR(visual_style::kDialogSectionSpacing,
                           visual_style::kDialogPadding,
                           visual_style::kDialogPadding,
                           visual_style::kDialogPadding);
}

gfx::Insets DialogTitleMargins() {
  return gfx::Insets::TLBR(visual_style::kDialogPadding,
                           visual_style::kDialogPadding, 0,
                           visual_style::kDialogPadding);
}

gfx::Insets DialogButtonRowInsets() {
  return gfx::Insets::TLBR(0, visual_style::kDialogPadding,
                           visual_style::kDialogPadding,
                           visual_style::kDialogPadding);
}

gfx::FontList DialogTitleFontList() {
  const gfx::FontList base;
  return base
      .DeriveWithSizeDelta(visual_style::kDialogTitleFontSize -
                           base.GetFontSize())
      .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD);
}

void ApplyDialogFrame(views::DialogDelegate& delegate, int width) {
  delegate.set_fixed_width(width);
  delegate.set_frame_margins({.contents = DialogContentMargins(),
                              .title = DialogTitleMargins()});
  delegate.set_use_round_corners(true);
  delegate.set_corner_radius(visual_style::kDialogCornerRadius);
}

void ApplyDialogChrome(views::DialogDelegate& delegate) {
  if (views::BubbleFrameView* frame = delegate.GetBubbleFrameView()) {
    if (views::Label* title = frame->default_title()) {
      StyleDialogTitle(*title);
    }
  }
  if (!delegate.GetWidget()) {
    return;
  }
  delegate.SetButtonRowInsets(DialogButtonRowInsets());
  if (views::LabelButton* ok = delegate.GetOkButton()) {
    StyleDialogButton(*ok);
  }
  if (views::LabelButton* cancel = delegate.GetCancelButton()) {
    StyleDialogButton(*cancel);
  }
}

void StyleDialogTitle(views::Label& title) {
  title.SetFontList(DialogTitleFontList());
  title.SetLineHeight(visual_style::kDialogTitleLineHeight);
  title.SetEnabledColor(visual_style::kText);
}

void StyleDialogButton(views::LabelButton& button) {
  button.SetMinSize(gfx::Size(0, visual_style::kDialogButtonHeight));
  button.SetMaxSize(gfx::Size(0, visual_style::kDialogButtonHeight));
  StyleFocusRing(button);
}

void StyleDialogField(views::Textfield& field) {
  field.SetPreferredSize(gfx::Size(field.GetPreferredSize().width(),
                                   visual_style::kDialogFieldHeight));
  StyleFocusRing(field);
}

void StyleFocusRing(views::View& view) {
  views::FocusRing* const ring = views::FocusRing::Get(&view);
  if (!ring) {
    return;
  }
  ring->SetHaloThickness(kFocusRingHaloThickness);
  ring->SetHaloInset(kFocusRingHaloInset);
  ring->SetColorId(visual_style::kFocusRing);
}

std::unique_ptr<views::Label> CreateFieldErrorLabel() {
  auto label = std::make_unique<views::Label>();
  label->SetSubpixelRenderingEnabled(false);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kErrorText);
  label->SetVisible(false);
  return label;
}

void ShowFieldError(views::Textfield& field,
                    views::Label& error_label,
                    const std::u16string& message) {
  const bool invalid = !message.empty();
  field.SetInvalid(invalid);
  error_label.SetText(message);
  error_label.SetVisible(invalid);
  if (!invalid) {
    field.GetViewAccessibility().RemoveDescription();
    return;
  }
  field.GetViewAccessibility().SetDescription(message);
  field.GetViewAccessibility().AnnounceAlert(message);
  if (field.GetWidget()) {
    field.RequestFocus();
  }
}

}  // namespace ahoi::dialog_style
