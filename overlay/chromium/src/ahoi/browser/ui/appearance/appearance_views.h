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

// Applies the material background and compositor treatment while preserving
// any existing border. This is useful for Chromium dialog client views whose
// border participates in layout.
void ApplySurfaceBackgroundAppearance(
    views::View* view,
    const SurfaceAppearance& appearance);

// Clears background-only material state from a former surface host without
// changing its border or child hierarchy.
void ClearSurfaceBackgroundAppearance(views::View* view);

// Applies semantic background/border roles and the resolved layer treatment to
// a View. The caller owns the View and may reapply this after AppearanceState
// changes. ColorIds are passed directly to Views backgrounds so active light,
// dark and high-contrast ColorProviders continue to update the surface.
void ApplySurfaceAppearance(views::View* view,
                            const SurfaceAppearance& appearance);

// Applies only the compositing portion. Useful for an existing custom-painted
// View or a shell-owned Layer that already has its own background painter.
void ApplySurfaceLayerAppearance(ui::Layer* layer,
                                 const SurfaceAppearance& appearance);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_VIEWS_H_
