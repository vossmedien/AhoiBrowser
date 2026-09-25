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
  kToggleAutoPeek,
  kArchiveTemporaryTab,
  kArchiveList,
  kArchivePolicy,
  kRestoreArchiveOriginal,
  kRestoreArchiveElsewhere,
  kGoToSavedHome,
  kSetSavedHome,
  kCopyActivePageLink,
  kCopyActivePageMarkdownLink,
  kOpenActivePageInReadingMode,
};

constexpr int kArchivePolicyCommandBase = 600;

// ADR 0011 step 2, in a fully separated Workspace's window: loads the main
// Profile when its Workspaces are not known yet, or presents the main window
// with one of its Workspaces. Between the archive policies (600..604) and
// the separated Workspaces; at most 99 items each.
constexpr int kOpenMainWorkspacesCommand = 700;
constexpr int kOpenMainWorkspaceCommandBase = 800;
// Presents a fully separated Workspace's window; below the Workspace range.
constexpr int kOpenIsolatedWorkspaceCommandBase = 900;
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
  kArchive,
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
