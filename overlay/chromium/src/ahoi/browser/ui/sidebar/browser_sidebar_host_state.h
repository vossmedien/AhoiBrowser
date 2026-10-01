// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_STATE_H_
#define AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_STATE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/workspace_directory_order.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_types.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view_tracker.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace ui {
class SimpleMenuModel;
}  // namespace ui

namespace views {
class BubbleDialogDelegate;
class Button;
class ImageButton;
class Checkbox;
class Label;
class RadioButton;
class Textfield;
class View;
class Widget;
}  // namespace views

// Per-subsystem state of BrowserSidebarHostView, grouped so the host class
// declares one member per subsystem (split from browser_sidebar_host_view.h,
// source line budget). Field order and defaults are unchanged, so each
// group is destroyed in the same order as before.
namespace ahoi::sidebar {

class BrowserSidebarHostView;
class CachedTabThumbnail;

// ADR 0011 step 2 (handoff 048): one switcher over the Workspaces of all
// Profiles, in the process-wide order. `own` entries belong to this
// window's Profile and switch inside it; the others hand the frame over.
struct SwitcherWorkspace {
  session::DirectoryWorkspace key;
  std::u16string name;
  std::u16string icon;
  std::optional<uint32_t> accent_argb;
  bool own = false;
  bool own_website_sessions = false;
};

// ADR 0011 WS-ISO-05: an item on its way to another Profile's Workspace.
struct CrossLevelMoveRequest {
  CrossLevelMoveRequest();
  CrossLevelMoveRequest(const CrossLevelMoveRequest&);
  CrossLevelMoveRequest& operator=(const CrossLevelMoveRequest&);
  ~CrossLevelMoveRequest();

  std::vector<base::Uuid> root_ids;
  SwitcherWorkspace target;
  // The window's active tab moves, so the window follows it.
  bool follow = false;
  // The window of the target Profile where the item was dropped, if any.
  base::WeakPtr<BrowserSidebarHostView> presenter;
};

enum class SidebarDiscoveryPrimaryResultKind {
  kTreeNode,
  kDeviceTab,
  kRuntimeTab,
};

struct SidebarDiscoveryPrimaryResult {
  SidebarDiscoveryPrimaryResultKind kind =
      SidebarDiscoveryPrimaryResultKind::kTreeNode;
  base::Uuid node_id;
  std::string device_tab_stable_id;
  int runtime_tab_handle = -1;
  raw_ptr<views::View> row = nullptr;
};

// Search and keyboard selection of the sidebar discovery surface.
struct SidebarDiscoveryState {
  SidebarDiscoveryState();
  ~SidebarDiscoveryState();

  views::ViewTracker focus_restore_tracker;
  std::u16string query;
  std::set<int> runtime_tab_handles;
  std::set<std::string> device_tab_ids;
  std::vector<SidebarDiscoveryPrimaryResult> primary_results;
  std::optional<size_t> primary_selection;
  std::optional<int> scroll_offset;
  std::optional<base::Uuid> selection_before_search;
  bool activation_committed = false;
};

struct SavedTabThumbnailSnapshot {
  GURL url;
  gfx::ImageSkia image;
  uint64_t recency = 0;
};

// Live tab thumbnails and snapshots kept for saved pages.
struct SidebarThumbnailState {
  SidebarThumbnailState();
  ~SidebarThumbnailState();

  std::map<int, std::unique_ptr<CachedTabThumbnail>> tab_cache;
  std::map<base::Uuid, SavedTabThumbnailSnapshot> saved_snapshots;
  uint64_t saved_recency = 0;
};

// The hover bubble listing a group's recently opened links.
struct SidebarGroupRecentState {
  SidebarGroupRecentState();
  ~SidebarGroupRecentState();

  views::ViewTracker anchor_tracker;
  std::optional<base::Uuid> hovered_folder_id;
  std::optional<base::Uuid> bubble_folder_id;
  base::OneShotTimer show_timer;
  base::OneShotTimer hide_timer;
  base::CancelableTaskTracker history_task_tracker;
  uint64_t query_generation = 0;
  bool bubble_hovered = false;
  raw_ptr<views::View> links_view = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> delegate;
  std::unique_ptr<views::Widget> widget;
};

// The group create/edit dialog.
struct SidebarGroupDialogState {
  SidebarGroupDialogState();
  ~SidebarGroupDialogState();

  PendingGroupAction action = PendingGroupAction::kNone;
  std::optional<base::Uuid> source_id;
  std::optional<base::Uuid> parent_id;
  std::optional<int> runtime_tab_handle;
  std::u16string icon;
  std::optional<uint32_t> accent_argb;
  raw_ptr<views::Textfield> name_field = nullptr;
  raw_ptr<views::Textfield> icon_field = nullptr;
  std::vector<std::pair<raw_ptr<views::ImageButton>, std::u16string>>
      icon_buttons;
  std::vector<std::pair<raw_ptr<views::Button>, std::optional<uint32_t>>>
      color_buttons;
  std::unique_ptr<views::BubbleDialogDelegate> delegate;
  std::unique_ptr<views::Widget> widget;
};

// The Workspace create/edit dialog.
struct SidebarWorkspaceDialogState {
  SidebarWorkspaceDialogState();
  ~SidebarWorkspaceDialogState();

  PendingWorkspaceAction action = PendingWorkspaceAction::kNone;
  std::optional<base::Uuid> workspace_id;
  std::optional<uint32_t> accent_argb;
  raw_ptr<views::Textfield> name_field = nullptr;
  // Written-out validation error below `name_field`; hidden until needed.
  raw_ptr<views::Label> name_error = nullptr;
  raw_ptr<views::Textfield> icon_field = nullptr;
  // ADR 0011 level choice; only present while creating a Workspace.
  raw_ptr<views::RadioButton> own_sessions_radio = nullptr;
  raw_ptr<views::RadioButton> isolated_radio = nullptr;
  // ADR 0012 merge: the target and the folder choice.
  std::optional<base::Uuid> merge_target_id;
  raw_ptr<views::Checkbox> merge_into_folder = nullptr;
  std::vector<std::pair<raw_ptr<views::Button>, std::optional<uint32_t>>>
      color_buttons;
  std::unique_ptr<views::BubbleDialogDelegate> delegate;
  std::unique_ptr<views::Widget> widget;
};

// Targets of the open context menu.
struct SidebarContextMenuState {
  SidebarContextMenuState();
  ~SidebarContextMenuState();

  std::optional<base::Uuid> node_id;
  base::WeakPtr<tabs::TabInterface> runtime_tab;
  base::WeakPtr<content::WebContents> page_action_contents;
  int page_action_navigation_id = 0;
  GURL page_action_url;
  std::vector<base::Uuid> workspace_ids;
  // Workspace menu command -> position in the shared switcher, for the
  // Cmd+1..9 hints (handoff 048).
  std::map<int, size_t> workspace_positions;
  // Profile directories behind the menu's fully separated Workspace items.
  std::vector<std::string> isolated_workspace_dirs;
  // Main Profile Workspaces listed in a fully separated Workspace's window.
  std::vector<base::Uuid> main_workspace_ids;
  bool offers_main_workspaces = false;
  // WS-ISO-18: other Profiles' Workspaces that play audio, for "pause".
  std::vector<session::DirectoryWorkspace> media_pause_targets;
  std::vector<ContextMoveDestination> move_destinations;
  // WS-ISO-05: other Profiles' Workspaces in "Move to", and the item.
  std::vector<SwitcherWorkspace> cross_level_targets;
  std::vector<base::Uuid> cross_level_roots;
  ContextMenuScope scope = ContextMenuScope::kNone;
  // Declared before `model`, which refers to it as a submenu.
  std::unique_ptr<ui::SimpleMenuModel> split_arrange_model;
  std::unique_ptr<ui::SimpleMenuModel> model;
  std::unique_ptr<ui::SimpleMenuModel> archive_policy_model;
  std::optional<base::Uuid> archive_id;
  std::optional<base::Uuid> archive_workspace_id;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_STATE_H_
