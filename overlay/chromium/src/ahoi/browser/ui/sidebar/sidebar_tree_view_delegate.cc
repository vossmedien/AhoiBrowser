// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"

namespace ahoi::sidebar {

bool SidebarTreeViewDelegate::IsSavedPageBookmarked(
    const tab_tree::TreeNode&) const {
  return false;
}

}  // namespace ahoi::sidebar
