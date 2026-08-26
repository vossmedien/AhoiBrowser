// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/empty_state/empty_state_view.h"

#include <memory>

#include "ahoi/browser/ui/visual_style.h"
#include "cc/paint/paint_flags.h"
#include "chrome/grit/generated_resources.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace ahoi::empty_state {
namespace {

class EmptyStateView final : public views::View {
 public:
  EmptyStateView() {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kPane);
    GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_AHOI_EMPTY_WORKSPACE_ACCESSIBLE_NAME));
  }

  EmptyStateView(const EmptyStateView&) = delete;
  EmptyStateView& operator=(const EmptyStateView&) = delete;
  ~EmptyStateView() override = default;

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }

    cc::PaintFlags background;
    background.setStyle(cc::PaintFlags::kFill_Style);
    background.setColor(colors->GetColor(visual_style::kChromeSurface));
    canvas->DrawRect(GetLocalBounds(), background);

    // Keep the empty state intentionally quiet: a small theme-resolved mark
    // gives the surface a stable visual anchor without introducing a fake tab,
    // a fake URL, or a competing call-to-action.
    const gfx::Rect bounds = GetLocalBounds();
    if (bounds.width() < 80 || bounds.height() < 80) {
      return;
    }

    const gfx::PointF center(bounds.CenterPoint());
    cc::PaintFlags halo;
    halo.setAntiAlias(true);
    halo.setStyle(cc::PaintFlags::kFill_Style);
    halo.setColor(colors->GetColor(visual_style::kRaisedSurface));
    canvas->DrawCircle(center, 28.0f, halo);

    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(2.0f);
    ring.setColor(colors->GetColor(visual_style::kMutedText));
    canvas->DrawCircle(center, 10.0f, ring);

    cc::PaintFlags mark;
    mark.setAntiAlias(true);
    mark.setStyle(cc::PaintFlags::kFill_Style);
    mark.setColor(colors->GetColor(visual_style::kAccent));
    canvas->DrawCircle(gfx::PointF(center.x() + 8.0f, center.y() - 8.0f), 3.0f,
                       mark);
  }
};

}  // namespace

std::unique_ptr<views::View> CreateEmptyStateView() {
  return std::make_unique<EmptyStateView>();
}

}  // namespace ahoi::empty_state
