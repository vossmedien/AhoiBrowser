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
  kTogglePeekOnShiftClick,
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
  kConvertWorkspaceToIsolated,
  kMergeWorkspace,
  kArrangeSplit,
};

// ADR 0011 WS-ISO-18: pauses the audio of another Profile's Workspace from
// the Workspace menu; below the merge targets, at most 99.
constexpr int kPauseOtherProfileMediaCommandBase = 400;
// ADR 0012 (handoff 080): "Zusammenführen mit" the Workspace at this index
// of the menu's Workspace list; below the archive policies, at most 99.
constexpr int kMergeWorkspaceCommandBase = 500;
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
// ADR 0011 WS-ISO-05: "Move to" another Profile's Workspace; at most 99,
// above the Workspace activations and below the destinations.
// ⌘/⇧-click multi-selection menu: fixed actions, then one "Move to" item per
// Workspace (at most 89), above the activations and below cross-level moves.
constexpr int kMultiOpenInSplitCommand = 1800;
constexpr int kMultiCloseTabsCommand = 1801;
constexpr int kMultiArchiveCommand = 1802;
constexpr int kMultiClearSelectionCommand = 1803;
constexpr int kMultiMoveSubmenuCommand = 1809;
constexpr int kMultiMoveToWorkspaceCommandBase = 1810;
constexpr int kCrossLevelMoveCommandBase = 1900;
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
  kMultiSelection,
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
  // ADR 0011 step 2 (handoff 052).
  kConvertToIsolated,
  // ADR 0012 (handoff 080).
  kMerge,
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_TYPES_H_
