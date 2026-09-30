// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_menu_presence.h"

#include <algorithm>
#include <vector>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

namespace {

std::vector<raw_ptr<const ScopedSidebarMenu>>& OpenMenus() {
  static base::NoDestructor<std::vector<raw_ptr<const ScopedSidebarMenu>>>
      menus;
  return *menus;
}

}  // namespace

ScopedSidebarMenu::ScopedSidebarMenu(views::View* anchor) : anchor_(anchor) {
  CHECK(anchor);
  OpenMenus().push_back(this);
}

ScopedSidebarMenu::~ScopedSidebarMenu() {
  std::erase(OpenMenus(), this);
}

bool IsSidebarMenuShowingWithin(const views::View* root) {
  return root && std::ranges::any_of(OpenMenus(), [root](const auto& menu) {
           const views::View* const anchor = menu->anchor();
           return anchor && root->Contains(anchor);
         });
}

}  // namespace ahoi::sidebar
