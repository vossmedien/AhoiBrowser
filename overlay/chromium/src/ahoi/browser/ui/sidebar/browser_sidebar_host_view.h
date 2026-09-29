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
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/media/ahoi_media_state.h"
#include "ahoi/browser/media/media_mini_player_chromium_adapter.h"
#include "ahoi/browser/media/media_mini_player_service.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/group_page_close.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/workspace_directory_order.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/appearance/sidebar_tint_transition.h"
#include "ahoi/browser/ui/media/media_mini_player_view.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_state.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_types.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"
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
#include "base/uuid.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/split_tabs/split_tab_id.h"
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
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

class Browser;
class BrowserWindowInterface;
class Profile;
class SessionID;
class TabStripModel;

namespace bookmarks {
class BookmarkModel;
}

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
class RadioButton;
class Textfield;
class Widget;
}  // namespace views

namespace ahoi {
class CommandService;
class ModalOverlayController;
struct CommandItem;
}  // namespace ahoi

namespace ahoi::sidebar {

class CachedTabThumbnail;
struct SidebarDiscoveryItem;
class SidebarDiscoveryModel;
class SidebarMediaOverlayView;
class SidebarTreeView;
class BrowserSidebarHostView;

// WS-ISO-05: live hosts that can receive a drag from another Profile.
void TrackBrowserSidebarHostForCrossLevelDrop(BrowserSidebarHostView* host,
                                              bool live);
// Arms those of another Profile than `source`'s drag; nullptr disarms.
void UpdateBrowserSidebarCrossLevelDropTargets(BrowserSidebarHostView* source);

class BrowserSidebarHostView final
    : public views::View,
      public content::WebContentsObserver,
      public SidebarTreeViewDelegate,
      public WorkspaceServiceObserver,
      public sync::ProfileSyncService::Observer,
      public TabStripModelObserver,
      public bookmarks::BaseBookmarkModelObserver,
      public appearance::SidebarTintTransition::Observer,
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

  // Walk the active Workspace's sidebar tab stops; null `index` is the last.
  base::WeakPtr<tabs::TabInterface> ResolveRelativeRuntimeTab(int delta) const;
  base::WeakPtr<tabs::TabInterface> ResolveNumberedRuntimeTab(
      std::optional<size_t> index) const;
  bool ActivateRelativeRuntimeTab(int delta);

  // `index` counts the process-wide order of the shared switcher (ADR 0011
  // step 2), not only this Profile's Workspaces.
  bool ActivateWorkspaceAtIndex(size_t index);
  bool ActivateWorkspaceById(const base::Uuid& workspace_id);

  bool RevealFolder(const base::Uuid& folder_id);
  bool MoveSelectionToWorkspace(const base::Uuid& workspace_id, bool dry_run);
  bool MoveSelectionAcrossLevels(const base::Uuid& workspace_id, bool dry_run);

  bool SetSidebarPresentationMode(SidebarPresentationMode mode);
  bool ToggleFloatingSidebar();
  bool ToggleSidebarVisibility();
  bool RestoreSidebar();
  void OnSidebarPresentationSettled();

  BrowserSidebarSplitDropSource ResolveSplitDropSource(
      const drag::SidebarTabDragPayload& payload,
      bool activate_saved_page);
  void BeginSplitPaneDrag(const drag::SidebarTabDragPayload& payload);
  void CancelSplitDropDrag();
  void ClaimDropTargetPresentation(views::View* claimant);
  void ClearDropTargetPresentation();

  ~BrowserSidebarHostView() override;

 private:
  friend bool IsBrowserSidebarDragActive(views::View* sidebar_host);
  friend bool ToggleBrowserSidebarDiscovery(views::View* sidebar_host);

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

  // One model observer per host invalidates row presentation; URL membership
  // is queried through BookmarkModel's index, never by scanning during paint.
  void BookmarkModelChanged() override;
  void BookmarkModelBeingDeleted() override;
  bool IsUrlBookmarked(const GURL& url) const;

  void OnAppearanceChanged(const appearance::GlassPolicy& policy);
  void RefreshPageTint(bool allow_animation = true);

  // appearance::SidebarTintTransition::Observer:
  void OnSidebarTintTransitionUpdated() override;

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
  bool ActivateRelativeSwitcherWorkspace(int delta,
                                         WorkspaceActivationSource source);
  bool ActivateRelativeWorkspaceWithTransition(
      int delta,
      WorkspaceActivationSource source);
  void StartWorkspaceTransition(int delta, bool active_web_contents_changed);
  void CancelWorkspaceTransition();
  void UpdateWorkspaceSelectorIndicators();
  void RememberActiveTabForWorkspace(
      const std::optional<base::Uuid>& workspace_id);
  tabs::TabInterface* FindRuntimeTab(int runtime_tab_handle) const;
  void ActivateWorkspaceRuntimeTab(const base::Uuid& workspace_id);

  // TabStripModel observer callbacks must not activate another tab. After
  // their notification finishes, native tab selection follows that tab's
  // workspace; removal instead preserves the current workspace, including
  // its empty surface when only foreign-workspace tabs remain.
  void ReconcileWorkspaceSurface(uint64_t generation,
                                 bool follow_selected_tab);

  // Keeps the native WebView surface aligned with the active Ahoi workspace
  // after a tab removal. A shared Chromium TabStripModel may still contain
  // tabs from another workspace, so an empty Ahoi workspace must explicitly
  // cover that stale global selection instead of showing it in the page area.
  void EnsureWorkspaceSurface();

  // Handoff 011 S4: SessionRestore creates the window, applies its Workspace
  // and only then inserts the tabs with theirs. Aligning the surface in
  // between activates the wrong tab or leaves the empty state visible, so it
  // waits for the restore-finished notification and runs once afterwards.
  bool DeferWorkspaceSurfaceDuringRestore();
  void OnSessionRestored(Profile* profile, int num_tabs);
  void ReconcileWorkspaceSurfaceAfterRestore();
  void SynchronizeSelection();
  void ScheduleRuntimePresentationRefresh();
  void RunScheduledRuntimePresentationRefresh(uint64_t generation);
  void PrimeRuntimeAuxiliaryPresentation();
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

  // `refresh_auxiliary` controls expensive thumbnail/media/sync enrichment.
  // Typeahead filtering only needs to rebuild the visible projection and must
  // not publish device state or capture thumbnails on every keystroke.
  void RefreshRuntimePresentation(bool refresh_auxiliary = true);
  void PublishLocalDeviceTabs();
  sync::LocalTabCapture BuildSharedTabCapture(uint64_t generation) const;
  void PublishRequestedSharedTabCapture(uint64_t generation);
  ui::ImageModel GetSharedTabOriginIcon(const base::Uuid& node_id) const;
  std::u16string GetSharedTabOriginText(const base::Uuid& node_id) const;
  bool HasProjectedSharedPage(const sync::RemoteTabRecord& tab) const;
  void PublishDeviceTabCommands();
  void RefreshRemoteTabPresentation();
  ui::ImageModel GetFaviconForUrl(const GURL& page_url);
  bool OpenRemoteTab(sync::RemoteTabRecord tab);
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
  bool CanDropOpenTabToTemporary(
      const drag::SidebarTabDragPayload& payload) const;
  bool DropOpenTabToTemporary(const drag::SidebarTabDragPayload& payload);
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
  void OnSidebarDiscoveryPressed(const ui::Event&);
  void ToggleSidebarDiscovery();
  void OpenSidebarDiscovery();
  void CloseSidebarDiscovery();
  void ScheduleCloseSidebarDiscoveryAfterActivation();
  bool HandleSidebarDiscoveryPrimaryResult(
      SidebarDiscoveryView::PrimaryResultAction action);
  void ClearSidebarDiscoveryPrimarySelection(
      bool restore_tree_selection = true);
  void RebuildSidebarDiscoveryPrimaryResults();
  std::set<std::string> ApplySidebarDiscoveryFilter(
      const std::u16string& query,
      const std::vector<SidebarDiscoveryItem>& items);
  bool ActivateSidebarDiscoveryCommand(const CommandItem& item);
  bool RestoreSidebarDiscoveryEntry(SessionID entry_id);
  void RunBrowserCommand(int command_id, const ui::Event&);
  void ExecuteBrowserCommand(int command_id);

  // SidebarTreeViewDelegate:
  void ActivateSavedPage(const tab_tree::TreeNode& node) override;

  void ActivateFolderSearchResult(const tab_tree::TreeNode& node) override;

  BrowserSidebarSplitDropSource MaterializeSavedPage(
      const tab_tree::TreeNode& node,
      bool require_local_model,
      bool use_saved_home = false);

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

  bool ResizeSavedPageSplit(const std::vector<base::Uuid>& node_ids,
                            size_t divider_index,
                            double ratio,
                            bool done_resizing) override;

  bool ResizeSidebarSplit(split_tabs::SplitTabId split_id,
                          size_t divider_index,
                          double ratio,
                          bool done_resizing);

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

  bool IsSavedPageBookmarked(const tab_tree::TreeNode& node) const override;

  std::vector<gfx::ImageSkia> GetSavedPageDragThumbnails(
      const base::Uuid& node_id) const override;

  ui::ImageModel GetSavedPageIcon(const tab_tree::TreeNode& node) override;

  ui::ImageModel GetSavedPageMediaIndicator(
      const tab_tree::TreeNode& node) const override;

  std::u16string GetSavedPageStatusText(
      const tab_tree::TreeNode& node) const override;

  void PerformSavedPageTrailingAction(const base::Uuid& node_id) override;
  bool CloseTemporaryPageForDeletion(const base::Uuid& node_id) override;

  void OnSidebarDragStateChanged(
      std::optional<base::Uuid> dragged_node_id) override;

  void OnTemporaryTabDragStateChanged(
      std::optional<int> runtime_tab_handle) override;

  void OnSidebarDropTargetClaimed() override;

  void UpdateNewGroupDropTargetVisibility();

  // Clears all host-owned drag presentation. This is intentionally tied to
  // the Widget drag lifecycle as well as the source row: a successful drop can
  // remove/recycle the source View before Views is able to call OnDragDone().
  void ResetDragPresentation();

  // views::ContextMenuController:
  bool CaptureContextPageActionTarget(tabs::TabInterface* tab);

  bool IsContextPageActionTargetCurrent() const;

  void ClearContextPageActionTarget();

  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& screen_point,
      ui::mojom::MenuSourceType source_type) override;

  void ShowOpenTabContextMenu(base::WeakPtr<tabs::TabInterface> tab,
                              const gfx::Point& screen_point,
                              ui::mojom::MenuSourceType source_type);

  using SwitcherWorkspace = sidebar::SwitcherWorkspace;
  std::vector<SwitcherWorkspace> SwitcherWorkspaces() const;
  // Index of this window's active Workspace in `switcher`, if listed.
  std::optional<size_t> ActiveSwitcherIndex(
      const std::vector<SwitcherWorkspace>& switcher) const;
  bool ActivateSwitcherWorkspace(const SwitcherWorkspace& target,
                                 WorkspaceActivationSource source);
  // Presents the main Profile's window in this frame, then selects
  // `workspace_id` there when given.
  // `then` receives the presented window, or nullptr.
  void OpenMainWorkspaceByHandOver(
      std::optional<base::Uuid> workspace_id,
      base::OnceCallback<void(BrowserWindowInterface*)> then = {});
  void OpenIsolatedWorkspaceByHandOver(
      const std::string& profile_dir,
      base::OnceCallback<void(BrowserWindowInterface*)> then = {});

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

  // ADR 0011 WS-ISO-05 (browser_sidebar_host_cross_level_move.cc): moving
  // an item to a Workspace of another Profile. Its pages reopen there by
  // URL; sign-ins and site data stay behind.
  std::vector<SwitcherWorkspace> CrossLevelTargets() const;
  // The open tab's node, with its split partners.
  std::vector<base::Uuid> CrossLevelRootsForTab(tabs::TabInterface* tab);
  // Adds the other Profiles' Workspaces to the "Move to" menu, creating it
  // if needed. Returns whether the menu has any item.
  bool AppendCrossLevelMoveItems(std::vector<base::Uuid> roots,
                                 bool has_menu);
  bool RunCrossLevelMoveCommand(int command_id);
  // Explains a refusal, or asks for confirmation in `presenter` (the window
  // the user looks at) and then moves. `follow`: the window follows.
  void RequestCrossLevelMove(std::vector<base::Uuid> roots,
                             const SwitcherWorkspace& target,
                             bool follow,
                             BrowserSidebarHostView* presenter);
  void StartCrossLevelMove(CrossLevelMoveRequest request);
  void OnCrossLevelPagesAnswered(
      CrossLevelMoveRequest request,
      std::optional<SessionBridge::CrossLevelOpenPages> open);
  void FinishCrossLevelMove(
      CrossLevelMoveRequest request,
      SessionBridge::CrossLevelOpenPages open,
      const base::FilePath& target_path,
      std::optional<session::CrossLevelMovePlacement> placement);
  void ShowCrossLevelFailure(const std::u16string& target_name);
  bool UndoCrossLevelMoveIfLatest();
  // Drag-and-drop from another Profile's window (drag routing).
  friend void SetBrowserSidebarDragRoutingActive(views::View*, bool);
  friend void TrackBrowserSidebarHostForCrossLevelDrop(
      BrowserSidebarHostView*, bool);
  friend void UpdateBrowserSidebarCrossLevelDropTargets(
      BrowserSidebarHostView*);
  void SetCrossLevelDropSource(BrowserSidebarHostView* source);
  void AcceptCrossLevelDrop();
  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;

  bool IsCommandIdEnabled(int command_id) const override;

  // Shows the shared shortcut catalog's key for Workspace and sidebar items.
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;

  void ExecuteCommand(int command_id, int) override;

  const tab_tree::Workspace* FindWorkspace(
      const base::Uuid& workspace_id) const;

  void ShowWorkspaceDialog(PendingWorkspaceAction action,
                           std::optional<base::Uuid> workspace_id);
  static std::u16string StructureText(std::u16string_view german,
                                      std::u16string_view english);
  void BuildArchiveMenus();
  void ShowArchiveRestoreMenu(base::Uuid entry_id);
  void ShowArchiveSearch();
  void HandleArchiveSearchAction(sync::TabArchiveEntryRecord expected,
                                 bool delete_entry);
  void ConfirmArchiveDelete(sync::TabArchiveEntryRecord expected);
  void OnArchiveSearchClosed();
  void ArchiveContextTabs();
  std::vector<base::Uuid> ContextArchiveNodes() const;
  void ShowStructureNotice(std::u16string title, std::u16string body);
  void OnStructureDialogClosed();
  void CompleteArchiveAction(bool success);
  void UseSavedHome(base::Uuid node_id, bool set_current);

  void SelectWorkspaceColor(std::optional<uint32_t> color, const ui::Event&);
  void UpdateWorkspaceColorButtons();
  void AddWorkspaceLevelChoice(views::View* contents);
  // ADR 0012 (handoff 080), browser_sidebar_host_workspace_merge.cc.
  void AddWorkspaceMergeChoice(views::View* contents);
  bool AcceptWorkspaceMerge();
  bool AcceptWorkspaceDialog();
  std::string NextProcessWideWorkspaceSortKey() const;

  bool RequestWorkspaceDialogClose();

  void CloseWorkspaceDialogNow();

  void OnWorkspaceDialogClosed();
  // Blurs a client-owned dialog Widget and detaches its input method's text
  // input client, so destroying it cannot trip NativeWidgetMac's focus check.
  // `remove_views` also destroys the dialog's views; only pass it from a
  // posted task, never from inside the dialog's own button or close handling.
  static void PrepareDialogWidgetForDestruction(views::Widget* widget,
                                                bool remove_views = false);

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
  void OnAhoiSharedTabSyncStateChanged(
      const sync::SharedTabSyncState& state) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;

  void OnTabChangedAt(tabs::TabInterface* tab,
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
  bool reduced_transparency_ = false;
  int surface_corner_radius_ = 0;
  appearance::SidebarTintTransition sidebar_tint_transition_{this};
  PrefChangeRegistrar page_tint_pref_change_registrar_;
  std::unique_ptr<SidebarTreeController> controller_;
  raw_ptr<SidebarTreeView> tree_view_ = nullptr;
  raw_ptr<views::Button> workspace_button_ = nullptr;
  raw_ptr<views::View> floating_sidebar_button_ = nullptr;
  raw_ptr<CommandService> command_service_ = nullptr;
  std::unique_ptr<SidebarDiscoveryModel> discovery_model_;
  raw_ptr<SidebarDiscoveryView> discovery_view_ = nullptr;
  SidebarDiscoveryState discovery_state_;
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
  raw_ptr<bookmarks::BookmarkModel> bookmark_model_ = nullptr;
  base::ScopedObservation<bookmarks::BookmarkModel,
                          bookmarks::BookmarkModelObserver>
      bookmark_observation_{this};
  std::map<GURL, ui::ImageModel> favicon_cache_;
  std::set<GURL> requested_favicon_urls_;
  base::CancelableTaskTracker favicon_task_tracker_;
  SidebarThumbnailState thumbnails_;
  std::unique_ptr<SidebarTabPreviewController> tab_preview_controller_;
  std::map<int, std::unique_ptr<AhoiMediaStateTracker>> media_trackers_;
  std::map<int, base::CallbackListSubscription> media_state_subscriptions_;
  std::unique_ptr<MediaMiniPlayerService> mini_player_service_;
  std::unique_ptr<MediaMiniPlayerChromiumAdapter> mini_player_adapter_;
  raw_ptr<media_ui::MediaMiniPlayerView> mini_player_view_ = nullptr;
  std::set<int> mini_player_tab_handles_;
  SidebarGroupRecentState group_recent_;
  std::optional<base::Uuid> dragged_node_id_;
  std::optional<int> dragged_runtime_tab_handle_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_drag_observation_{this};
  SidebarRuntimeRefreshGate runtime_refresh_gate_;
  uint64_t runtime_refresh_generation_ = 0;
  uint64_t workspace_surface_generation_ = 0;
  base::CallbackListSubscription session_restored_subscription_;
  bool session_restore_notified_ = false;
  bool runtime_auxiliary_prime_scheduled_ = false;
  bool runtime_auxiliary_ready_ = false;
  // Split ratio notifications are synchronous. Suppressing the ordinary
  // runtime rebuild while a sidebar ResizeArea owns mouse capture keeps that
  // source View alive until its final commit.
  bool sidebar_split_resize_update_in_progress_ = false;
  bool sidebar_split_resize_active_ = false;
  raw_ptr<sync::ProfileSyncService> profile_sync_service_ = nullptr;
  bool profile_sync_ui_attached_ = false;
  sync::DeviceTabsSnapshot device_tabs_snapshot_;
  SidebarGroupDialogState group_dialog_;
  SidebarWorkspaceDialogState workspace_dialog_;
  SidebarContextMenuState context_;
  std::unique_ptr<views::Widget> structure_dialog_widget_;
  std::unique_ptr<views::BubbleDialogDelegate> archive_search_delegate_;
  std::unique_ptr<views::Widget> archive_search_widget_;
  std::unique_ptr<ui::SimpleMenuModel> context_move_menu_model_;
  std::vector<std::unique_ptr<ui::SimpleMenuModel>>
      context_move_submenu_models_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  base::CallbackListSubscription session_presentation_subscription_;
  base::CallbackListSubscription shared_tab_capture_subscription_;
  std::optional<sync::LocalTabCapture> observed_shared_tabs_;
  // Running before-unload group question of "close all temporary tabs".
  std::unique_ptr<session::GroupPageClose> close_all_temporary_;
  // WS-ISO-05: a sidebar item dragged from another Profile's window.
  base::WeakPtr<BrowserSidebarHostView> cross_level_drop_source_;
  raw_ptr<views::View> cross_level_drop_overlay_ = nullptr;
  base::WeakPtrFactory<BrowserSidebarHostView> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_VIEW_H_
