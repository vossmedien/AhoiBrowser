// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SHELL_FLOATING_BROWSER_VIEW_BROWSERTEST_SUPPORT_H_
#define AHOI_BROWSER_UI_SHELL_FLOATING_BROWSER_VIEW_BROWSERTEST_SUPPORT_H_

#include <set>

#include "ui/gfx/geometry/rect.h"

namespace ahoi::sidebar {
class SidebarTreeView;
}  // namespace ahoi::sidebar

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace ui {
class OSExchangeData;
}  // namespace ui

namespace views {
class View;
}  // namespace views

// View-tree searches the floating browser view browser tests use to find
// sidebar drag sources, drop targets and projected open tabs (split from
// floating_browser_view_browsertest.cc, source line budget).
namespace ahoi::test_support {

// The first visible open-tab row under `root` that can start a drag.
views::View* FindDraggableDescendant(views::View* root);

// The deepest visible view under `root` that accepts `data` as a drop.
views::View* FindAcceptingDropDescendant(views::View* root,
                                         const ui::OSExchangeData& data);

gfx::Rect BoundsInTarget(views::View* view, views::View* target);

bool IsInsideOrEqual(views::View* ancestor, views::View* candidate);

// Every open tab projected as a sidebar row under `root`.
void CollectProjectedOpenTabs(views::View* root,
                              std::set<tabs::TabInterface*>* open_tabs);

ahoi::sidebar::SidebarTreeView* FindSidebarTreeView(views::View* root);

}  // namespace ahoi::test_support

#endif  // AHOI_BROWSER_UI_SHELL_FLOATING_BROWSER_VIEW_BROWSERTEST_SUPPORT_H_
