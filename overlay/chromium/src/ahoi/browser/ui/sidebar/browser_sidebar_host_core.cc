// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tabs_surface_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/pickle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {
BrowserSidebarHostView::BrowserSidebarHostView(
    Browser* browser,
    SessionBridge* session_bridge,
    WorkspaceService* workspace_service,
    ModalOverlayController* modal_overlay_controller)
    : browser_(browser),
      session_bridge_(session_bridge),
      workspace_service_(workspace_service),
      modal_overlay_controller_(modal_overlay_controller),
      tab_strip_model_(browser->tab_strip_model()),
      controller_(std::make_unique<SidebarTreeController>(
          session_bridge->tab_tree_store())) {
  CHECK(modal_overlay_controller_);
  CHECK(browser_);
  CHECK(session_bridge_);
  CHECK(session_bridge_->tab_tree_store());
  CHECK(workspace_service_);
  CHECK(tab_strip_model_);
  favicon_service_ = FaviconServiceFactory::GetForProfile(
      browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  history_service_ = HistoryServiceFactory::GetForProfile(
      browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);

  SetPreferredSize(gfx::Size(visual_style::kSidebarWidthDefault, 480));
  // The appearance resolver installs the themed opaque/glass surface after
  // the child hierarchy exists. Keeping this container transparent here lets
  // the resolver's backdrop blur sample the browser content behind the tree.
  SetBackground(nullptr);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
  GetViewAccessibility().SetName(u"AhoiBrowser");

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(visual_style::kSidebarTopInset,
                        visual_style::kSidebarHorizontalInset,
                        visual_style::kSidebarBottomInset,
                        visual_style::kSidebarHorizontalInset),
      visual_style::kSidebarSectionSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto workspace_header = std::make_unique<views::View>();
  auto* workspace_header_layout =
      workspace_header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          visual_style::kSidebarFooterSpacing));
  workspace_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  workspace_button_ =
      workspace_header->AddChildView(CreateWorkspaceSelectorButton(
          base::BindRepeating(&BrowserSidebarHostView::OnWorkspacePressed,
                              base::Unretained(this))));
  workspace_header_layout->SetFlexForView(workspace_button_, 1);
  workspace_header->AddChildView(CreateSidebarHeaderActionButton(
      base::BindRepeating(&BrowserSidebarHostView::OnSidebarHeaderActionPressed,
                          weak_ptr_factory_.GetWeakPtr(),
                          /*toggle_visibility=*/false),
      kDockToLeftIcon,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_FLOATING_SIDEBAR)));
  workspace_header->AddChildView(CreateSidebarHeaderActionButton(
      base::BindRepeating(&BrowserSidebarHostView::OnSidebarHeaderActionPressed,
                          weak_ptr_factory_.GetWeakPtr(),
                          /*toggle_visibility=*/true),
      kLeftPanelCloseFlippableIcon,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_HIDE_SIDEBAR)));
  AddChildView(std::move(workspace_header));
  SetWorkspaceSelectorPresentation(workspace_button_, u"Ahoi", u"A",
                                   visual_style::kDefaultAccent);
  workspace_button_->set_context_menu_controller(this);

  // New Group is a normal content row. Keeping it out of the workspace
  // header's bounds leaves the selector visible and interactive throughout a
  // native drag and prevents the target from overlapping floating chrome.
  new_group_drop_target_ = AddChildView(CreateNewGroupDropTargetView(
      base::BindRepeating(&BrowserSidebarHostView::ShowCreateGroupDialog,
                          base::Unretained(this)),
      base::BindRepeating(
          &BrowserSidebarHostView::ShowCreateGroupDialogForTemporaryTab,
          base::Unretained(this))));
  SetNewGroupDropTargetVisible(new_group_drop_target_, false);

  auto tabs_surface = CreateSidebarTabsSurfaceView();
  auto* tabs_surface_layout =
      tabs_surface->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  tabs_surface_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto tree = std::make_unique<SidebarTreeView>(
      controller_.get(), this,
      l10n_util::GetStringUTF16(IDS_AHOI_SIDEBAR_TREE_ACCESSIBLE_NAME),
      l10n_util::GetStringUTF16(IDS_AHOI_SPLIT_WITH_PREFIX));
  tree_view_ = tree.get();
  tabs_surface->AddChildView(std::move(tree));

  open_tabs_header_ = tabs_surface->AddChildView(CreateSidebarSectionDivider(
      base::BindRepeating(&BrowserSidebarHostView::CloseAllTemporaryTabs,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_DOWNLOAD_LINK_CLEAR_ALL)));

  auto open_tabs = CreateOpenTabsDropTargetView(base::BindRepeating(
      &BrowserSidebarHostView::MakeSavedPageTemporary, base::Unretained(this)));
  open_tabs->GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  open_tabs->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_TAB_SEARCH_OPEN_TABS));
  open_tabs_container_ = tabs_surface->AddChildView(std::move(open_tabs));
  // ScrollView expands its contents to at least the viewport. Giving the
  // temporary-tab target the remaining height makes the entire free lower
  // sidebar a valid saved-to-temporary drop zone instead of requiring a hit
  // on the narrow list itself.
  tabs_surface_layout->SetFlexForView(open_tabs_container_, 1,
                                      /*use_min_size=*/true);

  auto remote_tabs_header = std::make_unique<views::View>();
  auto* remote_header_layout =
      remote_tabs_header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(10, 8, 4, 8)));
  remote_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* remote_header_label = remote_tabs_header->AddChildView(
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(
          IDS_SIDE_PANEL_TABS_FROM_OTHER_DEVICES_TITLE)));
  remote_header_label->SetSubpixelRenderingEnabled(false);
  remote_header_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  remote_header_label->SetEnabledColor(visual_style::kMutedText);
  remote_tabs_header_ =
      tabs_surface->AddChildView(std::move(remote_tabs_header));
  remote_tabs_header_->SetVisible(false);

  auto remote_tabs = std::make_unique<views::View>();
  auto* remote_tabs_layout =
      remote_tabs->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  remote_tabs_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  remote_tabs_container_ = tabs_surface->AddChildView(std::move(remote_tabs));
  remote_tabs_container_->SetVisible(false);
  // Cross-device rows are part of the tab list, not a detached management
  // page. Keep them immediately below the saved tree and above temporary
  // local tabs; the latter may flex into otherwise unused sidebar height.
  tabs_surface->ReorderChildView(remote_tabs_header_, 1);
  tabs_surface->ReorderChildView(remote_tabs_container_, 2);
  auto* const mini_player_scroll_inset =
      tabs_surface->AddChildView(std::make_unique<views::View>());
  mini_player_scroll_inset->SetPreferredSize(gfx::Size());

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackground(nullptr);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // Re-query SidebarTabsSurfaceView with the live viewport width on every
  // layout pass. Without this, ScrollView keeps the contents' former preferred
  // width after a native sidebar resize, so the separator and "Clear all"
  // action remain at their old x position and are clipped by the new viewport.
  scroll->SetUseContentsPreferredSize(true);
  scroll->SetContents(std::move(tabs_surface));
  media_overlay_view_ = AddChildView(
      CreateMiniPlayerOverlay(std::move(scroll), mini_player_scroll_inset));
  layout->SetFlexForView(media_overlay_view_, 1, /*use_min_size=*/true);

  auto actions = std::make_unique<views::View>();
  auto* actions_layout =
      actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 0),
          visual_style::kSidebarFooterSpacing));
  actions_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  actions_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  const auto add_action = [&](int command_id, const gfx::VectorIcon& icon,
                              int label_id) {
    auto* action = actions->AddChildView(CreateSidebarActionButton(
        base::BindRepeating(&BrowserSidebarHostView::RunBrowserCommand,
                            weak_ptr_factory_.GetWeakPtr(), command_id),
        icon, l10n_util::GetStringUTF16(label_id)));
    actions_layout->SetFlexForView(action, 1);
  };
  auto* split_action = actions->AddChildView(CreateSidebarSplitActionCell(
      base::BindRepeating(&BrowserSidebarHostView::RunBrowserCommand,
                          weak_ptr_factory_.GetWeakPtr(), IDC_NEW_TAB),
      vector_icons::kAddWeight500Icon, l10n_util::GetStringUTF16(IDS_NEW_TAB),
      base::BindRepeating(&BrowserSidebarHostView::RunBrowserCommand,
                          weak_ptr_factory_.GetWeakPtr(),
                          IDC_NEW_INCOGNITO_WINDOW),
      kIncognitoIcon, l10n_util::GetStringUTF16(IDS_NEW_INCOGNITO_WINDOW)));
  actions_layout->SetFlexForView(split_action, 1);
  add_action(IDC_SHOW_DOWNLOADS, vector_icons::kDownloadIcon,
             IDS_DOWNLOAD_HISTORY_TITLE);
  add_action(IDC_SHOW_HISTORY, vector_icons::kHistoryIcon, IDS_HISTORY_MENU);
  add_action(IDC_OPTIONS, vector_icons::kSettingsIcon, IDS_SETTINGS);
  sidebar_actions_ = AddChildView(std::move(actions));

  // The drag-only action overlays the workspace pill at exactly the same
  // bounds. It therefore remains easy to hit without changing the scroll
  // viewport or moving any saved/temporary tab while AppKit owns the drag.

  workspace_service_->AddObserver(this);
  tab_strip_model_->AddObserver(this);
  session_presentation_subscription_ =
      session_bridge_->AddRuntimePresentationChangedCallback(
          base::BindRepeating(
              &BrowserSidebarHostView::OnSessionPresentationChanged,
              base::Unretained(this)));
  window_id_ = session_bridge_->GetWindowId(browser_);
  profile_sync_service_ =
      sync::ProfileSyncServiceFactory::GetForProfile(browser_->GetProfile());
  if (profile_sync_service_) {
    profile_sync_service_->AttachUiBridge(session_bridge_);
    profile_sync_service_->AddObserver(this);
  }
  ActivateInitialWorkspace();
  UpdateWorkspaceSelectorIndicators();
  SynchronizeSelection();
  RefreshRuntimePresentation();

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          browser_->GetProfile()->GetPrefs(),
          base::BindRepeating(&BrowserSidebarHostView::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  PrefService* const prefs = browser_->GetProfile()->GetPrefs();
  if (prefs->FindPreference(appearance::kSidebarPageTintEnabledPref)) {
    page_tint_pref_change_registrar_.Init(prefs);
    page_tint_pref_change_registrar_.Add(
        appearance::kSidebarPageTintEnabledPref,
        base::BindRepeating(&BrowserSidebarHostView::RefreshPageTint,
                            weak_ptr_factory_.GetWeakPtr()));
  }
  OnAppearanceChanged(appearance_signal_source_->policy());
}

void BrowserSidebarHostView::AddedToWidget() {
  views::View::AddedToWidget();
  if (views::Widget* const widget = GetWidget()) {
    widget_drag_observation_.Observe(widget);
  }
}

void BrowserSidebarHostView::RemovedFromWidget() {
  ResetDragPresentation();
  widget_drag_observation_.Reset();
  views::View::RemovedFromWidget();
}

void BrowserSidebarHostView::OnWidgetDragDropWillStart(views::Widget* widget) {
  // Do not clear presentation here. Depending on the macOS/AppKit path,
  // DragController::OnWillStartDragForView can publish the source either just
  // before or just after this Widget notification. Clearing synchronously
  // made the drop targets disappear intermittently for otherwise valid
  // native drags. OnDragDone, every drop callback, widget completion and view
  // removal all remain authoritative cleanup boundaries.
}

void BrowserSidebarHostView::OnWidgetDragDropCompleted(views::Widget* widget) {
  if (widget_drag_observation_.IsObservingSource(widget)) {
    ResetDragPresentation();
  }
}

bool BrowserSidebarHostView::UndoLastMutationIfAvailable() {
  const tab_tree::TabTreeStore::Result result = controller_->UndoLastMutation();
  if (result == tab_tree::TabTreeStore::Result::kNothingToUndo) {
    // Let the focused web/native editor receive Cmd+Z when the sidebar has
    // no durable mutation to consume.
    return false;
  }
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(result);
  }
  // A database error belongs to this attempted sidebar undo as well; do not
  // unexpectedly undo page content after surfacing that failure.
  return true;
}

bool BrowserSidebarHostView::ActivateWorkspaceAtIndex(size_t index) {
  if (index >= workspace_service_->ordered_workspaces().size()) {
    return false;
  }
  return session_bridge_->SetActiveWorkspaceForWindow(
      browser_, workspace_service_->ordered_workspaces()[index].id,
      WorkspaceActivationSource::kKeyboard);
}

bool BrowserSidebarHostView::RevealFolder(const base::Uuid& folder_id) {
  tab_tree::TreeNode folder;
  if (!folder_id.is_valid() ||
      session_bridge_->tab_tree_store()->GetNode(folder_id, &folder) !=
          tab_tree::TabTreeStore::Result::kOk ||
      folder.tombstone || folder.type != tab_tree::TreeNodeType::kFolder) {
    return false;
  }

  std::vector<base::Uuid> path;
  path.push_back(folder.id);
  std::optional<base::Uuid> parent_id = folder.parent_id;
  while (parent_id.has_value()) {
    tab_tree::TreeNode parent;
    if (path.size() > 1024u ||
        session_bridge_->tab_tree_store()->GetNode(*parent_id, &parent) !=
            tab_tree::TabTreeStore::Result::kOk ||
        parent.tombstone || parent.workspace_id != folder.workspace_id ||
        parent.type != tab_tree::TreeNodeType::kFolder) {
      return false;
    }
    path.push_back(parent.id);
    parent_id = parent.parent_id;
  }
  std::reverse(path.begin(), path.end());

  if (session_bridge_->GetActiveWorkspaceForWindow(browser_) !=
          folder.workspace_id &&
      !session_bridge_->SetActiveWorkspaceForWindow(
          browser_, folder.workspace_id,
          WorkspaceActivationSource::kKeyboard)) {
    return false;
  }
  // Workspace observers run synchronously, but keep this entry point robust
  // for a future asynchronous WorkspaceService implementation.
  if (controller_->view_model().workspace_id() != folder.workspace_id) {
    ActivateWorkspace(folder.workspace_id);
  }
  for (const base::Uuid& node_id : path) {
    if (controller_->ExpandNode(node_id) !=
        tab_tree::TabTreeStore::Result::kOk) {
      return false;
    }
  }
  if (!controller_->SelectNode(folder.id)) {
    return false;
  }
  tree_view_->RequestFocus();
  return true;
}

BrowserSidebarHostView::~BrowserSidebarHostView() {
  CancelWorkspaceTransition();
  SetBrowserSidebarDragRoutingActive(this, false);
  weak_ptr_factory_.InvalidateWeakPtrs();
  widget_drag_observation_.Reset();
  group_recent_show_timer_.Stop();
  group_recent_hide_timer_.Stop();
  group_recent_history_task_tracker_.TryCancelAll();
  group_recent_links_view_ = nullptr;
  group_recent_widget_.reset();
  group_recent_delegate_.reset();
  session_presentation_subscription_ = {};
  if (profile_sync_service_) {
    if (window_id_.has_value()) {
      profile_sync_service_->RemoveWindowTabs(window_id_->AsLowercaseString());
    }
    profile_sync_service_->RemoveObserver(this);
    profile_sync_service_->DetachUiBridge(session_bridge_);
    profile_sync_service_ = nullptr;
  }
  if (workspace_button_) {
    workspace_button_->set_context_menu_controller(nullptr);
  }
  workspace_name_field_ = nullptr;
  workspace_icon_field_ = nullptr;
  if (workspace_dialog_widget_) {
    modal_overlay_controller_->DismissPanelImmediately(
        workspace_dialog_widget_.get());
  }
  workspace_dialog_widget_.reset();
  workspace_dialog_delegate_.reset();
  group_name_field_ = nullptr;
  if (group_dialog_widget_) {
    modal_overlay_controller_->DismissPanelImmediately(
        group_dialog_widget_.get());
  }
  group_dialog_widget_.reset();
  group_dialog_delegate_.reset();
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  if (workspace_service_) {
    workspace_service_->RemoveObserver(this);
  }
  // SidebarTreeView observes the controller's model. Destroy child Views
  // before the controller and delegate members disappear.
  RemoveAllChildViews();
}

void BrowserSidebarHostView::OnSessionPresentationChanged() {
  SynchronizeSelection();
  ScheduleRuntimePresentationRefresh();
}

void BrowserSidebarHostView::ActivateInitialWorkspace() {
  std::optional<base::Uuid> workspace_id =
      session_bridge_->GetActiveWorkspaceForWindow(browser_);
  if (!workspace_id.has_value() &&
      !workspace_service_->ordered_workspaces().empty()) {
    workspace_id = workspace_service_->ordered_workspaces().front().id;
    (void)session_bridge_->SetActiveWorkspaceForWindow(
        browser_, *workspace_id, WorkspaceActivationSource::kRestore);
  }
  if (workspace_id.has_value()) {
    ActivateWorkspace(*workspace_id);
  }
}

void BrowserSidebarHostView::ActivateWorkspace(const base::Uuid& workspace_id) {
  if (controller_->ActivateWorkspace(workspace_id) !=
      tab_tree::TabTreeStore::Result::kOk) {
    return;
  }
  for (const tab_tree::Workspace& workspace :
       workspace_service_->ordered_workspaces()) {
    if (workspace.id == workspace_id) {
      SetWorkspaceSelectorPresentation(workspace_button_, workspace.name,
                                       workspace.icon, workspace.accent_argb);
      break;
    }
  }
  UpdateWorkspaceSelectorIndicators();
  ActivateWorkspaceRuntimeTab(workspace_id);
  SynchronizeSelection();
  ScheduleRuntimePresentationRefresh();
}

void BrowserSidebarHostView::UpdateWorkspaceSelectorIndicators() {
  if (!workspace_button_ || !workspace_service_) {
    return;
  }
  const std::optional<base::Uuid> active_workspace =
      window_id_.has_value()
          ? workspace_service_->GetActiveWorkspace(*window_id_)
          : std::nullopt;
  std::vector<WorkspaceSelectorIndicator> indicators;
  indicators.reserve(workspace_service_->ordered_workspaces().size());
  const auto& workspaces = workspace_service_->ordered_workspaces();
  for (size_t index = 0; index < workspaces.size(); ++index) {
    const tab_tree::Workspace& workspace = workspaces[index];
    // The active workspace is already represented by icon and name. Only
    // inactive workspaces become dots, matching the compact Arc-like model.
    if (active_workspace.has_value() && *active_workspace == workspace.id) {
      continue;
    }
    indicators.push_back({.workspace_index = index,
                          .name = workspace.name,
                          .icon = workspace.icon,
                          .accent_argb = workspace.accent_argb});
  }
  SetWorkspaceSelectorIndicators(
      workspace_button_, std::move(indicators),
      base::BindRepeating(
          [](BrowserSidebarHostView* host, size_t index) {
            if (host) {
              (void)host->ActivateWorkspaceAtIndex(index);
            }
          },
          base::Unretained(this)));
}

void BrowserSidebarHostView::RememberActiveTabForWorkspace(
    const std::optional<base::Uuid>& workspace_id) {
  if (!workspace_id.has_value() || !tab_strip_model_) {
    return;
  }
  tabs::TabInterface* active_tab = tab_strip_model_->GetActiveTab();
  if (active_tab &&
      session_bridge_->GetWorkspaceForTab(active_tab) == workspace_id) {
    last_active_tab_handles_.insert_or_assign(
        *workspace_id, active_tab->GetHandle().raw_value());
  }
}

tabs::TabInterface* BrowserSidebarHostView::FindRuntimeTab(
    int runtime_tab_handle) const {
  if (!tab_strip_model_ || runtime_tab_handle < 0) {
    return nullptr;
  }
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (tab && tab->GetHandle().raw_value() == runtime_tab_handle) {
      return tab;
    }
  }
  return nullptr;
}

void BrowserSidebarHostView::ActivateWorkspaceRuntimeTab(
    const base::Uuid& workspace_id) {
  if (!tab_strip_model_) {
    return;
  }
  tabs::TabInterface* active_tab = tab_strip_model_->GetActiveTab();
  const std::optional<base::Uuid> active_tab_workspace =
      session_bridge_->GetWorkspaceForTab(active_tab);
  if (active_tab && (!active_tab_workspace.has_value() ||
                     active_tab_workspace == workspace_id)) {
    if (browser_->GetWindow()) {
      browser_->GetBrowserView().SetAhoiEmptyStateVisible(false);
    }
    return;
  }

  tabs::TabInterface* target = nullptr;
  auto remembered = last_active_tab_handles_.find(workspace_id);
  if (remembered != last_active_tab_handles_.end()) {
    target = FindRuntimeTab(remembered->second);
    if (!target || session_bridge_->GetWorkspaceForTab(target) !=
                       std::make_optional(workspace_id)) {
      last_active_tab_handles_.erase(remembered);
      target = nullptr;
    }
  }
  if (!target) {
    target =
        session_bridge_->GetLastActiveTabForWorkspace(browser_, workspace_id);
    if (target && session_bridge_->GetWorkspaceForTab(target) ==
                      std::make_optional(workspace_id)) {
      last_active_tab_handles_.insert_or_assign(
          workspace_id, target->GetHandle().raw_value());
    } else {
      target = nullptr;
    }
  }
  if (!target) {
    for (tabs::TabInterface* candidate : *tab_strip_model_) {
      if (candidate && session_bridge_->GetWorkspaceForTab(candidate) ==
                           std::make_optional(workspace_id)) {
        target = candidate;
        break;
      }
    }
  }
  if (target) {
    const int target_index = tab_strip_model_->GetIndexOfTab(target);
    if (target_index >= 0) {
      tab_strip_model_->ActivateTabAt(
          target_index,
          TabStripUserGestureDetails(
              TabStripUserGestureDetails::GestureType::kKeyboard));
      if (browser_->GetWindow()) {
        browser_->GetBrowserView().SetAhoiEmptyStateVisible(false);
      }
    }
    return;
  }

  // Empty workspaces are a first-class Ahoi state. Do not synthesize a New
  // Tab page when switching into one; the native empty surface remains visible
  // and the user can create a tab explicitly through Cmd+T or the sidebar.
  if (browser_->GetWindow()) {
    browser_->GetBrowserView().SetAhoiEmptyStateVisible(true);
  }
}

void BrowserSidebarHostView::EnsureWorkspaceSurface() {
  if (!controller_ || !browser_ || !browser_->GetWindow() ||
      browser_->IsWindowCloseRequested()) {
    return;
  }
  const std::optional<base::Uuid> active_workspace =
      controller_->view_model().workspace_id();
  if (active_workspace.has_value()) {
    ActivateWorkspaceRuntimeTab(*active_workspace);
  } else {
    browser_->GetBrowserView().SetAhoiEmptyStateVisible(false);
  }
}

void BrowserSidebarHostView::SynchronizeSelection() {
  if (!tab_strip_model_ || !session_bridge_) {
    return;
  }
  tabs::TabInterface* active_tab = tab_strip_model_->GetActiveTab();
  (void)controller_->SelectNode(
      session_bridge_->FindTreeNodeIdForTab(active_tab));
}

// WorkspaceServiceObserver:
void BrowserSidebarHostView::OnWorkspaceListChanged() {
  UpdateWorkspaceSelectorIndicators();
  ActivateInitialWorkspace();
}

void BrowserSidebarHostView::OnActiveWorkspaceChanged(
    const base::Uuid& window_id,
    const std::optional<base::Uuid>& old_workspace_id,
    const std::optional<base::Uuid>& new_workspace_id,
    WorkspaceActivationSource) {
  if (!window_id_.has_value()) {
    window_id_ = session_bridge_->GetWindowId(browser_);
  }
  if (window_id_ == window_id && new_workspace_id.has_value()) {
    UpdateWorkspaceSelectorIndicators();
    RememberActiveTabForWorkspace(old_workspace_id);
    ActivateWorkspace(*new_workspace_id);
  }
}

// TabStripModelObserver:
void BrowserSidebarHostView::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  SynchronizeSelection();
  RefreshPageTint();
  EnsureWorkspaceSurface();
  ScheduleRuntimePresentationRefresh();
}

void BrowserSidebarHostView::OnTabChangedAt(tabs::TabInterface* tab,
                                            int,
                                            TabChangeType change_type) {
  if (tab && change_type == TabChangeType::kAll) {
    const auto it = tab_thumbnail_cache_.find(tab->GetHandle().raw_value());
    if (it != tab_thumbnail_cache_.end()) {
      it->second->Refresh(tab);
    }
  }
  if (tab_strip_model_ && tab && tab == tab_strip_model_->GetActiveTab() &&
      change_type == TabChangeType::kAll) {
    RefreshPageTint();
  }
  ScheduleRuntimePresentationRefresh();
}

void BrowserSidebarHostView::OnSplitTabChanged(const SplitTabChange& change) {
  if (change.model == tab_strip_model_ && tree_view_) {
    tree_view_->OnSplitGroupsChanged();
  }
  SynchronizeSelection();
  ScheduleRuntimePresentationRefresh();
}

void BrowserSidebarHostView::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  if (tab_strip_model_ == tab_strip_model) {
    for (const int handle : mini_player_tab_handles_) {
      mini_player_adapter_->UnregisterWebContents(base::NumberToString(handle));
    }
    mini_player_tab_handles_.clear();
    tab_strip_model_ = nullptr;
  }
}

BEGIN_METADATA(BrowserSidebarHostView)
END_METADATA

}  // namespace ahoi::sidebar
