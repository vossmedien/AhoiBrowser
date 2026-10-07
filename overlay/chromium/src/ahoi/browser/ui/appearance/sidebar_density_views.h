// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_DENSITY_VIEWS_H_
#define AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_DENSITY_VIEWS_H_

#include "ahoi/browser/ui/appearance/sidebar_density.h"

namespace views {
class Label;
class Textfield;
class View;
}  // namespace views

namespace ahoi::appearance {

// The host projects its Profile preference through the existing Views tree.
// No process-global density: separate Profile windows can differ.
SidebarDensityMetrics GetSidebarDensityMetricsForView(const views::View* view);
void SetSidebarDensityForView(views::View* root, SidebarDensity density);
void ApplySidebarDensityFont(views::Label* label);
void ApplySidebarDensityFont(views::Textfield* editor);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_DENSITY_VIEWS_H_
