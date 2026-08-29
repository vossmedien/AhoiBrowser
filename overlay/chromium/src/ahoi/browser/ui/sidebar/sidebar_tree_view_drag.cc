// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/drag_utils.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

namespace {

constexpr int kAutoScrollEdge = 24;

ui::mojom::DragOperation ToNativeDragOperation(
    SidebarTreeController::DropOperation operation) {
  return operation == SidebarTreeController::DropOperation::kCopy
             ? ui::mojom::DragOperation::kCopy
             : ui::mojom::DragOperation::kMove;
}

}  // namespace

bool SidebarTreeView::AreDropTypesRequired() {
  return true;
}

bool SidebarTreeView::CanDrop(const ui::OSExchangeData& data) {
  return model().workspace_id().has_value() &&
         drag::ReadSidebarTabDragPayload(data).has_value();
}

void SidebarTreeView::OnDragEntered(const ui::DropTargetEvent& event) {
  OnDragUpdated(event);
}

int SidebarTreeView::OnDragUpdated(const ui::DropTargetEvent& event) {
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(event.data());
  if (!payload.has_value()) {
    last_drop_probe_.reset();
    SetDropIndicator(std::nullopt);
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  const std::optional<base::Uuid>& source = payload->saved_node_id;
  const std::optional<int>& runtime_source = payload->runtime_tab_handle;
  if (!source.has_value() && !runtime_source.has_value()) {
    last_drop_probe_.reset();
    SetDropIndicator(std::nullopt);
    return static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  MaybeAutoScroll(event.location());
  // Sidebar drags are intentionally moves. Native macOS drag negotiation can
  // advertise a copy when the source offers both operations, even though the
  // user performed an ordinary drag. Duplication is an explicit context-menu
  // action; keeping the drag contract move-only prevents accidental duplicate
  // nodes while preserving the separate controller copy API for that action.
  const auto operation = SidebarTreeController::DropOperation::kMove;
  // Split grouping and visual row geometry can be O(N). Compute the immutable
  // projection once for this native pointer event, then carry the resolved
  // target bounds through validation and marker painting.
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::optional<DropIndicator> probe =
      source.has_value()
          ? BuildDropProbe(*source, event.location(), operation, visual_rows)
          : BuildTemporaryTabDropProbe(*runtime_source, event.location(),
                                       visual_rows);
  // A latched indicator can intentionally differ from the raw geometric
  // probe inside the hysteresis band. Do not take the validation cache fast
  // path in that state: the pointer must be allowed to cross the far side of
  // the band even though its coarse 30/40/30 probe has not changed.
  const bool probe_matches_presentation =
      (!probe.has_value() && !drop_indicator_.has_value()) ||
      (probe.has_value() && !drop_indicator_.has_value()) ||
      (probe.has_value() && drop_indicator_.has_value() &&
       probe->position == drop_indicator_->position);
  if (last_drop_probe_ == probe && probe_matches_presentation) {
    return drop_indicator_.has_value()
               ? static_cast<int>(
                     ToNativeDragOperation(drop_indicator_->operation))
               : static_cast<int>(ui::mojom::DragOperation::kNone);
  }
  last_drop_probe_ = probe;
  std::optional<DropIndicator> indicator;
  if (probe.has_value()) {
    indicator = source.has_value() ? CalculateDropIndicator(*probe)
                                   : CalculateTemporaryTabDropIndicator(*probe);
  }
  indicator = StabilizeInsertionSlot(std::move(indicator));
  indicator = StabilizeDropZone(std::move(indicator), event.location());
  if (delegate_) {
    delegate_->OnSidebarDropTargetClaimed();
  }
  SetDropIndicator(indicator);
  return indicator.has_value()
             ? static_cast<int>(ToNativeDragOperation(indicator->operation))
             : static_cast<int>(ui::mojom::DragOperation::kNone);
}

void SidebarTreeView::OnDragExited() {
  ClearDropTargetPresentation();
}

void SidebarTreeView::ClearDropTargetPresentation() {
  CancelFolderAutoExpand();
  last_drop_probe_.reset();
  SetDropIndicator(std::nullopt);
}

views::View::DropCallback SidebarTreeView::GetDropCallback(
    const ui::DropTargetEvent& /*event*/) {
  if (!drop_indicator_.has_value()) {
    return base::NullCallback();
  }
  DropIndicator indicator = *drop_indicator_;
  CancelFolderAutoExpand();
  last_drop_probe_.reset();
  SetDropIndicator(std::nullopt);
  return base::BindOnce(&SidebarTreeView::PerformDrop,
                        weak_ptr_factory_.GetWeakPtr(), std::move(indicator));
}

void SidebarTreeView::OnBatchUpdateStarted() {
  in_batch_update_ = true;
  pending_animation_from_height_ = last_visual_height_;
}

int SidebarTreeView::GetDragOperationsForView(views::View* sender,
                                              const gfx::Point& point) {
  return ui::DragDropTypes::DRAG_MOVE;
}

bool SidebarTreeView::CanStartDragForView(views::View* sender,
                                          const gfx::Point& press_pt,
                                          const gfx::Point& /*point*/) {
  auto* row = views::AsViewClass<SidebarTreeRowView>(sender);
  const bool allowed = row && row->is_bound() && !row->is_editing() &&
                       !row->IsTrailingActionAt(press_pt);
  return allowed;
}

void SidebarTreeView::OnWillStartDragForView(views::View* dragged_view) {
  auto* row = views::AsViewClass<SidebarTreeRowView>(dragged_view);
  if (delegate_ && row && row->is_bound()) {
    // Widget has registered `dragged_view` before invoking this callback, so
    // the source remains owned while the host reveals its drag-only targets.
    // Do not depend exclusively on AppKit's willBegin callback here: some
    // macOS 26 event paths never deliver it, leaving an otherwise valid Views
    // drag without its targets or presentation state.
    delegate_->OnSidebarDragStateChanged(row->node_id());
  }
}

void SidebarTreeView::OnNativeDragStartedForView(views::View* dragged_view) {
  auto* row = views::AsViewClass<SidebarTreeRowView>(dragged_view);
  if (!delegate_ || !row || !row->is_bound()) {
    return;
  }

  // The macOS backend calls this from AppKit's native session lifecycle. Defer
  // the layout-producing delegate callback until that callback has returned
  // to the backend's nested drag loop.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&SidebarTreeView::NotifyNativeDragStarted,
                     weak_ptr_factory_.GetWeakPtr(), row->node_id()));
}

void SidebarTreeView::NotifyNativeDragStarted(base::Uuid node_id) {
  SidebarTreeRowView* row = GetMaterializedRowForTesting(node_id);
  if (!delegate_ || !row || !row->is_bound() ||
      !row->is_native_drag_in_progress()) {
    return;
  }
  delegate_->OnSidebarDragStateChanged(node_id);
}

void SidebarTreeView::ShowContextMenuForViewImpl(
    views::View* /*source*/,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!delegate_) {
    return;
  }
  std::optional<base::Uuid> node_id;
  if (source_type == ui::mojom::MenuSourceType::kKeyboard) {
    node_id = model().selected_node_id();
  } else {
    gfx::Point local_point = screen_point;
    views::View::ConvertPointFromScreen(this, &local_point);
    node_id = NodeAtPoint(local_point);
    if (node_id.has_value()) {
      std::ignore = controller_->SelectNode(*node_id);
    }
  }
  delegate_->ShowNodeContextMenu(node_id, screen_point, source_type);
}

void SidebarTreeView::ScheduleSynchronization(bool preferred_size_changed) {
  synchronization_pending_ = true;
  preferred_size_change_pending_ |= preferred_size_changed;
  if (in_batch_update_) {
    return;
  }
  const bool notify_preferred_size = preferred_size_change_pending_;
  synchronization_pending_ = false;
  preferred_size_change_pending_ = false;
  if (notify_preferred_size) {
    PreferredSizeChanged();
  }
  InvalidateLayout();
  SynchronizeRows(GetVisibleBounds());
}

std::optional<SidebarTreeView::DropIndicator>
SidebarTreeView::CalculateDropIndicator(DropIndicator probe) {
  if (!probe.source_node_id.is_valid() || !model().workspace_id().has_value()) {
    return std::nullopt;
  }
  const base::Uuid source_node_id = probe.source_node_id;
  const SidebarTreeController::DropOperation operation = probe.operation;

  if (operation == SidebarTreeController::DropOperation::kMove && delegate_ &&
      probe.target_node_id == std::optional(source_node_id) &&
      probe.position != SidebarTreeController::DropPosition::kInside &&
      delegate_->CanExtractSavedSplitPaneForDrop(source_node_id,
                                                 std::nullopt)) {
    probe.action = DropIndicator::Action::kExtractSplitPane;
    return probe;
  }

  SidebarTreeController::DropTarget target{
      .workspace_id = *model().workspace_id(),
      .target_node_id = probe.target_node_id,
      .position = probe.position};
  if (probe.target_node_id.has_value() &&
      probe.position == SidebarTreeController::DropPosition::kInside &&
      operation == SidebarTreeController::DropOperation::kMove && delegate_) {
    const tab_tree::TreeNode* source = model().GetNode(source_node_id);
    const tab_tree::TreeNode* destination =
        model().GetNode(*probe.target_node_id);
    if (source && destination &&
        source->type == tab_tree::TreeNodeType::kSavedPage &&
        destination->type == tab_tree::TreeNodeType::kSavedPage) {
      if (delegate_->CanReorderSavedSplitPanes(source_node_id,
                                               *probe.target_node_id)) {
        probe.action = DropIndicator::Action::kReorderSplitPane;
        return probe;
      }
      if (delegate_->CanSplitSavedPages(source_node_id,
                                        *probe.target_node_id)) {
        probe.action = DropIndicator::Action::kSplit;
        return probe;
      }
      // The center of a saved-page row expresses split intent. When that
      // particular pair cannot be split, keep the drag useful by resolving to
      // the nearer insertion edge instead of exposing a dead 40% drop zone.
      if (probe.fallback_position.has_value()) {
        probe.position = *probe.fallback_position;
        target.position = *probe.fallback_position;
      }
    }
  }
  SidebarTreeController::DropPlan plan;
  if (controller_->ValidateDrop(source_node_id, target, operation, &plan) !=
      SidebarTreeController::DropValidationResult::kAllowed) {
    return std::nullopt;
  }
  return probe;
}

std::optional<SidebarTreeView::DropIndicator>
SidebarTreeView::CalculateTemporaryTabDropIndicator(DropIndicator probe) {
  if (!probe.source_runtime_tab_handle.has_value() ||
      !model().workspace_id().has_value() || !delegate_) {
    return std::nullopt;
  }
  const int runtime_tab_handle = *probe.source_runtime_tab_handle;
  if (probe.target_node_id.has_value() &&
      probe.position == SidebarTreeController::DropPosition::kInside) {
    const tab_tree::TreeNode* destination =
        model().GetNode(*probe.target_node_id);
    if (destination &&
        destination->type == tab_tree::TreeNodeType::kSavedPage) {
      if (delegate_->CanReorderTemporarySplitPane(runtime_tab_handle,
                                                  destination->id)) {
        probe.action = DropIndicator::Action::kReorderSplitPane;
        return probe;
      }
      if (delegate_->CanSaveAndSplitTemporaryTab(runtime_tab_handle,
                                                 destination->id)) {
        probe.action = DropIndicator::Action::kSplit;
        return probe;
      }
      // Match saved-tab drags: rejected split intent falls back to the
      // closest stable before/after target rather than making the row center
      // appear interactive while refusing the drop.
      if (probe.fallback_position.has_value()) {
        probe.position = *probe.fallback_position;
      }
    }
  }
  const SidebarTreeController::DropTarget target{
      .workspace_id = *model().workspace_id(),
      .target_node_id = probe.target_node_id,
      .position = probe.position};
  return delegate_->CanSaveTemporaryTab(runtime_tab_handle, target)
             ? std::make_optional(std::move(probe))
             : std::nullopt;
}

std::optional<SidebarTreeView::DropIndicator> SidebarTreeView::BuildDropProbe(
    const base::Uuid& source_node_id,
    const gfx::Point& point,
    SidebarTreeController::DropOperation operation,
    const std::vector<VisualRow>& visual_rows) const {
  if (!source_node_id.is_valid() || !model().workspace_id().has_value()) {
    return std::nullopt;
  }

  DropIndicator probe{.source_node_id = source_node_id,
                      .target_node_id = std::nullopt,
                      .position = SidebarTreeController::DropPosition::kInside,
                      .operation = operation};
  const auto& rows = model().rows();
  if (!visual_rows.empty()) {
    const int clamped_y =
        std::clamp(point.y(), 0, GetVisualRowsHeight(visual_rows) - 1);
    const std::optional<VisualHit> hit =
        FindVisualHit(visual_rows, gfx::Point(point.x(), clamped_y));
    if (!hit.has_value() || hit->model_index >= rows.size()) {
      return std::nullopt;
    }
    const size_t row_index = hit->model_index;
    probe.target_node_id = rows[row_index].node_id;
    probe.target_bounds = hit->bounds;
    const gfx::Rect& target_bounds = hit->bounds;
    const int offset = clamped_y - target_bounds.y();
    const int target_height = target_bounds.height();
    const int reorder_zone_height =
        GetSidebarEdgeDropTargetExtent(target_height);
    const auto nearest_position =
        offset < target_height / 2
            ? SidebarTreeController::DropPosition::kBefore
            : SidebarTreeController::DropPosition::kAfter;
    const bool targets_split_segment =
        visual_rows[hit->visual_row].model_indices.size() > 1;
    const bool targets_own_split_segment =
        targets_split_segment &&
        probe.target_node_id == std::optional(source_node_id);
    if ((targets_split_segment && !targets_own_split_segment) ||
        ((rows[row_index].type == tab_tree::TreeNodeType::kFolder ||
          rows[row_index].type == tab_tree::TreeNodeType::kSavedPage) &&
         offset >= reorder_zone_height &&
         offset < target_height - reorder_zone_height)) {
      probe.position = SidebarTreeController::DropPosition::kInside;
      if (rows[row_index].type == tab_tree::TreeNodeType::kSavedPage) {
        probe.fallback_position = nearest_position;
      }
    } else {
      probe.position = nearest_position;
    }
  }
  return probe;
}

std::optional<SidebarTreeView::DropIndicator>
SidebarTreeView::BuildTemporaryTabDropProbe(
    int runtime_tab_handle,
    const gfx::Point& point,
    const std::vector<VisualRow>& visual_rows) const {
  if (runtime_tab_handle < 0 || !model().workspace_id().has_value()) {
    return std::nullopt;
  }
  DropIndicator probe{.source_runtime_tab_handle = runtime_tab_handle,
                      .target_node_id = std::nullopt,
                      .position = SidebarTreeController::DropPosition::kInside,
                      .operation = SidebarTreeController::DropOperation::kMove};
  const auto& rows = model().rows();
  if (!visual_rows.empty()) {
    const int clamped_y =
        std::clamp(point.y(), 0, GetVisualRowsHeight(visual_rows) - 1);
    const std::optional<VisualHit> hit =
        FindVisualHit(visual_rows, gfx::Point(point.x(), clamped_y));
    if (!hit.has_value() || hit->model_index >= rows.size()) {
      return std::nullopt;
    }
    const size_t row_index = hit->model_index;
    probe.target_node_id = rows[row_index].node_id;
    probe.target_bounds = hit->bounds;
    const gfx::Rect& target_bounds = hit->bounds;
    const int offset = clamped_y - target_bounds.y();
    const int target_height = target_bounds.height();
    const int reorder_zone_height =
        GetSidebarEdgeDropTargetExtent(target_height);
    const auto nearest_position =
        offset < target_height / 2
            ? SidebarTreeController::DropPosition::kBefore
            : SidebarTreeController::DropPosition::kAfter;
    const bool targets_split_segment =
        visual_rows[hit->visual_row].model_indices.size() > 1;
    if (targets_split_segment ||
        ((rows[row_index].type == tab_tree::TreeNodeType::kFolder ||
          rows[row_index].type == tab_tree::TreeNodeType::kSavedPage) &&
         offset >= reorder_zone_height &&
         offset < target_height - reorder_zone_height)) {
      probe.position = SidebarTreeController::DropPosition::kInside;
      if (rows[row_index].type == tab_tree::TreeNodeType::kSavedPage) {
        probe.fallback_position = nearest_position;
      }
    } else {
      probe.position = nearest_position;
    }
  }
  return probe;
}

std::optional<int> SidebarTreeView::InsertionSlotY(
    const DropIndicator& indicator) const {
  if (!indicator.target_node_id.has_value() ||
      !indicator.target_bounds.has_value() ||
      indicator.position == SidebarTreeController::DropPosition::kInside) {
    return std::nullopt;
  }
  return indicator.position == SidebarTreeController::DropPosition::kBefore
             ? indicator.target_bounds->y()
             : indicator.target_bounds->bottom();
}

std::optional<SidebarTreeView::DropIndicator>
SidebarTreeView::StabilizeInsertionSlot(
    std::optional<DropIndicator> indicator) const {
  if (!indicator.has_value() || !drop_indicator_.has_value() ||
      indicator->source_node_id != drop_indicator_->source_node_id ||
      indicator->source_runtime_tab_handle !=
          drop_indicator_->source_runtime_tab_handle ||
      indicator->operation != drop_indicator_->operation) {
    return indicator;
  }
  const std::optional<int> current_y = InsertionSlotY(*drop_indicator_);
  const std::optional<int> next_y = InsertionSlotY(*indicator);
  const tab_tree::TreeNode* const current_target =
      drop_indicator_->target_node_id.has_value()
          ? model().GetNode(*drop_indicator_->target_node_id)
          : nullptr;
  const tab_tree::TreeNode* const next_target =
      indicator->target_node_id.has_value()
          ? model().GetNode(*indicator->target_node_id)
          : nullptr;
  // Adjacent rows describe the same insertion slot as either "after A" or
  // "before B" only when both nodes share the same persisted parent. Keep the
  // first validated semantic target so a tiny pointer movement cannot make the
  // marker hop, without crossing a folder boundary or changing ownership.
  constexpr int kEquivalentSlotTolerance = 3;
  if (current_target && next_target &&
      current_target->parent_id == next_target->parent_id &&
      current_y.has_value() && next_y.has_value() &&
      std::abs(*current_y - *next_y) <= kEquivalentSlotTolerance) {
    return drop_indicator_;
  }
  return indicator;
}

std::optional<SidebarTreeView::DropIndicator>
SidebarTreeView::StabilizeDropZone(
    std::optional<DropIndicator> indicator,
    const gfx::Point& point) const {
  if (!indicator.has_value() || !drop_indicator_.has_value() ||
      indicator->source_node_id != drop_indicator_->source_node_id ||
      indicator->source_runtime_tab_handle !=
          drop_indicator_->source_runtime_tab_handle ||
      indicator->operation != drop_indicator_->operation ||
      indicator->target_node_id != drop_indicator_->target_node_id ||
      indicator->target_bounds != drop_indicator_->target_bounds ||
      !indicator->target_bounds.has_value() ||
      indicator->position == drop_indicator_->position) {
    return indicator;
  }

  // Native AppKit drags report sub-pixel pointer jitter around a Views target
  // boundary. Keep the already validated semantic zone until the pointer has
  // crossed a small, symmetric dead band. This makes the painted target and
  // the eventual drop agree without allowing a stale target to survive a row
  // or model transition.
  const gfx::Rect& bounds = *indicator->target_bounds;
  const int edge_extent = GetSidebarEdgeDropTargetExtent(bounds.height());
  const int before_boundary = bounds.y() + edge_extent;
  const int after_boundary = bounds.bottom() - edge_extent;
  const int center_boundary = bounds.CenterPoint().y();
  constexpr int kDropZoneHysteresis = 4;
  const auto current = drop_indicator_->position;
  const auto next = indicator->position;
  if ((current == SidebarTreeController::DropPosition::kBefore &&
       next == SidebarTreeController::DropPosition::kInside &&
       point.y() < before_boundary + kDropZoneHysteresis) ||
      (current == SidebarTreeController::DropPosition::kInside &&
       next == SidebarTreeController::DropPosition::kBefore &&
       point.y() >= before_boundary - kDropZoneHysteresis) ||
      (current == SidebarTreeController::DropPosition::kAfter &&
       next == SidebarTreeController::DropPosition::kInside &&
       point.y() >= after_boundary - kDropZoneHysteresis) ||
      (current == SidebarTreeController::DropPosition::kInside &&
       next == SidebarTreeController::DropPosition::kAfter &&
       point.y() < after_boundary + kDropZoneHysteresis) ||
      // When a row cannot accept a center split, validation deliberately
      // resolves that center to before/after. Latch that direct transition as
      // well, otherwise the marker still flips on every pixel around 50%.
      (current == SidebarTreeController::DropPosition::kBefore &&
       next == SidebarTreeController::DropPosition::kAfter &&
       point.y() < center_boundary + kDropZoneHysteresis) ||
      (current == SidebarTreeController::DropPosition::kAfter &&
       next == SidebarTreeController::DropPosition::kBefore &&
       point.y() >= center_boundary - kDropZoneHysteresis)) {
    return drop_indicator_;
  }
  return indicator;
}

void SidebarTreeView::UpdateInsertionMarker() {
  if (!insertion_marker_) {
    return;
  }
  const std::optional<int> slot_y = drop_indicator_.has_value()
                                        ? InsertionSlotY(*drop_indicator_)
                                        : std::nullopt;
  if (!slot_y.has_value() || width() <= 20 || height() <= 0) {
    insertion_marker_->SetVisible(false);
    return;
  }

  // The edge is derived solely from the validated target bounds. It therefore
  // remains fixed while the pointer moves within the painted 30% zone and is
  // a companion to that surface, never the only available target affordance.
  constexpr int kMarkerHeight = 3;
  constexpr int kMarkerHorizontalInset = 10;
  const int y = std::clamp(*slot_y - kMarkerHeight / 2, 0,
                           std::max(height() - kMarkerHeight, 0));
  insertion_marker_->SetBounds(kMarkerHorizontalInset, y,
                               width() - 2 * kMarkerHorizontalInset,
                               kMarkerHeight);
  insertion_marker_->SetVisible(true);
  ReorderChildView(insertion_marker_, children().size() - 1);
}

void SidebarTreeView::SetDropIndicator(std::optional<DropIndicator> indicator) {
  if (drop_indicator_ == indicator) {
    return;
  }
  const std::optional<base::Uuid> old_target =
      drop_indicator_.has_value() ? drop_indicator_->target_node_id
                                  : std::nullopt;
  drop_indicator_ = std::move(indicator);
  drag_target_accepting_ = drop_indicator_.has_value();
  if (old_target.has_value()) {
    if (SidebarTreeRowView* row = GetMaterializedRowForTesting(*old_target)) {
      row->SetDropPosition(std::nullopt);
      row->SetSplitDropTarget(false);
    }
  }
  if (drop_indicator_.has_value() &&
      drop_indicator_->target_node_id.has_value()) {
    if (SidebarTreeRowView* row =
            GetMaterializedRowForTesting(*drop_indicator_->target_node_id)) {
      row->SetDropPosition(drop_indicator_->position);
      row->SetSplitDropTarget(drop_indicator_->action ==
                              DropIndicator::Action::kSplit);
    }
  }
  UpdateInsertionMarker();
  UpdateFolderAutoExpand(drop_indicator_);
  SchedulePaint();
}

void SidebarTreeView::UpdateFolderAutoExpand(
    const std::optional<DropIndicator>& indicator) {
  std::optional<base::Uuid> target_id;
  if (indicator.has_value() && indicator->target_node_id.has_value() &&
      indicator->action == DropIndicator::Action::kMoveOrCopy &&
      indicator->position == SidebarTreeController::DropPosition::kInside) {
    const tab_tree::TreeNode* node =
        model().GetNode(*indicator->target_node_id);
    if (node && node->type == tab_tree::TreeNodeType::kFolder &&
        !model().IsExpanded(node->id)) {
      target_id = node->id;
    }
  }
  if (target_id == pending_folder_expand_id_ &&
      folder_expand_timer_.IsRunning()) {
    return;
  }
  CancelFolderAutoExpand();
  if (!target_id.has_value()) {
    return;
  }
  pending_folder_expand_id_ = target_id;
  folder_expand_timer_.Start(
      FROM_HERE, visual_style::kFolderAutoExpandDelay,
      base::BindOnce(&SidebarTreeView::ExpandPendingFolder,
                     weak_ptr_factory_.GetWeakPtr()));
}

void SidebarTreeView::CancelFolderAutoExpand() {
  folder_expand_timer_.Stop();
  pending_folder_expand_id_.reset();
}

void SidebarTreeView::ExpandPendingFolder() {
  const std::optional<base::Uuid> target_id = pending_folder_expand_id_;
  pending_folder_expand_id_.reset();
  if (!target_id.has_value() || !drop_indicator_.has_value() ||
      drop_indicator_->target_node_id != target_id ||
      drop_indicator_->position !=
          SidebarTreeController::DropPosition::kInside ||
      drop_indicator_->action != DropIndicator::Action::kMoveOrCopy) {
    return;
  }
  const auto result = controller_->ExpandNode(*target_id);
  if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
    delegate_->OnMutationFailed(result);
  }
}

void SidebarTreeView::MaybeAutoScroll(const gfx::Point& point) {
  const gfx::Rect visible = GetVisibleBounds();
  if (visible.IsEmpty()) {
    return;
  }
  if (point.y() < visible.y() + kAutoScrollEdge && visible.y() > 0) {
    ScrollRectToVisible(gfx::Rect(
        point.x(), std::max(visible.y() - SidebarTreeRowView::kRowHeight, 0), 1,
        1));
    return;
  }
  if (point.y() > visible.bottom() - kAutoScrollEdge &&
      visible.bottom() < height()) {
    ScrollRectToVisible(
        gfx::Rect(point.x(),
                  std::min(visible.bottom() + SidebarTreeRowView::kRowHeight,
                           std::max(height() - 1, 0)),
                  1, 1));
  }
}

void SidebarTreeView::PerformDrop(
    DropIndicator indicator,
    const ui::DropTargetEvent& /*event*/,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> /*drag_image_owner*/) {
  // A successful tree mutation can synchronously recycle the dragged row.
  // Widget then cannot deliver OnDragDone() to that source View, so clear the
  // host's drag-only UI from the drop callback on every exit path as well.
  base::ScopedClosureRunner clear_drag_state(base::BindOnce(
      [](SidebarTreeViewDelegate* delegate, bool temporary_tab) {
        if (!delegate) {
          return;
        }
        if (temporary_tab) {
          delegate->OnTemporaryTabDragStateChanged(std::nullopt);
        } else {
          delegate->OnSidebarDragStateChanged(std::nullopt);
        }
      },
      delegate_, indicator.source_runtime_tab_handle.has_value()));
  if (!model().workspace_id().has_value()) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }
  if (indicator.source_runtime_tab_handle.has_value()) {
    bool saved = false;
    if (delegate_) {
      if (indicator.action == DropIndicator::Action::kReorderSplitPane &&
          indicator.target_node_id.has_value()) {
        saved = delegate_->ReorderTemporarySplitPane(
            *indicator.source_runtime_tab_handle, *indicator.target_node_id);
      } else if (indicator.action == DropIndicator::Action::kSplit &&
                 indicator.target_node_id.has_value()) {
        saved = delegate_->SaveAndSplitTemporaryTab(
            *indicator.source_runtime_tab_handle, *indicator.target_node_id);
      } else {
        saved = delegate_->SaveTemporaryTab(
            *indicator.source_runtime_tab_handle,
            {.workspace_id = *model().workspace_id(),
             .target_node_id = indicator.target_node_id,
             .position = indicator.position});
      }
    }
    output_drag_op = saved ? ui::mojom::DragOperation::kMove
                           : ui::mojom::DragOperation::kNone;
    return;
  }
  if (indicator.action == DropIndicator::Action::kExtractSplitPane) {
    const bool extracted =
        delegate_ &&
        delegate_->CanExtractSavedSplitPaneForDrop(indicator.source_node_id,
                                                   std::nullopt) &&
        delegate_->ExtractSavedSplitPaneAfterDrop(indicator.source_node_id);
    output_drag_op = extracted ? ui::mojom::DragOperation::kMove
                               : ui::mojom::DragOperation::kNone;
    return;
  }
  if (indicator.action == DropIndicator::Action::kSplit) {
    const bool split = delegate_ && indicator.target_node_id.has_value() &&
                       delegate_->SplitSavedPages(indicator.source_node_id,
                                                  *indicator.target_node_id);
    output_drag_op = split ? ui::mojom::DragOperation::kMove
                           : ui::mojom::DragOperation::kNone;
    return;
  }
  if (indicator.action == DropIndicator::Action::kReorderSplitPane) {
    const bool reordered =
        delegate_ && indicator.target_node_id.has_value() &&
        delegate_->ReorderSavedSplitPanes(indicator.source_node_id,
                                          *indicator.target_node_id);
    output_drag_op = reordered ? ui::mojom::DragOperation::kMove
                               : ui::mojom::DragOperation::kNone;
    return;
  }
  SidebarTreeController::DropTarget target{
      .workspace_id = *model().workspace_id(),
      .target_node_id = indicator.target_node_id,
      .position = indicator.position};
  const bool extract_split_pane =
      delegate_ &&
      indicator.operation == SidebarTreeController::DropOperation::kMove &&
      delegate_->CanExtractSavedSplitPaneForDrop(indicator.source_node_id,
                                                 indicator.target_node_id);
  std::vector<base::Uuid> move_group{indicator.source_node_id};
  if (!extract_split_pane && delegate_ &&
      indicator.operation == SidebarTreeController::DropOperation::kMove) {
    move_group = delegate_->GetMoveGroupNodeIds(indicator.source_node_id);
  }
  const std::optional<base::Uuid> selected_before_drop =
      model().selected_node_id();
  SidebarTreeController::DropExecutionResult result =
      move_group.size() > 1
          ? controller_->PerformGroupedDrop(
                move_group, target, indicator.operation, base::Time::Now())
          : controller_->PerformDrop(indicator.source_node_id, target,
                                     indicator.operation, base::Time::Now());
  if (!result.ok()) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    if (delegate_) {
      delegate_->OnMutationFailed(result.store_result);
    }
    return;
  }
  const base::Uuid selected_id =
      result.copied_root_id.value_or(indicator.source_node_id);
  if (extract_split_pane) {
    if (!delegate_->ExtractSavedSplitPaneAfterDrop(indicator.source_node_id)) {
      const tab_tree::TabTreeStore::Result undo_result =
          controller_->UndoLastMutation();
      if (undo_result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
        delegate_->OnMutationFailed(undo_result);
      }
      std::ignore = controller_->SelectNode(selected_before_drop);
      output_drag_op = ui::mojom::DragOperation::kNone;
      return;
    }
  }
  std::ignore = controller_->SelectNode(selected_id);
  output_drag_op = ToNativeDragOperation(indicator.operation);
}

}  // namespace ahoi::sidebar
