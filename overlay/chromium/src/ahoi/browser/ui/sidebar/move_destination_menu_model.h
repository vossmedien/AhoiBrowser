// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_MOVE_DESTINATION_MENU_MODEL_H_
#define AHOI_BROWSER_UI_SIDEBAR_MOVE_DESTINATION_MENU_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/uuid.h"

namespace ahoi::sidebar {

// A pure, UI-independent representation of the nested destinations shown by
// the sidebar's "Move to" context menu. A non-selectable folder remains in
// the hierarchy when it contains selectable descendants; this is important
// when the source is already inside that folder.
struct MoveDestinationFolder {
  base::Uuid id;
  std::u16string title;
  bool selectable = true;
  std::vector<MoveDestinationFolder> children;

  bool operator==(const MoveDestinationFolder&) const = default;
};

struct MoveDestinationWorkspace {
  base::Uuid id;
  std::u16string name;
  std::u16string icon;
  bool root_selectable = true;
  std::vector<MoveDestinationFolder> folders;

  bool operator==(const MoveDestinationWorkspace&) const = default;
};

// Builds destinations in the supplied workspace order and folder hierarchy.
// The source, all of its descendants, and its current parent/root destination
// are not selectable, preventing cycles and no-op moves before a command is
// ever exposed by the native menu.
std::vector<MoveDestinationWorkspace> BuildMoveDestinationMenuModel(
    const std::vector<tab_tree::Workspace>& ordered_workspaces,
    const tab_tree::TabTreeSnapshot& snapshot,
    const tab_tree::TreeNode* source);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_MOVE_DESTINATION_MENU_MODEL_H_
