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
  const bool detaching = intent_->action == DropAction::kDetachFromSplit;
  constexpr float kRadius = 12.0f;

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColorSetA(accent, detaching ? 0x48 : 0x38));
  fill.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(highlight, kRadius, fill);

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setColor(SkColorSetA(accent, 0xE6));
  stroke.setStrokeWidth(3.0f);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  canvas->DrawRoundRect(highlight, kRadius, stroke);

  if (detaching) {
    gfx::RectF outer_edge = highlight;
    constexpr float kEdgeMarkerThickness = 6.0f;
    if (intent_->zone == DropZone::kLeft) {
      outer_edge.set_width(kEdgeMarkerThickness);
    } else if (intent_->zone == DropZone::kRight) {
      outer_edge.set_x(outer_edge.right() - kEdgeMarkerThickness);
      outer_edge.set_width(kEdgeMarkerThickness);
    } else if (intent_->zone == DropZone::kTop) {
      outer_edge.set_height(kEdgeMarkerThickness);
    } else {
      outer_edge.set_y(outer_edge.bottom() - kEdgeMarkerThickness);
      outer_edge.set_height(kEdgeMarkerThickness);
    }
    cc::PaintFlags edge_marker;
    edge_marker.setAntiAlias(true);
    edge_marker.setColor(SkColorSetA(accent, 0xF2));
    edge_marker.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(outer_edge, kEdgeMarkerThickness / 2.0f,
                          edge_marker);
  }
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
