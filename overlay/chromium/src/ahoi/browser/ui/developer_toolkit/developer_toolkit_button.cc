// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"

#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/border.h"

namespace ahoi {

DeveloperToolkitButton::DeveloperToolkitButton(PressedCallback callback,
                                               std::u16string text,
                                               const gfx::VectorIcon* icon)
    : views::LabelButton(std::move(callback), std::move(text)) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetRequestFocusOnPress(true);
  SetTextSubpixelRenderingEnabled(false);
  SetAnimateOnStateChange(true);
  SetHasInkDropActionOnClick(false);
  SetShowInkDropWhenHotTracked(false);
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetImageLabelSpacing(10);
  if (icon) {
    SetImageModel(STATE_NORMAL, ui::ImageModel::FromVectorIcon(
                                    *icon, visual_style::kMutedText,
                                    visual_style::kDeveloperToolkitIconSize));
    SetImageModel(STATE_HOVERED, ui::ImageModel::FromVectorIcon(
                                     *icon, visual_style::kText,
                                     visual_style::kDeveloperToolkitIconSize));
    SetImageModel(STATE_PRESSED, ui::ImageModel::FromVectorIcon(
                                     *icon, visual_style::kText,
                                     visual_style::kDeveloperToolkitIconSize));
    SetImageModel(STATE_DISABLED, ui::ImageModel::FromVectorIcon(
                                      *icon, visual_style::kDisabledIcon,
                                      visual_style::kDeveloperToolkitIconSize));
  }
  SetTextColor(STATE_NORMAL, visual_style::kText);
  SetTextColor(STATE_HOVERED, visual_style::kText);
  SetTextColor(STATE_PRESSED, visual_style::kText);
  SetTextColor(STATE_DISABLED, visual_style::kMutedText);
  SetPreferredSize(gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
}

DeveloperToolkitButton::~DeveloperToolkitButton() = default;

void DeveloperToolkitButton::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  SchedulePaint();
}

void DeveloperToolkitButton::PaintButtonContents(gfx::Canvas* canvas) {
  const auto* colors = GetColorProvider();
  const ButtonState state = GetState();
  const ui::ColorId surface_id =
      state == STATE_DISABLED               ? visual_style::kChromeSurface
      : selected_ || state == STATE_PRESSED ? visual_style::kSelectedSurface
      : state == STATE_HOVERED              ? visual_style::kHoverSurface
                                            : visual_style::kRaisedSurface;

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setStyle(cc::PaintFlags::kFill_Style);
  fill.setColor(colors->GetColor(surface_id));
  const float radius = visual_style::kRowCornerRadius;
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), radius, fill);

  cc::PaintFlags outline;
  outline.setAntiAlias(true);
  outline.setStyle(cc::PaintFlags::kStroke_Style);
  const bool emphasized =
      state != STATE_DISABLED && (selected_ || state == STATE_PRESSED);
  outline.setStrokeWidth(emphasized ? 1.5f : 1.0f);
  const ui::ColorId border_color =
      emphasized ? visual_style::kAccent : visual_style::kDivider;
  outline.setColor(colors->GetColor(border_color));
  gfx::RectF outline_bounds(GetLocalBounds());
  outline_bounds.Inset(emphasized ? 0.75f : 0.5f);
  canvas->DrawRoundRect(outline_bounds, radius - (emphasized ? 0.75f : 0.5f),
                        outline);

  if (HasFocus() && state != STATE_DISABLED) {
    cc::PaintFlags focus;
    focus.setAntiAlias(true);
    focus.setStyle(cc::PaintFlags::kStroke_Style);
    focus.setStrokeWidth(2.0f);
    focus.setColor(colors->GetColor(visual_style::kFocusRing));
    gfx::RectF focus_bounds(GetLocalBounds());
    focus_bounds.Inset(gfx::InsetsF(2.0f));
    canvas->DrawRoundRect(focus_bounds, radius - 2.0f, focus);
  }

  views::LabelButton::PaintButtonContents(canvas);
}

BEGIN_METADATA(DeveloperToolkitButton)
END_METADATA

}  // namespace ahoi
