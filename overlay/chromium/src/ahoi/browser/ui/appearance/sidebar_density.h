// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_DENSITY_H_
#define AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_DENSITY_H_

#include "ahoi/browser/ui/visual_style.h"

class PrefService;

namespace ahoi::appearance {

inline constexpr char kSidebarDensityPref[] = "ahoi.appearance.sidebar_density";

// Persisted values; never reorder. Standard preserves the existing geometry.
enum class SidebarDensity { kCompact = 0, kStandard = 1, kComfortable = 2 };

struct SidebarDensityMetrics {
  int row_height;
  int font_size_delta;
  int icon_size_delta;
  int row_vertical_inset;
  int split_pane_minimum_height;
};

constexpr SidebarDensity NormalizeSidebarDensity(int value) {
  return value == 0 ? SidebarDensity::kCompact
       : value == 2 ? SidebarDensity::kComfortable
                    : SidebarDensity::kStandard;
}

constexpr SidebarDensityMetrics GetSidebarDensityMetrics(
    SidebarDensity density) {
  switch (density) {
    case SidebarDensity::kCompact:
      return {32, -1, -2, 1, visual_style::kSidebarSplitPaneMinimumHeight};
    case SidebarDensity::kComfortable:
      return {44, 2, 2, 4, visual_style::kSidebarSplitPaneMinimumHeight + 8};
    case SidebarDensity::kStandard:
      break;
  }
  return {visual_style::kSidebarTabRowHeight, 0, 0,
          visual_style::kSidebarTabRowVerticalInset,
          visual_style::kSidebarSplitPaneMinimumHeight};
}

SidebarDensity GetSidebarDensity(const PrefService& prefs);
// Clears only this user setting; policy-controlled settings remain untouched.
bool ResetSidebarDensity(PrefService& prefs);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_DENSITY_H_
