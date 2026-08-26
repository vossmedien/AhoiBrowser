// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_overlay_view.h"

#include "ahoi/browser/ui/visual_style.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view_class_properties.h"

namespace ahoi::split_drop {

SplitDropOverlayView::SplitDropOverlayView() {
  SetCanProcessEventsWithinSubtree(false);
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
}

SplitDropOverlayView::~SplitDropOverlayView() = default;

void SplitDropOverlayView::SetIntent(const DropIntent& intent) {
  intent_ = intent;
  SetVisible(true);
  SchedulePaint();
}

void SplitDropOverlayView::ClearIntent() {
  intent_.reset();
  SetVisible(false);
}

void SplitDropOverlayView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!intent_.has_value() || intent_->highlight_bounds.IsEmpty() ||
      !GetColorProvider()) {
    return;
  }

  const SkColor accent = GetColorProvider()->GetColor(visual_style::kAccent);
  const gfx::RectF highlight(intent_->highlight_bounds);
  constexpr float kRadius = 12.0f;

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColorSetA(accent, 0x38));
  fill.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(highlight, kRadius, fill);

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setColor(SkColorSetA(accent, 0xE6));
  stroke.setStrokeWidth(3.0f);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  canvas->DrawRoundRect(highlight, kRadius, stroke);
}

void SplitDropOverlayView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

std::unique_ptr<SplitDropOverlayView> CreateSplitDropOverlayView() {
  return std::make_unique<SplitDropOverlayView>();
}

BEGIN_METADATA(SplitDropOverlayView)
END_METADATA

}  // namespace ahoi::split_drop
