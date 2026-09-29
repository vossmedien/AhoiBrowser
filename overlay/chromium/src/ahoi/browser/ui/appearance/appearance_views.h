// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_VIEWS_H_
#define AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_VIEWS_H_

#include "ahoi/browser/ui/appearance/appearance_policy.h"

namespace ui {
class Layer;
}  // namespace ui

namespace views {
class View;
}  // namespace views

namespace ahoi::appearance {

// Some surfaces (the docked/floating sidebar) get their final output geometry
// from their parent layout. Material updates must not replace those corners.
enum class SurfaceCornerOwnership {
  kAppearance,
  kCaller,
};

// Applies the material background and compositor treatment while preserving
// any existing border. This is useful for Chromium dialog client views whose
// border participates in layout.
void ApplySurfaceBackgroundAppearance(
    views::View* view,
    const SurfaceAppearance& appearance,
    SurfaceCornerOwnership corner_ownership =
        SurfaceCornerOwnership::kAppearance);

// Clears background-only material state and appearance-owned corners from a
// former surface host. Borders, transforms, explicit clips/masks and child/
// shadow hierarchies are unchanged; caller-owned corners are preserved.
void ClearSurfaceBackgroundAppearance(
    views::View* view,
    SurfaceCornerOwnership corner_ownership =
        SurfaceCornerOwnership::kAppearance);

// Applies semantic background/border roles and the resolved layer treatment to
// a View. The caller owns the View and may reapply this after AppearanceState
// changes. ColorIds are passed directly to Views backgrounds so active light,
// dark and high-contrast ColorProviders continue to update the surface.
void ApplySurfaceAppearance(views::View* view,
                            const SurfaceAppearance& appearance,
                            SurfaceCornerOwnership corner_ownership =
                                SurfaceCornerOwnership::kAppearance);

// Applies only the compositing portion. Useful for an existing custom-painted
// View or a shell-owned Layer that already has its own background painter.
void ApplySurfaceLayerAppearance(ui::Layer* layer,
                                 const SurfaceAppearance& appearance,
                                 SurfaceCornerOwnership corner_ownership =
                                     SurfaceCornerOwnership::kAppearance);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_VIEWS_H_
