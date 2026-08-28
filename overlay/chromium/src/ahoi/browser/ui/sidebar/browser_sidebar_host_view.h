// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_VIEW_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/media/ahoi_media_state.h"
#include "ahoi/browser/media/media_mini_player_chromium_adapter.h"
#include "ahoi/browser/media/media_mini_player_service.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/media/media_mini_player_view.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_refresh_gate.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_preview_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/sidebar/workspace_transition_animator.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

class Browser;
class TabStripModel;

namespace favicon {
class FaviconService;
}
namespace history {
class HistoryService;
}
namespace views {
class BubbleDialogDelegate;
class Button;
class ImageButton;
class LabelButton;
class MenuRunner;
class ScrollView;
class Textfield;
class Widget;
}  // namespace views

namespace ahoi {
class ModalOverlayController;
class SessionBridge;
}  // namespace ahoi

namespace ahoi::sidebar {

class CachedTabThumbnail;
class SidebarMediaOverlayView;
class SidebarTreeView;

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

class BrowserSidebarHostView final : public views::View,
                                     public content::WebContentsObserver,
                                     public SidebarTreeViewDelegate,
                                     public WorkspaceServiceObserver,
                                     public sync::ProfileSyncService::Observer,
                                     public TabStripModelObserver,
                                     public media_ui::MediaMiniPlayerHost,
                                     public views::ContextMenuController,
                                     public views::TextfieldController,
                                     public views::WidgetObserver,
                                     public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(BrowserSidebarHostView, views::View)

 public:
  BrowserSidebarHostView(Browser* browser,
                         SessionBridge* session_bridge,
                         WorkspaceService* workspace_service,
                         ModalOverlayController* modal_overlay_controller);

  BrowserSidebarHostView(const BrowserSidebarHostView&) = delete;
  BrowserSidebarHostView& operator=(const BrowserSidebarHostView&) = delete;

  bool UndoLastMutationIfAvailable();

  bool ActivateRelativeWorkspace(int delta);

  bool ActivateRelativeWorkspaceByGesture(int delta);

  bool ActivateRelativeRuntimeTab(int delta);

  bool ActivateWorkspaceAtIndex(size_t index);

  bool RevealFolder(const base::Uuid& folder_id);

  bool SetSidebarPresentationMode(SidebarPresentationMode mode);
  bool ToggleFloatingSidebar();
  bool ToggleSidebarVisibility();
  bool RestoreSidebar();

  BrowserSidebarSplitDropSource ResolveSplitDropSource(
      const drag::SidebarTabDragPayload& payload,
      bool activate_saved_page);
  void CancelSplitDropDrag();

  ~BrowserSidebarHostView() override;

 private:
  friend bool IsBrowserSidebarDragActive(views::View* sidebar_host);

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

  // views::WidgetObserver:
  void OnWidgetDragDropWillStart(views::Widget* widget) override;
  void OnWidgetDragDropCompleted(views::Widget* widget) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  void OnSessionPresentationChanged();

  void OnAppearanceChanged(const appearance::GlassPolicy& policy);
  void RefreshPageTint();

  // content::WebContentsObserver:
  void DidChangeThemeColor() override;
  void WebContentsDestroyed() override;

  // media_ui::MediaMiniPlayerHost:
  void OnMiniPlayerExpandedChanged(bool expanded) override;

  std::unique_ptr<SidebarMediaOverlayView> CreateMiniPlayerOverlay(
      std::unique_ptr<views::ScrollView> scroll_view,
      views::View* scroll_bottom_inset);

  void ActivateInitialWorkspace();

  void ActivateWorkspace(const base::Uuid& workspace_id);

  bool ActivateRelativeWorkspaceWithTransition(
      int delta,
      WorkspaceActivationSource source);

  void StartWorkspaceTransition(int delta);

  void CancelWorkspaceTransition();

  void UpdateWorkspaceSelectorIndicators();

  void RememberActiveTabForWorkspace(
      const std::optional<base::Uuid>& workspace_id);

  tabs::TabInterface* FindRuntimeTab(int runtime_tab_handle) const;

  void ActivateWorkspaceRuntimeTab(const base::Uuid& workspace_id);

  // Keeps the native WebView surface aligned with the active Ahoi workspace
  // after a tab removal. A shared Chromium TabStripModel may still contain
  // tabs from another workspace, so an empty Ahoi workspace must explicitly
  // cover that stale global selection instead of showing it in the page area.
  void EnsureWorkspaceSurface();

  void SynchronizeSelection();

  void ScheduleRuntimePresentationRefresh();
  bool IsSidebarDragActive() const;
  void MaybeScheduleDeferredRuntimePresentationRefresh();

  void RefreshThumbnailCache();

  void OnTabThumbnailChanged(int runtime_tab_handle);

  void RefreshMediaTrackers();

  void RefreshMiniPlayerSources();

  std::string GetMiniPlayerSourceId(tabs::TabInterface* tab) const;

  ui::ImageModel GetMiniPlayerFavicon(
      const MediaMiniPlayerSourceId& source_id) const;

  void OnTrackedMediaStateChanged(const AhoiMediaState& state);

  std::optional<tabs::TabAlert> GetMediaAlertForTab(
      tabs::TabInterface* tab) const;

  ui::ImageModel GetMediaIndicatorForTab(tabs::TabInterface* tab) const;

  std::u16string GetTabAlertStatusText(tabs::TabInterface* tab) const;

  std::vector<gfx::ImageSkia> GetCachedDragThumbnails(
      const std::vector<tabs::TabInterface*>& tabs) const;

  std::vector<gfx::ImageSkia> GetRuntimeTabPreviewThumbnails(
      base::WeakPtr<tabs::TabInterface> tab) const;

  void OnRuntimeTabHoverChanged(base::WeakPtr<tabs::TabInterface> tab,
                                views::View* anchor,
                                bool hovered);

  std::optional<SidebarTabPreviewData> ResolveTabPreviewData(
      const SidebarTabPreviewTarget& target);

  bool ValidateTabPreviewAnchor(const SidebarTabPreviewTarget& target,
                                const views::View* anchor) const;

  void StoreSavedTabThumbnailSnapshot(const base::Uuid& node_id,
                                      const GURL& url,
                                      const gfx::ImageSkia& image);

  void RefreshRuntimePresentation();

  void PublishLocalDeviceTabs();

  void RefreshRemoteTabPresentation();

  ui::ImageModel GetFaviconForUrl(const GURL& page_url);

  void OpenRemoteTab(sync::RemoteTabRecord tab);

  void ActivateRuntimeTab(base::WeakPtr<tabs::TabInterface> tab);

  void CloseRuntimeTab(base::WeakPtr<tabs::TabInterface> tab);

  void CloseAllTemporaryTabs(const ui::Event&);

  bool CanDropOnRuntimeTab(std::optional<base::Uuid> source_node_id,
                           std::optional<int> source_runtime_handle,
                           base::WeakPtr<tabs::TabInterface> target,
                           OpenTabDropPosition position) const;

  bool DropOnRuntimeTab(std::optional<base::Uuid> source_node_id,
                        std::optional<int> source_runtime_handle,
                        base::WeakPtr<tabs::TabInterface> target,
                        OpenTabDropPosition position);

  tabs::TabInterface* FindTemporaryTab(int runtime_tab_handle) const;

  bool SaveTemporaryTabAtDrop(int runtime_tab_handle,
                              const SidebarTreeController::DropTarget& target,
                              base::Uuid* created_node_id);

  bool SaveTemporaryTabAtWorkspaceRoot(int runtime_tab_handle,
                                       base::Uuid* created_node_id);

  bool MakeSavedPageTemporary(const base::Uuid& source_node_id);

  void OnFaviconAvailable(const GURL& page_url,
                          const favicon_base::FaviconImageResult& result);

  void OnFolderHoverChanged(const base::Uuid& folder_node_id,
                            views::View* anchor,
                            bool hovered) override;

  void OnSavedPageHoverChanged(const base::Uuid& node_id,
                               views::View* anchor,
                               bool hovered) override;

  void BeginGroupRecentQuery(const base::Uuid& folder_node_id);

  void OnGroupHistoryQueryCompleted(
      uint64_t generation,
      const base::Uuid& folder_node_id,
      std::map<GURL, tab_tree::TreeNode> pages_by_url,
      history::QueryResults results);

  void ShowGroupRecentBubble(const base::Uuid& folder_node_id,
                             std::vector<RecentGroupLink> links);

  void ActivateRecentGroupLink(const base::Uuid& node_id);

  void OnGroupRecentBubbleHover(bool hovered);

  void ScheduleGroupRecentBubbleHide();

  void MaybeHideGroupRecentBubble();

  void InvalidateAndCloseGroupRecentBubble();

  void CloseGroupRecentBubble();

  void OnGroupRecentBubbleClosed();

  void OnWorkspacePressed(const ui::Event&);

  // The header buttons can change the visibility of their own ancestor. Defer
  // that mutation until after Button finishes dispatching the current event so
  // layout cannot invalidate the event target while its callback is active.
  void OnSidebarHeaderActionPressed(bool toggle_visibility, const ui::Event&);

  void RunSidebarHeaderAction(bool toggle_visibility);

  void RunBrowserCommand(int command_id, const ui::Event&);

  void ExecuteBrowserCommand(int command_id);

  // SidebarTreeViewDelegate:
  void ActivateSavedPage(const tab_tree::TreeNode& node) override;

  BrowserSidebarSplitDropSource MaterializeSavedPage(
      const tab_tree::TreeNode& node,
      bool require_local_model);

  bool CanSplitSavedPages(const base::Uuid& source_node_id,
                          const base::Uuid& target_node_id) const override;

  bool SplitSavedPages(const base::Uuid& source_node_id,
                       const base::Uuid& target_node_id) override;

  bool CanReorderSavedSplitPanes(
      const base::Uuid& source_node_id,
      const base::Uuid& target_node_id) const override;

  bool ReorderSavedSplitPanes(const base::Uuid& source_node_id,
                              const base::Uuid& target_node_id) override;

  std::vector<std::vector<base::Uuid>> GetSplitSavedPageGroups() const override;

  std::optional<split_tabs::SplitTabVisualData> GetSplitSavedPageVisualData(
      const std::vector<base::Uuid>& node_ids) const override;

  std::vector<base::Uuid> GetMoveGroupNodeIds(
      const base::Uuid& source_node_id) const override;

  bool CanExtractSavedSplitPaneForDrop(
      const base::Uuid& source_node_id,
      const std::optional<base::Uuid>& target_node_id) const override;

  bool ExtractSavedSplitPaneAfterDrop(
      const base::Uuid& source_node_id) override;

  bool CanSaveTemporaryTab(
      int runtime_tab_handle,
      const SidebarTreeController::DropTarget& target) override;

  bool SaveTemporaryTab(
      int runtime_tab_handle,
      const SidebarTreeController::DropTarget& target) override;

  bool CanSaveAndSplitTemporaryTab(
      int runtime_tab_handle,
      const base::Uuid& target_node_id) const override;

  bool SaveAndSplitTemporaryTab(int runtime_tab_handle,
                                const base::Uuid& target_node_id) override;

  bool CanReorderTemporarySplitPane(
      int runtime_tab_handle,
      const base::Uuid& target_node_id) const override;

  bool ReorderTemporarySplitPane(int runtime_tab_handle,
                                 const base::Uuid& target_node_id) override;

  bool IsSavedPageRunning(const base::Uuid& node_id) const override;

  bool IsSavedPageSleeping(const base::Uuid& node_id) const override;

  std::vector<gfx::ImageSkia> GetSavedPageDragThumbnails(
      const base::Uuid& node_id) const override;

  ui::ImageModel GetSavedPageIcon(const tab_tree::TreeNode& node) override;

  ui::ImageModel GetSavedPageMediaIndicator(
      const tab_tree::TreeNode& node) const override;

  std::u16string GetSavedPageStatusText(
      const tab_tree::TreeNode& node) const override;

  void PerformSavedPageTrailingAction(const base::Uuid& node_id) override;

  void OnSidebarDragStateChanged(
      std::optional<base::Uuid> dragged_node_id) override;

  void OnTemporaryTabDragStateChanged(
      std::optional<int> runtime_tab_handle) override;

  void UpdateNewGroupDropTargetVisibility();

  // Clears all host-owned drag presentation. This is intentionally tied to
  // the Widget drag lifecycle as well as the source row: a successful drop can
  // remove/recycle the source View before Views is able to call OnDragDone().
  void ResetDragPresentation();

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& screen_point,
      ui::mojom::MenuSourceType source_type) override;

  void ShowOpenTabContextMenu(base::WeakPtr<tabs::TabInterface> tab,
                              const gfx::Point& screen_point,
                              ui::mojom::MenuSourceType source_type);

  void ShowWorkspaceMenu(const gfx::Point& screen_point,
                         ui::mojom::MenuSourceType source_type);

  void ShowNodeContextMenu(std::optional<base::Uuid> node_id,
                           const gfx::Point& screen_point,
                           ui::mojom::MenuSourceType source_type) override;

  std::optional<int> AddMoveDestinationCommand(
      const base::Uuid& workspace_id,
      std::optional<base::Uuid> folder_id);

  void AppendMoveDestinationFolder(ui::SimpleMenuModel* parent_menu,
                                   const base::Uuid& workspace_id,
                                   const MoveDestinationFolder& folder);

  bool BuildMoveToMenu(const tab_tree::TreeNode* source);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;

  bool IsCommandIdEnabled(int command_id) const override;

  void ExecuteCommand(int command_id, int) override;

  const tab_tree::Workspace* FindWorkspace(
      const base::Uuid& workspace_id) const;

  void ShowWorkspaceDialog(PendingWorkspaceAction action,
                           std::optional<base::Uuid> workspace_id);

  void SelectWorkspaceColor(std::optional<uint32_t> color, const ui::Event&);

  void UpdateWorkspaceColorButtons();

  bool AcceptWorkspaceDialog();

  bool RequestWorkspaceDialogClose();

  void CloseWorkspaceDialogNow();

  void OnWorkspaceDialogClosed();

  void ShowCreateGroupDialog(const base::Uuid& source_node_id);

  void ShowGroupCustomizationDialog(const base::Uuid& folder_node_id);

  void CopyAllLinksInGroup(const base::Uuid& folder_node_id);

  void CopyAllLinksInWorkspace(const base::Uuid& workspace_id);

  void ShowCreateGroupDialogForTemporaryTab(int runtime_tab_handle);

  void ShowCreateRootGroupDialog();

  void ShowCreateSubgroupDialog(const base::Uuid& parent_node_id);

  void ShowGroupDialog(PendingGroupAction action,
                       std::optional<base::Uuid> source_node_id,
                       std::optional<base::Uuid> parent_node_id,
                       std::optional<int> runtime_tab_handle,
                       const std::u16string& default_title);

  void SelectGroupIcon(std::u16string icon, const ui::Event&);

  void SelectGroupColor(std::optional<uint32_t> color, const ui::Event&);

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

  void UpdateGroupChoiceButtons();

  bool AcceptCreateGroupDialog();

  bool RequestGroupDialogClose();

  void CloseGroupDialogNow();

  void OnCreateGroupDialogClosed();

  void CreateGroupAroundNode(const base::Uuid& source_node_id,
                             std::u16string title);

  void CreateFolder(std::optional<base::Uuid> parent_node_id,
                    std::u16string title);

  // WorkspaceServiceObserver:
  void OnWorkspaceListChanged() override;

  void OnActiveWorkspaceChanged(
      const base::Uuid& window_id,
      const std::optional<base::Uuid>& old_workspace_id,
      const std::optional<base::Uuid>& new_workspace_id,
      WorkspaceActivationSource) override;

  // sync::ProfileSyncService::Observer:
  void OnAhoiDeviceTabsChanged(
      const sync::DeviceTabsSnapshot& snapshot) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;

  void OnTabChangedAt(tabs::TabInterface* tab,
                      int,
                      TabChangeType change_type) override;

  void OnSplitTabChanged(const SplitTabChange& change) override;

  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  const raw_ptr<Browser> browser_;
  const raw_ptr<SessionBridge> session_bridge_;
  const raw_ptr<WorkspaceService> workspace_service_;
  const raw_ptr<ModalOverlayController> modal_overlay_controller_;
  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  std::optional<base::Uuid> window_id_;
  std::map<base::Uuid, int> last_active_tab_handles_;
  WorkspaceTransitionAnimator workspace_transition_animator_;
  bool reduced_motion_ = false;
  bool high_contrast_ = false;
  int surface_corner_radius_ = 0;
  std::optional<SkColor> sidebar_page_tint_;
  PrefChangeRegistrar page_tint_pref_change_registrar_;
  std::unique_ptr<SidebarTreeController> controller_;
  raw_ptr<SidebarTreeView> tree_view_ = nullptr;
  raw_ptr<views::Button> workspace_button_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<SidebarMediaOverlayView> media_overlay_view_ = nullptr;
  raw_ptr<views::View> sidebar_actions_ = nullptr;
  raw_ptr<views::View> open_tabs_header_ = nullptr;
  raw_ptr<views::View> open_tabs_container_ = nullptr;
  raw_ptr<views::View> remote_tabs_header_ = nullptr;
  raw_ptr<views::View> remote_tabs_container_ = nullptr;
  raw_ptr<views::View> new_group_drop_target_ = nullptr;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  raw_ptr<favicon::FaviconService> favicon_service_ = nullptr;
  raw_ptr<history::HistoryService> history_service_ = nullptr;
  std::map<GURL, ui::ImageModel> favicon_cache_;
  std::set<GURL> requested_favicon_urls_;
  base::CancelableTaskTracker favicon_task_tracker_;
  std::map<int, std::unique_ptr<CachedTabThumbnail>> tab_thumbnail_cache_;
  struct SavedTabThumbnailSnapshot {
    GURL url;
    gfx::ImageSkia image;
    uint64_t recency = 0;
  };
  std::map<base::Uuid, SavedTabThumbnailSnapshot> saved_thumbnail_snapshots_;
  uint64_t saved_thumbnail_recency_ = 0;
  std::unique_ptr<SidebarTabPreviewController> tab_preview_controller_;
  std::map<int, std::unique_ptr<AhoiMediaStateTracker>> media_trackers_;
  std::map<int, base::CallbackListSubscription> media_state_subscriptions_;
  std::unique_ptr<MediaMiniPlayerService> mini_player_service_;
  std::unique_ptr<MediaMiniPlayerChromiumAdapter> mini_player_adapter_;
  raw_ptr<media_ui::MediaMiniPlayerView> mini_player_view_ = nullptr;
  std::set<int> mini_player_tab_handles_;
  views::ViewTracker group_recent_anchor_tracker_;
  std::optional<base::Uuid> hovered_folder_id_;
  std::optional<base::Uuid> group_recent_bubble_folder_id_;
  base::OneShotTimer group_recent_show_timer_;
  base::OneShotTimer group_recent_hide_timer_;
  base::CancelableTaskTracker group_recent_history_task_tracker_;
  uint64_t group_recent_query_generation_ = 0;
  bool group_recent_bubble_hovered_ = false;
  raw_ptr<views::View> group_recent_links_view_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> group_recent_delegate_;
  std::unique_ptr<views::Widget> group_recent_widget_;
  std::optional<base::Uuid> dragged_node_id_;
  std::optional<int> dragged_runtime_tab_handle_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_drag_observation_{this};
  SidebarRuntimeRefreshGate runtime_refresh_gate_;
  raw_ptr<sync::ProfileSyncService> profile_sync_service_ = nullptr;
  sync::DeviceTabsSnapshot device_tabs_snapshot_;
  PendingGroupAction pending_group_action_ = PendingGroupAction::kNone;
  std::optional<base::Uuid> pending_group_source_id_;
  std::optional<base::Uuid> pending_group_parent_id_;
  std::optional<int> pending_group_runtime_tab_handle_;
  std::u16string pending_group_icon_;
  std::optional<uint32_t> pending_group_accent_argb_;
  raw_ptr<views::Textfield> group_name_field_ = nullptr;
  raw_ptr<views::Textfield> group_icon_field_ = nullptr;
  std::vector<std::pair<raw_ptr<views::ImageButton>, std::u16string>>
      group_icon_buttons_;
  std::vector<std::pair<raw_ptr<views::Button>, std::optional<uint32_t>>>
      group_color_buttons_;
  std::unique_ptr<views::BubbleDialogDelegate> group_dialog_delegate_;
  std::unique_ptr<views::Widget> group_dialog_widget_;
  PendingWorkspaceAction pending_workspace_action_ =
      PendingWorkspaceAction::kNone;
  std::optional<base::Uuid> pending_workspace_id_;
  std::optional<uint32_t> pending_workspace_accent_argb_;
  raw_ptr<views::Textfield> workspace_name_field_ = nullptr;
  raw_ptr<views::Textfield> workspace_icon_field_ = nullptr;
  std::vector<std::pair<raw_ptr<views::Button>, std::optional<uint32_t>>>
      workspace_color_buttons_;
  std::unique_ptr<views::BubbleDialogDelegate> workspace_dialog_delegate_;
  std::unique_ptr<views::Widget> workspace_dialog_widget_;
  std::optional<base::Uuid> context_node_id_;
  base::WeakPtr<tabs::TabInterface> context_runtime_tab_;
  std::vector<base::Uuid> context_workspace_ids_;
  std::vector<ContextMoveDestination> context_move_destinations_;
  ContextMenuScope context_menu_scope_ = ContextMenuScope::kNone;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> context_move_menu_model_;
  std::vector<std::unique_ptr<ui::SimpleMenuModel>>
      context_move_submenu_models_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  base::CallbackListSubscription session_presentation_subscription_;
  base::WeakPtrFactory<BrowserSidebarHostView> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_VIEW_H_
