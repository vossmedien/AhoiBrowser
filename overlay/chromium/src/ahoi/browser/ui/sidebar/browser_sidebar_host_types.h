// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_TYPES_H_
#define AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_TYPES_H_

#include <optional>

#include "base/uuid.h"

namespace ahoi::sidebar {

enum SidebarContextMenuCommand {
  kActivateNode = 1,
  kToggleGroupExpanded,
  kCreateRootGroup,
  kCreateSubgroup,
  kCreateGroupAroundNode,
  kDuplicateNode,
  kRenameNode,
  kDeleteNode,
  kSeparateSplit,
  kSaveTemporaryTab,
  kKeepOpenOnly,
  kCloseRuntimeTab,
  kSplitSideBySide,
  kSplitStacked,
  kReverseSplit,
  kCustomizeGroup,
  kCopyAllLinks,
  kMoveTo,
  kCreateWorkspace,
  kDuplicateWorkspace,
  kEditWorkspace,
  kDeleteWorkspace,
  kToggleFloatingSidebar,
  kToggleSidebarVisibility,
  kRestoreSidebar,
  kSleepTab,
  kWakeTab,
  kToggleNeverSleep,
  kToggleWorkspaceSwipe,
  kToggleCmdScrollTabSwitching,
  kToggleMiddleClickAutoscroll,
};

constexpr int kActivateWorkspaceCommandBase = 1000;
constexpr int kMoveToDestinationCommandBase = 2000;
// The persistent tree supports far more than one thousand folders. Keep
// submenu identifiers well above the destination range so a large workspace
// cannot make a destination look like a submenu command.
constexpr int kMoveToWorkspaceSubmenuCommandBase = 1000000;

struct ContextMoveDestination {
  base::Uuid workspace_id;
  std::optional<base::Uuid> folder_id;
};

enum class ContextMenuScope {
  kNone = 0,
  kTree,
  kWorkspace,
  kOpenTab,
};

enum class PendingGroupAction {
  kNone = 0,
  kWrapNode,
  kWrapTemporaryTab,
  kCreateFolder,
  kEditFolder,
};

enum class PendingWorkspaceAction {
  kNone = 0,
  kCreate,
  kDuplicate,
  kEdit,
  kDelete,
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_TYPES_H_
