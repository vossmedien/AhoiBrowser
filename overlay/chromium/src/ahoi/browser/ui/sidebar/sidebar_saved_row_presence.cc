// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_saved_row_presence.h"

namespace ahoi::sidebar {

bool ShouldHideClosedTemporaryPageRow(const tab_tree::TreeNode& node,
                                      const SavedRowRuntimeFacts& facts) {
  return node.type == tab_tree::TreeNodeType::kSavedPage && !node.tombstone &&
         node.is_temporary && !facts.has_live_tab && !facts.archived &&
         !facts.created_on_other_device && !facts.restored_from_archive;
}

}  // namespace ahoi::sidebar
