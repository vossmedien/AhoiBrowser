// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_views.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "base/check.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/view.h"

namespace ahoi::appearance {

namespace {

// A surface's translucency belongs to its background paint, never to the
// containing View layer. Applying opacity to a container layer also fades
// labels, icons and focus rings. Resolving the ColorId at paint time keeps
// this background in sync with light/dark/high-contrast ColorProviders.
class AlphaRoundedRectBackground final : public views::Background {
 public:
  AlphaRoundedRectBackground(ui::ColorId color_id,
                             float corner_radius,
                             uint8_t alpha)
      : color_id_(color_id), corner_radius_(corner_radius), alpha_(alpha) {}

  AlphaRoundedRectBackground(const AlphaRoundedRectBackground&) = delete;
  AlphaRoundedRectBackground& operator=(const AlphaRoundedRectBackground&) =
      delete;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(SkColorSetA(view->GetColorProvider()->GetColor(color_id_),
                               alpha_));
    canvas->DrawRoundRect(gfx::RectF(view->GetLocalBounds()), corner_radius_,
                          flags);
  }

  void OnViewThemeChanged(views::View* view) override {
    view->SchedulePaint();
  }

  std::optional<gfx::RoundedCornersF> GetRoundedCornerRadii() const override {
    return gfx::RoundedCornersF(corner_radius_);
  }

 private:
  const ui::ColorId color_id_;
  const float corner_radius_;
  const uint8_t alpha_;
};

uint8_t OpacityToAlpha(float opacity) {
  const float clamped = std::clamp(opacity, 0.0f, 1.0f);
  return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

}  // namespace

void ApplySurfaceBackgroundAppearance(
    views::View* view,
    const SurfaceAppearance& appearance,
    SurfaceCornerOwnership corner_ownership) {
  CHECK(view);
  if (appearance.uses_glass()) {
    view->SetBackground(std::make_unique<AlphaRoundedRectBackground>(
        appearance.background_color, appearance.corner_radius,
        OpacityToAlpha(appearance.opacity)));
  } else {
    view->SetBackground(views::CreateRoundedRectBackground(
        appearance.background_color, appearance.corner_radius));
  }
  view->SetPaintToLayer();
  ApplySurfaceLayerAppearance(view->layer(), appearance, corner_ownership);
}

void ClearSurfaceBackgroundAppearance(
    views::View* view,
    SurfaceCornerOwnership corner_ownership) {
  CHECK(view);
  view->SetBackground(nullptr);
  if (!view->layer()) {
    return;
  }
  view->layer()->SetFillsBoundsOpaquely(false);
  view->layer()->SetOpacity(1.0f);
  view->layer()->SetBackgroundBlur(0.0f);
  view->layer()->SetBackdropFilterQuality(0.0f);
  if (corner_ownership == SurfaceCornerOwnership::kAppearance) {
    view->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF());
    view->layer()->SetIsFastRoundedCorner(false);
  }
}

void ApplySurfaceAppearance(views::View* view,
                            const SurfaceAppearance& appearance,
                            SurfaceCornerOwnership corner_ownership) {
  ApplySurfaceBackgroundAppearance(view, appearance, corner_ownership);
  if (appearance.border_thickness > 0) {
    view->SetBorder(views::CreateRoundedRectBorder(
        appearance.border_thickness, appearance.corner_radius,
        appearance.border_color));
  } else {
    // Semantic glass surfaces are separated by depth and translucency. A
    // persistent one-pixel outline makes the sidebar and command surface look
    // like nested Chromium dialogs and leaves visible seams at rounded edges.
    view->SetBorder(nullptr);
  }
}

void ApplySurfaceLayerAppearance(ui::Layer* layer,
                                 const SurfaceAppearance& appearance,
                                 SurfaceCornerOwnership corner_ownership) {
  CHECK(layer);
  if (corner_ownership == SurfaceCornerOwnership::kAppearance) {
    const gfx::RoundedCornersF radii(std::max(0, appearance.corner_radius));
    // Rounded background paint alone does not clip the compositor's backdrop
    // output or independently layered descendants. Keep blur INPUT sampling
    // rectangular; Layer applies these corners to the final rendered output.
    layer->SetRoundedCornerRadius(radii);
    layer->SetIsFastRoundedCorner(!radii.IsEmpty());
  }
  // Even an opaque material leaves transparent pixels outside a rounded
  // background. Advertising the whole rectangle as opaque lets the compositor
  // cull pixels which should remain visible through those corners.
  layer->SetFillsBoundsOpaquely(
      !appearance.uses_glass() && appearance.corner_radius <= 0 &&
      layer->rounded_corner_radii().IsEmpty());
  // Keep the layer fully opaque so child content remains crisp. The alpha is
  // painted only by AlphaRoundedRectBackground above.
  layer->SetOpacity(1.0f);
  layer->SetBackgroundBlur(appearance.uses_glass()
                               ? appearance.background_blur_sigma
                               : 0.0f);
  layer->SetBackdropFilterQuality(appearance.uses_glass() ? 0.8f : 0.0f);
}

}  // namespace ahoi::appearance
