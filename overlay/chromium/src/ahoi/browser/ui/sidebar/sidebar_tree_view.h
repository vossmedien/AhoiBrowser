// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-forward.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/animation/bounds_animator.h"
#include "ui/views/animation/bounds_animator_observer.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"

namespace ui {
class LayerTreeOwner;
class OSExchangeData;
}  // namespace ui

namespace views {
class ScrollView;
}  // namespace views

namespace ahoi::sidebar {

class SidebarSplitResizeArea;

// Native virtualized viewport over SidebarTreeViewModel. Ordinary rows keep a
// fixed semantic height; multi-row split collections reserve enough height for
// every pane. Only visible rows plus a small overscan are Views children; the
// persistent model remains the single source of truth.
class SidebarTreeView final : public views::View,
                              public SidebarTreeViewModelObserver,
                              public views::ContextMenuController,
                              public views::DragController,
                              public views::BoundsAnimatorObserver,
                              public gfx::AnimationDelegate {
  METADATA_HEADER(SidebarTreeView, views::View)

 public:
  struct VisibleRange {
    size_t first = 0;
    size_t past_last = 0;

    bool operator==(const VisibleRange&) const = default;
  };

  struct DropIndicator {
    enum class Action {
      kMoveOrCopy = 0,
      kSplit = 1,
      kReorderSplitPane = 2,
      kExtractSplitPane = 3,
    };

    base::Uuid source_node_id;
    std::optional<int> source_runtime_tab_handle;
    std::optional<base::Uuid> target_node_id;
    // Geometry belongs to the validated visual projection used for this
    // pointer event. Caching it keeps marker stabilization paint-only instead
    // of rebuilding the complete virtualized split projection in the drag
    // hotpath.
    std::optional<gfx::Rect> target_bounds;
    // A valid split target can still reject this particular source pair. Keep
    // the pointer's deterministic before/after fallback in the geometric
    // probe so crossing the row midpoint invalidates the cached validation.
    std::optional<SidebarTreeController::DropPosition> fallback_position;
    SidebarTreeController::DropPosition position =
        SidebarTreeController::DropPosition::kInside;
    SidebarTreeController::DropOperation operation =
        SidebarTreeController::DropOperation::kMove;
    Action action = Action::kMoveOrCopy;

    bool operator==(const DropIndicator&) const = default;
  };

  static constexpr size_t kOverscanRows = 2;
  static constexpr int kPreferredWidth = visual_style::kSidebarContentWidth;

  SidebarTreeView(SidebarTreeController* controller,
                  SidebarTreeViewDelegate* delegate,
                  std::u16string accessible_name,
                  std::u16string split_with_prefix);
  SidebarTreeView(const SidebarTreeView&) = delete;
  SidebarTreeView& operator=(const SidebarTreeView&) = delete;
  ~SidebarTreeView() override;

  static std::unique_ptr<views::ScrollView> CreateScrollView(
      std::unique_ptr<SidebarTreeView> tree_view);

  static VisibleRange CalculateVisibleRange(size_t row_count,
                                            const gfx::Rect& visible_bounds,
                                            size_t overscan_rows);
  void BeginRenameSelectedNode();
  void CancelRename();
  // The host owns native drag lifetime while this view owns geometric
  // acceptance. Keeping both states explicit lets the saved section remain a
  // visible drop surface between target enter/exit events, including when the
  // workspace has no saved nodes yet.
  void SetDragTargetVisible(bool visible);
  // Clears the concrete row/slot target without ending the host-owned drag.
  // This named entry point is also used when AppKit routes directly from a
  // descendant to a sibling or the WebContents overlay.
  void ClearDropTargetPresentation();
  // Live mixed saved/temporary splits are rendered by the host as one
  // composite row. Hide only their saved-page proxies here; the persistent
  // model and its selection/order remain untouched.
  void SetRuntimeCompositeSuppressedNodes(std::set<base::Uuid> node_ids);

  // Called by the frame host after a sidebar reveal has reached its final
  // compositor state and the corresponding layout pass has completed. The
  // actual row reconciliation stays posted through the existing weak-pointer
  // path so it cannot mutate Views' visible-bounds observer hierarchy while
  // that hierarchy is being traversed.
  void OnPresentationAnimationSettled();

  size_t materialized_row_count_for_testing() const {
    return materialized_rows_.size();
  }
  size_t recycled_row_count_for_testing() const {
    return recycled_rows_.size();
  }
  SidebarTreeRowView* GetMaterializedRowForTesting(
      const base::Uuid& node_id) const;
  const std::optional<DropIndicator>& drop_indicator_for_testing() const {
    return drop_indicator_;
  }
  bool drag_target_visible_for_testing() const { return drag_target_visible_; }
  bool drag_target_accepting_for_testing() const {
    return drag_target_accepting_;
  }
  bool insertion_marker_visible_for_testing() const {
    return insertion_marker_ && insertion_marker_->GetVisible();
  }
  gfx::Rect insertion_marker_bounds_for_testing() const {
    return insertion_marker_ ? insertion_marker_->bounds() : gfx::Rect();
  }
  void SynchronizeRowsForTesting(const gfx::Rect& visible_bounds);
  std::optional<DropIndicator> CalculateDropIndicatorForTesting(
      const base::Uuid& source_node_id,
      const gfx::Point& point,
      SidebarTreeController::DropOperation operation);
  void SetDropIndicatorForTesting(std::optional<DropIndicator> indicator) {
    SetDropIndicator(indicator);
  }
  bool row_bounds_animation_running_for_testing() const {
    return row_bounds_animator_.IsAnimating();
  }
  void CompleteRowBoundsAnimationForTesting() {
    row_bounds_animator_.Complete();
  }
  gfx::SlideAnimation* height_animation_for_testing() {
    return &preferred_height_animation_;
  }
  gfx::AnimationContainer* row_bounds_animation_container_for_testing() {
    return row_bounds_animator_.container();
  }
  const std::optional<base::Uuid>& editing_node_id_for_testing() const {
    return editing_node_id_;
  }

  // Row callbacks. These are public only so the separately compiled recycled
  // row can forward native Views events; callers should use the tree surface.
  void OnRowPressed(SidebarTreeRowView* row,
                    const ui::MouseEvent& event,
                    bool disclosure_hit);
  void OnRowReleased(SidebarTreeRowView* row,
                     const ui::MouseEvent& event,
                     bool disclosure_hit);
  void OnRowTrailingAction(SidebarTreeRowView* row);
  void OnRowHoverChanged(SidebarTreeRowView* row, bool hovered);
  std::vector<gfx::ImageSkia> GetSavedPageDragThumbnailsForNode(
      const base::Uuid& node_id) const;
  bool OnRowAccessibilityFocused(SidebarTreeRowView* row);
  bool OnRowAccessibilityActivated(SidebarTreeRowView* row);
  void CommitRename(const base::Uuid& node_id, std::u16string title);
  void CancelRename(const base::Uuid& node_id);
  void OnRowDragDone();
  void OnSplitGroupsChanged();
  void OnRuntimePresentationChanged();
  bool IsSearchProjectionActiveForRow() const {
    return model().is_search_projection_active();
  }
  bool IsExactSearchMatchForRow(const base::Uuid& node_id) const {
    return model().IsSearchExactMatch(node_id);
  }
  bool IsRuntimeCompositeSuppressedNode(const base::Uuid& node_id) const {
    return runtime_composite_suppressed_nodes_.contains(node_id);
  }

  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override;
  void OnVisibleBoundsChanged() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  gfx::Point GetKeyboardContextMenuLocation() override;
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

  // SidebarTreeViewModelObserver:
  void OnBatchUpdateStarted() override;
  void OnBatchUpdateEnded() override;
  void OnTreeReset() override;
  void OnFolderExpansionChanging(const base::Uuid& node_id,
                                 bool expanded) override;
  void OnRowsInserted(size_t first_row, size_t count) override;
  void OnRowsRemoved(size_t first_row, size_t count) override;
  void OnRowsChanged(size_t first_row, size_t count) override;
  void OnSelectionChanged(
      const std::optional<base::Uuid>& old_selection,
      const std::optional<base::Uuid>& new_selection) override;

  // views::DragController:
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& point) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& point) override;
  void OnWillStartDragForView(views::View* dragged_view) override;
  void OnNativeDragStartedForView(views::View* dragged_view) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

  // views::BoundsAnimatorObserver:
  void OnBoundsAnimatorProgressed(views::BoundsAnimator* animator) override;
  void OnBoundsAnimatorDone(views::BoundsAnimator* animator) override;

 private:
  struct FolderReveal {
    base::Uuid folder_id;
    bool expanded;
    int origin_y;
    bool splice_ready = false;
  };
  struct DeferredSelectionReveal {
    base::Uuid node_id;
    gfx::Point visible_origin;
  };

  struct VisualRow {
    std::vector<size_t> model_indices;
    size_t anchor_depth = 0;
    std::optional<split_tabs::SplitTabVisualData> split_visual_data;
    int y = 0;
    int height = SidebarTreeRowView::kRowHeight;
  };

  struct VisualPosition {
    size_t visual_row = 0;
    size_t segment = 0;
    size_t segment_count = 1;
    bool present = false;
  };

  struct VisualHit {
    size_t visual_row = 0;
    size_t model_index = 0;
    gfx::Rect bounds;
  };

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& screen_point,
      ui::mojom::MenuSourceType source_type) override;

  SidebarTreeViewModel& model() { return controller_->view_model(); }
  const SidebarTreeViewModel& model() const {
    return controller_->view_model();
  }
  void ScheduleSynchronization(bool preferred_size_changed);
  void ScheduleVisibleBoundsSynchronization();
  void SynchronizeRowsAfterVisibleBoundsChange();
  std::vector<VisualRow> BuildVisualRows() const;
  std::vector<VisualPosition> BuildVisualPositions(
      const std::vector<VisualRow>& visual_rows) const;
  static VisibleRange CalculateVisibleRange(
      const std::vector<VisualRow>& visual_rows,
      const gfx::Rect& visible_bounds,
      size_t overscan_rows);
  static int GetVisualRowsHeight(const std::vector<VisualRow>& visual_rows);
  static std::optional<size_t> FindVisualRowAtY(
      const std::vector<VisualRow>& visual_rows,
      int y);
  gfx::Rect GetSegmentBounds(const VisualRow& visual_row,
                             size_t segment_index,
                             int row_width) const;
  void SynchronizeRows(const gfx::Rect& visible_bounds);
  void SynchronizeSplitResizeAreas(const std::vector<VisualRow>& visual_rows,
                                   const VisibleRange& visible_range,
                                   int row_width,
                                   bool native_drag_in_progress);
  bool ResizeSavedSplit(const std::vector<base::Uuid>& node_ids,
                        size_t divider_index,
                        double ratio,
                        bool done_resizing);
  SidebarTreeRowView* AcquireRow();
  void RecycleRow(const base::Uuid& node_id);
  void UpdateActiveDescendant();
  void EnsureRowVisible(size_t row_index);
  void SelectRow(size_t row_index);
  void SelectRelativeRow(int delta);
  void CollapseOrSelectParent();
  void ExpandOrSelectChild();
  void ActivateSelectedNode();
  std::optional<base::Uuid> NodeAtPoint(const gfx::Point& point) const;
  std::optional<VisualHit> FindVisualHit(
      const std::vector<VisualRow>& visual_rows,
      const gfx::Point& point) const;
  std::optional<DropIndicator> CalculateDropIndicator(DropIndicator probe);
  std::optional<DropIndicator> CalculateTemporaryTabDropIndicator(
      DropIndicator probe);
  std::optional<DropIndicator> BuildDropProbe(
      const base::Uuid& source_node_id,
      const gfx::Point& point,
      SidebarTreeController::DropOperation operation,
      const std::vector<VisualRow>& visual_rows) const;
  std::optional<DropIndicator> BuildTemporaryTabDropProbe(
      int runtime_tab_handle,
      const gfx::Point& point,
      const std::vector<VisualRow>& visual_rows) const;
  std::optional<int> InsertionSlotY(const DropIndicator& indicator) const;
  std::optional<DropIndicator> StabilizeInsertionSlot(
      std::optional<DropIndicator> indicator) const;
  std::optional<DropIndicator> StabilizeDropZone(
      std::optional<DropIndicator> indicator,
      const gfx::Point& point) const;
  void UpdateInsertionMarker();
  void SetDropIndicator(std::optional<DropIndicator> indicator);
  void UpdateFolderAutoExpand(const std::optional<DropIndicator>& indicator);
  void CancelFolderAutoExpand();
  void ExpandPendingFolder();
  void MaybeAutoScroll(const gfx::Point& point);
  void NotifyNativeDragStarted(base::Uuid node_id);
  void PerformDrop(DropIndicator indicator,
                   const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_owner);
  void SynchronizeSearchContextGroups();
  void HandleVisualLayoutChanged();
  void StartPreferredHeightAnimation(int from_height, int to_height);
  int GetAnimatedHeight() const;
  void UpdateAnimatedSplitClips();
  void PrepareFolderExit(size_t first_row, size_t count);
  std::optional<int> FolderEntryOrigin(size_t row_index) const;
  void ScheduleExitedRowCleanup();
  void CleanupExitedRows();
  void MaybeScheduleSelectionReveal();
  void FinishSelectionReveal();
  void CancelSelectionReveal();
  const raw_ptr<SidebarTreeController> controller_;
  const raw_ptr<SidebarTreeViewDelegate> delegate_;
  const std::u16string split_with_prefix_;
  std::unordered_map<base::Uuid, raw_ptr<SidebarTreeRowView>, base::UuidHash>
      materialized_rows_;
  std::map<std::string, raw_ptr<SidebarSplitResizeArea>> split_resize_areas_;
  // Paint-only overlay. The full row edge zone communicates target area;
  // this fixed semantic slot edge disambiguates before from after without
  // participating in layout or following raw pointer coordinates.
  raw_ptr<views::View> insertion_marker_ = nullptr;
  std::vector<std::unique_ptr<SidebarTreeRowView>> recycled_rows_;
  // Selection can synchronously re-materialize a virtualized row between its
  // mouse-down and mouse-up. Keep the pressed identity in the stable owner so
  // a normal click is never degraded into a selection-only first click.
  std::optional<base::Uuid> pressed_node_id_;
  std::optional<base::Uuid> editing_node_id_;
  std::optional<DropIndicator> drop_indicator_;
  bool drag_target_visible_ = false;
  bool drag_target_accepting_ = false;
  std::set<base::Uuid> runtime_composite_suppressed_nodes_;
  // Avoid repeating database-backed cycle/order validation while the pointer
  // remains within the same geometric drop zone. Rejected probes are cached
  // too; every model delta invalidates this cache.
  std::optional<DropIndicator> last_drop_probe_;
  std::optional<base::Uuid> pending_folder_expand_id_;
  base::OneShotTimer folder_expand_timer_;
  views::BoundsAnimator row_bounds_animator_{this};
  // Only materialized split groups; a frame update must not traverse the full
  // persistent tree or retain raw row pointers through virtualization.
  std::vector<std::vector<base::Uuid>> materialized_split_clip_groups_;
  std::vector<std::vector<base::Uuid>> exiting_split_clip_groups_;
  std::optional<FolderReveal> pending_folder_reveal_;
  std::set<base::Uuid> exiting_rows_;
  bool exited_row_cleanup_pending_ = false;
  bool row_bounds_animation_pending_ = false;
  bool in_batch_update_ = false;
  bool synchronization_pending_ = false;
  bool visible_bounds_synchronization_pending_ = false;
  bool preferred_size_change_pending_ = false;
  gfx::SlideAnimation preferred_height_animation_{this};
  int last_visual_height_ = 0;
  std::optional<int> pending_animation_from_height_;
  int animated_height_from_ = 0;
  int animated_height_to_ = 0;
  bool preferred_height_animation_active_ = false;
  std::optional<DeferredSelectionReveal> deferred_selection_reveal_;
  std::vector<base::CallbackListSubscription>
      selection_reveal_scroll_subscriptions_;
  bool selection_reveal_task_pending_ = false;
  base::WeakPtrFactory<SidebarTreeView> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_H_
