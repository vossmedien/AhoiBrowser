// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MENU_PRESENCE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MENU_PRESENCE_H_

#include "ui/views/view_tracker.h"

namespace views {
class View;
}

namespace ahoi::sidebar {

// Marks a menu opened from a sidebar view as showing for as long as the
// token lives. Every sidebar menu owner (bookmark folder menu, bookmark
// context menu, remote-tab menu) holds one exactly while its menu runs, so
// the sidebar can ask whether any menu it opened is still up without
// knowing the owners. UI thread only.
class ScopedSidebarMenu {
 public:
  // `anchor` is the sidebar view the menu was opened from. The token stops
  // counting when that view is destroyed.
  explicit ScopedSidebarMenu(views::View* anchor);
  ScopedSidebarMenu(const ScopedSidebarMenu&) = delete;
  ScopedSidebarMenu& operator=(const ScopedSidebarMenu&) = delete;
  ~ScopedSidebarMenu();

  const views::View* anchor() const { return anchor_.view(); }

 private:
  views::ViewTracker anchor_;
};

// Returns whether a live ScopedSidebarMenu is anchored at `root` or at one
// of its descendants.
bool IsSidebarMenuShowingWithin(const views::View* root);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_MENU_PRESENCE_H_
