// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TABS_SURFACE_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TABS_SURFACE_VIEW_H_

#include <memory>

namespace views {
class View;
}

namespace ahoi::sidebar {

// Creates the ScrollView contents surface used by the sidebar tab list. The
// surface keeps its vertical preferred size while matching the live viewport
// width, so flexed headers remain usable after a native sidebar resize.
std::unique_ptr<views::View> CreateSidebarTabsSurfaceView();

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TABS_SURFACE_VIEW_H_
