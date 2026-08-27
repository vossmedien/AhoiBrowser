// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
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

// Native virtualized viewport over SidebarTreeViewModel. Ordinary rows keep a
// fixed semantic height; multi-row split collections reserve enough height for
// every pane. Only visible rows plus a small overscan are Views children; the
// persistent model remains the single source of truth.
class SidebarTreeView final : public views::View,
                              public SidebarTreeViewModelObserver,
                              public views::ContextMenuController,
                              public views::DragController,
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
  // Live mixed saved/temporary splits are rendered by the host as one
  // composite row. Hide only their saved-page proxies here; the persistent
  // model and its selection/order remain untouched.
  void SetRuntimeCompositeSuppressedNodes(std::set<base::Uuid> node_ids);

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
  bool OnRowAccessibilityFocused(SidebarTreeRowView* row);
  bool OnRowAccessibilityActivated(SidebarTreeRowView* row);
  void CommitRename(const base::Uuid& node_id, std::u16string title);
  void CancelRename(const base::Uuid& node_id);
  void OnRowDragDone();
  void OnSplitGroupsChanged();
  void OnRuntimePresentationChanged();

  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override;
  void OnVisibleBoundsChanged() override;
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

 private:
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
  std::optional<DropIndicator> CalculateDropIndicator(
      const base::Uuid& source_node_id,
      const gfx::Point& point,
      SidebarTreeController::DropOperation operation);
  std::optional<DropIndicator> CalculateTemporaryTabDropIndicator(
      int runtime_tab_handle,
      const gfx::Point& point);
  std::optional<DropIndicator> BuildDropProbe(
      const base::Uuid& source_node_id,
      const gfx::Point& point,
      SidebarTreeController::DropOperation operation) const;
  std::optional<DropIndicator> BuildTemporaryTabDropProbe(
      int runtime_tab_handle,
      const gfx::Point& point) const;
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
  void HandleVisualLayoutChanged();
  void StartPreferredHeightAnimation(int from_height, int to_height);
  const raw_ptr<SidebarTreeController> controller_;
  const raw_ptr<SidebarTreeViewDelegate> delegate_;
  const std::u16string split_with_prefix_;
  std::unordered_map<base::Uuid, raw_ptr<SidebarTreeRowView>, base::UuidHash>
      materialized_rows_;
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
  bool row_bounds_animation_pending_ = false;
  std::optional<int> row_bounds_animation_from_height_;
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
  base::WeakPtrFactory<SidebarTreeView> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_H_
