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

void SplitDropOverlayView::BeginDragPresentation() {
  if (drag_active_) {
    return;
  }
  drag_active_ = true;
  SetVisible(true);
  SchedulePaint();
}

void SplitDropOverlayView::SetIntent(const DropIntent& intent) {
  BeginDragPresentation();
  intent_ = intent;
  SchedulePaint();
}

void SplitDropOverlayView::ClearIntent() {
  if (!intent_.has_value()) {
    return;
  }
  intent_.reset();
  SchedulePaint();
}

void SplitDropOverlayView::EndDragPresentation() {
  if (!drag_active_ && !intent_.has_value()) {
    return;
  }
  drag_active_ = false;
  intent_.reset();
  SetVisible(false);
}

void SplitDropOverlayView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!drag_active_ || !GetColorProvider()) {
    return;
  }

  const SkColor accent = GetColorProvider()->GetColor(visual_style::kAccent);
  gfx::RectF active_bounds(GetLocalBounds());
  active_bounds.Inset(3.0f);
  constexpr float kActiveRadius =
      static_cast<float>(visual_style::kSplitPaneCornerRadius);

  // Keep the base state obvious but restrained: it confirms that the complete
  // split surface is participating without obscuring the rendered pages.
  cc::PaintFlags active_fill;
  active_fill.setAntiAlias(true);
  active_fill.setColor(SkColorSetA(accent, 0x18));
  active_fill.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(active_bounds, kActiveRadius, active_fill);

  cc::PaintFlags active_stroke;
  active_stroke.setAntiAlias(true);
  active_stroke.setColor(SkColorSetA(accent, 0xB8));
  active_stroke.setStrokeWidth(2.0f);
  active_stroke.setStyle(cc::PaintFlags::kStroke_Style);
  canvas->DrawRoundRect(active_bounds, kActiveRadius, active_stroke);

  if (!intent_.has_value() || intent_->highlight_bounds.IsEmpty()) {
    return;
  }

  const gfx::RectF highlight(intent_->highlight_bounds);
  const bool detaching = intent_->action == DropAction::kDetachFromSplit;
  constexpr float kTargetRadius = 12.0f;

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColorSetA(accent, detaching ? 0x5C : 0x4A));
  fill.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(highlight, kTargetRadius, fill);

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setColor(SkColorSetA(accent, 0xF2));
  stroke.setStrokeWidth(3.0f);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  canvas->DrawRoundRect(highlight, kTargetRadius, stroke);

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
