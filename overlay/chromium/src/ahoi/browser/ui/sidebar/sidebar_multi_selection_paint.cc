// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_multi_selection_paint.h"

#include "ahoi/browser/ui/visual_style.h"
#include "cc/paint/paint_flags.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"

namespace ahoi::sidebar {
void PaintSidebarMultiSelection(gfx::Canvas* canvas,
                                const ui::ColorProvider* colors,
                                const gfx::Rect& bounds,
                                const gfx::Rect& badge) {
  const SkColor accent = colors->GetColor(visual_style::kAccent);
  gfx::RectF outline(bounds);
  outline.Inset(gfx::InsetsF::VH(visual_style::kSidebarTabRowVerticalInset + 0.5f,
                                visual_style::kSidebarTabRowHorizontalInset + 0.5f));
  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setColor(accent);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(1.0f);
  canvas->DrawRoundRect(outline, visual_style::kRowCornerRadius - 0.5f, stroke);
  if (!badge.IsEmpty()) {
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(vector_icons::kCheckCircleFilledIcon, 16, accent),
        badge.CenterPoint().x() - 8, badge.CenterPoint().y() - 8);
  }
}
}  // namespace ahoi::sidebar
