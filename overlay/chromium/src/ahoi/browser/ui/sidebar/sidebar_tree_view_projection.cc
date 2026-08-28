// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
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

void SidebarTreeView::OnBatchUpdateEnded() {
  in_batch_update_ = false;
  const int target_height = GetVisualRowsHeight(BuildVisualRows());
  if (pending_animation_from_height_.has_value()) {
    StartPreferredHeightAnimation(*pending_animation_from_height_,
                                  target_height);
    pending_animation_from_height_.reset();
  }
  last_visual_height_ = target_height;
  if (synchronization_pending_ || preferred_size_change_pending_) {
    ScheduleSynchronization(preferred_size_change_pending_);
  }
}

void SidebarTreeView::OnTreeReset() {
  editing_node_id_.reset();
  last_drop_probe_.reset();
  SetDropIndicator(std::nullopt);
  row_bounds_animator_.Cancel();
  row_bounds_animation_pending_ = false;
  row_bounds_animation_from_height_.reset();
  preferred_height_animation_.Reset(1.0);
  preferred_height_animation_active_ = false;
  last_visual_height_ = GetVisualRowsHeight(BuildVisualRows());
  pending_animation_from_height_.reset();
  ScheduleSynchronization(/*preferred_size_changed=*/true);
}

void SidebarTreeView::OnRowsInserted(size_t /*first_row*/, size_t /*count*/) {
  last_drop_probe_.reset();
  HandleVisualLayoutChanged();
  ScheduleSynchronization(/*preferred_size_changed=*/true);
}

void SidebarTreeView::OnRowsRemoved(size_t /*first_row*/, size_t /*count*/) {
  if (editing_node_id_.has_value() &&
      !model().GetRowForNode(*editing_node_id_).has_value()) {
    editing_node_id_.reset();
  }
  last_drop_probe_.reset();
  SetDropIndicator(std::nullopt);
  HandleVisualLayoutChanged();
  ScheduleSynchronization(/*preferred_size_changed=*/true);
}

void SidebarTreeView::OnRowsChanged(size_t /*first_row*/, size_t /*count*/) {
  last_drop_probe_.reset();
  ScheduleSynchronization(/*preferred_size_changed=*/false);
}

void SidebarTreeView::OnSelectionChanged(
    const std::optional<base::Uuid>& old_selection,
    const std::optional<base::Uuid>& new_selection) {
  if (old_selection.has_value()) {
    if (SidebarTreeRowView* row =
            GetMaterializedRowForTesting(*old_selection)) {
      row->SetSelected(false);
    }
  }
  if (new_selection.has_value()) {
    if (const std::optional<size_t> index =
            model().GetRowForNode(*new_selection)) {
      EnsureRowVisible(*index);
    }
    if (SidebarTreeRowView* row =
            GetMaterializedRowForTesting(*new_selection)) {
      row->SetSelected(true);
    }
  }
  UpdateActiveDescendant();
}

void SidebarTreeView::HandleVisualLayoutChanged() {
  const int target_height = GetVisualRowsHeight(BuildVisualRows());
  row_bounds_animation_pending_ = target_height != last_visual_height_ &&
                                  gfx::Animation::ShouldRenderRichAnimation();
  row_bounds_animation_from_height_ =
      row_bounds_animation_pending_ ? std::make_optional(last_visual_height_)
                                    : std::nullopt;
  if (in_batch_update_) {
    if (!pending_animation_from_height_.has_value()) {
      pending_animation_from_height_ = last_visual_height_;
    }
  } else {
    StartPreferredHeightAnimation(last_visual_height_, target_height);
  }
  last_visual_height_ = target_height;
}

void SidebarTreeView::StartPreferredHeightAnimation(int from_height,
                                                    int to_height) {
  animated_height_from_ = std::max(from_height, 0);
  animated_height_to_ = std::max(to_height, 0);
  if (animated_height_from_ == animated_height_to_ ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    preferred_height_animation_.Reset(1.0);
    preferred_height_animation_active_ = false;
    PreferredSizeChanged();
    return;
  }
  preferred_height_animation_active_ = true;
  preferred_height_animation_.Reset(0.0);
  preferred_height_animation_.Show();
}

void SidebarTreeView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &preferred_height_animation_) {
    PreferredSizeChanged();
    InvalidateLayout();
  }
}

void SidebarTreeView::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &preferred_height_animation_) {
    preferred_height_animation_active_ = false;
    PreferredSizeChanged();
    InvalidateLayout();
  }
}

void SidebarTreeView::AnimationCanceled(const gfx::Animation* animation) {
  AnimationEnded(animation);
}

void SidebarTreeView::WriteDragDataForView(views::View* sender,
                                           const gfx::Point& press_pt,
                                           ui::OSExchangeData* data) {
  auto* row = views::AsViewClass<SidebarTreeRowView>(sender);
  CHECK(row && row->is_bound());

  // Once Views has crossed the native drag threshold this press is no longer
  // a click. Clear it before Cocoa enters its nested drag loop so a canceled
  // or platform-aborted drag cannot activate the source row on mouse-up.
  pressed_node_id_.reset();

  // Cocoa refuses to start a native drag with a zero-sized drag image. Ahoi's
  // compact card also makes the dragged tab remain recognizable when it is
  // moved across a deeply nested tree.
  const gfx::ImageSkia image = row->GetDragImage();
  CHECK(!image.isNull());
  CHECK(!image.size().IsEmpty());
  data->provider().SetDragImage(
      image, GetSidebarDragImageCursorOffset(image, press_pt));
  row->SetIsDragging(true);

  // macOS needs at least one concrete pasteboard representation to create an
  // NSDraggingItem reliably. Keep Ahoi's private UUID payload for in-browser
  // moves, and publish the visible title as a harmless portable fallback.
  drag::WriteSavedSidebarTabDragPayload(data, row->node_id(), row->title());
  // Mirror the runtime-tab fallback: this method is guaranteed to run for a
  // native drag even on macOS paths that omit or reorder willBegin callbacks.
  if (delegate_) {
    delegate_->OnSidebarDragStateChanged(row->node_id());
  }
}

std::vector<SidebarTreeView::VisualRow> SidebarTreeView::BuildVisualRows()
    const {
  const std::vector<SidebarTreeViewModel::Row>& rows = model().rows();
  std::vector<VisualRow> visual_rows;
  visual_rows.reserve(rows.size());
  if (rows.empty()) {
    return visual_rows;
  }

  std::unordered_map<base::Uuid, size_t, base::UuidHash> index_by_node;
  index_by_node.reserve(rows.size());
  for (size_t index = 0; index < rows.size(); ++index) {
    if (runtime_composite_suppressed_nodes_.contains(rows[index].node_id)) {
      continue;
    }
    index_by_node.emplace(rows[index].node_id, index);
  }

  std::unordered_map<base::Uuid, std::vector<size_t>, base::UuidHash>
      group_by_node;
  std::unordered_map<base::Uuid, size_t, base::UuidHash> group_anchor_by_node;
  std::unordered_map<base::Uuid, split_tabs::SplitTabVisualData, base::UuidHash>
      split_visual_by_node;
  std::unordered_set<base::Uuid, base::UuidHash> claimed_nodes;
  if (delegate_) {
    for (const std::vector<base::Uuid>& group :
         delegate_->GetSplitSavedPageGroups()) {
      std::vector<size_t> indices;
      indices.reserve(group.size());
      std::unordered_set<base::Uuid, base::UuidHash> seen_in_group;
      bool conflicts_with_existing_group = false;
      for (const base::Uuid& node_id : group) {
        const auto found = index_by_node.find(node_id);
        if (found == index_by_node.end() ||
            rows[found->second].type != tab_tree::TreeNodeType::kSavedPage ||
            !seen_in_group.insert(node_id).second) {
          continue;
        }
        if (claimed_nodes.contains(node_id)) {
          conflicts_with_existing_group = true;
          break;
        }
        indices.push_back(found->second);
      }
      if (conflicts_with_existing_group || indices.size() < 2) {
        continue;
      }
      std::vector<base::Uuid> node_ids;
      node_ids.reserve(indices.size());
      for (const size_t index : indices) {
        node_ids.push_back(rows[index].node_id);
      }
      if (const std::optional<split_tabs::SplitTabVisualData> visual_data =
              delegate_->GetSplitSavedPageVisualData(node_ids);
          visual_data.has_value()) {
        for (const size_t index : indices) {
          split_visual_by_node.emplace(rows[index].node_id, *visual_data);
        }
      }
      for (const size_t index : indices) {
        claimed_nodes.insert(rows[index].node_id);
      }
      size_t anchor_index = indices.front();
      for (const size_t index : indices) {
        if (rows[index].depth > rows[anchor_index].depth ||
            (rows[index].depth == rows[anchor_index].depth &&
             index < anchor_index)) {
          anchor_index = index;
        }
      }
      for (const size_t index : indices) {
        group_by_node.emplace(rows[index].node_id, indices);
        group_anchor_by_node.emplace(rows[index].node_id, anchor_index);
      }
    }
  }

  std::unordered_set<base::Uuid, base::UuidHash> emitted_nodes;
  emitted_nodes.reserve(rows.size());
  for (const SidebarTreeViewModel::Row& row : rows) {
    if (runtime_composite_suppressed_nodes_.contains(row.node_id)) {
      continue;
    }
    if (emitted_nodes.contains(row.node_id)) {
      continue;
    }
    const auto split_group = group_by_node.find(row.node_id);
    if (split_group == group_by_node.end()) {
      const size_t index = index_by_node.at(row.node_id);
      visual_rows.push_back(
          VisualRow{.model_indices = {index}, .anchor_depth = row.depth});
      emitted_nodes.insert(row.node_id);
      continue;
    }
    if (index_by_node.at(row.node_id) != group_anchor_by_node.at(row.node_id)) {
      continue;
    }
    std::optional<split_tabs::SplitTabVisualData> visual_data;
    if (const auto visual = split_visual_by_node.find(row.node_id);
        visual != split_visual_by_node.end()) {
      visual_data = visual->second;
    }
    visual_rows.push_back(
        VisualRow{.model_indices = split_group->second,
                  .anchor_depth = row.depth,
                  .split_visual_data = std::move(visual_data)});
    for (const size_t index : split_group->second) {
      emitted_nodes.insert(rows[index].node_id);
    }
  }
  int next_y = 0;
  for (VisualRow& visual_row : visual_rows) {
    visual_row.y = next_y;
    if (visual_row.model_indices.size() >= 2 &&
        visual_row.split_visual_data.has_value()) {
      visual_row.height = GetSplitRowPreferredHeight(
          visual_row.model_indices.size(), *visual_row.split_visual_data,
          SidebarTreeRowView::kRowHeight);
    }
    next_y = base::saturated_cast<int>(static_cast<int64_t>(next_y) +
                                       visual_row.height);
  }
  return visual_rows;
}

std::vector<SidebarTreeView::VisualPosition>
SidebarTreeView::BuildVisualPositions(
    const std::vector<VisualRow>& visual_rows) const {
  std::vector<VisualPosition> positions(model().rows().size());
  for (size_t visual_index = 0; visual_index < visual_rows.size();
       ++visual_index) {
    const VisualRow& visual_row = visual_rows[visual_index];
    for (size_t segment_index = 0;
         segment_index < visual_row.model_indices.size(); ++segment_index) {
      const size_t model_index = visual_row.model_indices[segment_index];
      CHECK_LT(model_index, positions.size());
      positions[model_index] = {
          .visual_row = visual_index,
          .segment = segment_index,
          .segment_count = visual_row.model_indices.size(),
          .present = true};
    }
  }
  return positions;
}

gfx::Rect SidebarTreeView::GetSegmentBounds(const VisualRow& visual_row,
                                            size_t segment_index,
                                            int row_width) const {
  CHECK(!visual_row.model_indices.empty());
  CHECK_LT(segment_index, visual_row.model_indices.size());
  const size_t segment_count = visual_row.model_indices.size();
  if (segment_count == 1) {
    return gfx::Rect(0, visual_row.y, row_width, visual_row.height);
  }

  const int group_x = std::min(
      base::saturated_cast<int>(visual_row.anchor_depth) *
          SidebarTreeRowView::kIndentWidth,
      std::max(row_width - base::saturated_cast<int>(segment_count), 0));
  const int available_width = std::max(row_width - group_x, 1);
  const gfx::Rect split_bounds(group_x, visual_row.y, available_width,
                               visual_row.height);
  if (visual_row.split_visual_data.has_value()) {
    return GetSplitSegmentBounds(split_bounds, segment_index, segment_count,
                                 *visual_row.split_visual_data);
  }
  const int total_gap = visual_style::kSidebarSplitPaneGap *
                        base::saturated_cast<int>(segment_count - 1);
  const int content_width = std::max(available_width - total_gap, 1);
  const int base_width =
      content_width / base::saturated_cast<int>(segment_count);
  const int remainder =
      content_width % base::saturated_cast<int>(segment_count);
  const int segment = base::saturated_cast<int>(segment_index);
  const int x = group_x +
                segment * (base_width + visual_style::kSidebarSplitPaneGap) +
                std::min(segment, remainder);
  const int width = base_width + (segment < remainder ? 1 : 0);
  return gfx::Rect(x, visual_row.y, std::max(width, 1), visual_row.height);
}

void SidebarTreeView::SynchronizeRows(const gfx::Rect& visible_bounds) {
  if (in_batch_update_) {
    synchronization_pending_ = true;
    return;
  }
  const bool native_drag_in_progress =
      std::ranges::any_of(materialized_rows_, [](const auto& entry) {
        return entry.second && entry.second->is_native_drag_in_progress();
      });
  const bool rich_motion =
      gfx::Animation::ShouldRenderRichAnimation() && !native_drag_in_progress;
  if (!rich_motion && row_bounds_animator_.IsAnimating()) {
    row_bounds_animator_.Cancel();
  }
  const std::vector<SidebarTreeViewModel::Row>& rows = model().rows();
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::vector<VisualPosition> visual_positions =
      BuildVisualPositions(visual_rows);
  const VisibleRange range =
      CalculateVisibleRange(visual_rows, visible_bounds, kOverscanRows);

  std::vector<size_t> desired_indices;
  desired_indices.reserve((range.past_last - range.first) * 3U +
                          (editing_node_id_.has_value() ? 3U : 0U));
  std::unordered_set<base::Uuid, base::UuidHash> desired;
  desired.reserve(desired_indices.capacity());
  for (size_t visual_index = range.first; visual_index < range.past_last;
       ++visual_index) {
    for (const size_t model_index : visual_rows[visual_index].model_indices) {
      desired_indices.push_back(model_index);
      desired.insert(rows[model_index].node_id);
    }
  }
  if (editing_node_id_.has_value()) {
    const std::optional<size_t> editing_index =
        model().GetRowForNode(*editing_node_id_);
    if (editing_index.has_value() && visual_positions[*editing_index].present) {
      const VisualRow& editing_visual_row =
          visual_rows[visual_positions[*editing_index].visual_row];
      for (const size_t model_index : editing_visual_row.model_indices) {
        if (desired.insert(rows[model_index].node_id).second) {
          desired_indices.push_back(model_index);
        }
      }
    }
  }
  // Scrolling can change the virtualized viewport while AppKit owns the
  // pointer. Keep the source visual row materialized so Widget::dragged_view()
  // remains valid through OnDragDone(), even when the source would otherwise
  // move outside the overscan range.
  for (const auto& entry : materialized_rows_) {
    if (!entry.second || !entry.second->is_dragging_for_presentation()) {
      continue;
    }
    const std::optional<size_t> dragged_index =
        model().GetRowForNode(entry.first);
    if (!dragged_index.has_value() ||
        !visual_positions[*dragged_index].present) {
      continue;
    }
    const VisualRow& dragged_visual_row =
        visual_rows[visual_positions[*dragged_index].visual_row];
    for (const size_t model_index : dragged_visual_row.model_indices) {
      if (desired.insert(rows[model_index].node_id).second) {
        desired_indices.push_back(model_index);
      }
    }
  }
  std::ranges::sort(desired_indices, {}, [&](size_t model_index) {
    const VisualPosition& position = visual_positions[model_index];
    return std::pair(position.visual_row, position.segment);
  });

  std::vector<base::Uuid> stale;
  stale.reserve(materialized_rows_.size());
  for (const auto& entry : materialized_rows_) {
    if (!desired.contains(entry.first)) {
      stale.push_back(entry.first);
    }
  }
  for (const base::Uuid& node_id : stale) {
    RecycleRow(node_id);
  }

  // ScrollView can momentarily keep the contents view at its previous width
  // while reporting the already-resized viewport through `visible_bounds`.
  // Rows must follow that live viewport immediately; using the larger stale
  // contents width clips titles and trailing actions during sidebar resize.
  const int row_width = visible_bounds.width() > 0 ? visible_bounds.width()
                                                   : std::max(width(), 1);
  size_t child_order = 0;
  for (const size_t index : desired_indices) {
    CHECK_LT(index, rows.size());
    const base::Uuid& node_id = rows[index].node_id;
    const tab_tree::TreeNode* node = model().GetNode(node_id);
    CHECK(node);
    SidebarTreeRowView* row = GetMaterializedRowForTesting(node_id);
    const bool was_materialized = row != nullptr;
    if (!row) {
      row = AcquireRow();
      materialized_rows_.emplace(node_id, row);
    }
    const VisualPosition& position = visual_positions[index];
    row->Bind(
        index, rows[index], *node, model().selected_node_id() == node_id,
        position.segment, position.segment_count,
        delegate_ ? delegate_->GetSavedPageIcon(*node) : ui::ImageModel(),
        delegate_ ? delegate_->GetSavedPageMediaIndicator(*node)
                  : ui::ImageModel(),
        delegate_ ? delegate_->GetSavedPageStatusText(*node) : std::u16string(),
        delegate_ ? delegate_->GetSavedPageDragThumbnails(node_id)
                  : std::vector<gfx::ImageSkia>(),
        delegate_ && delegate_->IsSavedPageRunning(node_id),
        delegate_ && delegate_->IsSavedPageSleeping(node_id));
    const bool is_drop_target = drop_indicator_.has_value() &&
                                drop_indicator_->target_node_id == node_id;
    row->SetDropPosition(is_drop_target
                             ? std::make_optional(drop_indicator_->position)
                             : std::nullopt);
    row->SetSplitDropTarget(is_drop_target &&
                            drop_indicator_->action ==
                                DropIndicator::Action::kSplit);
    const gfx::Rect target_bounds = GetSegmentBounds(
        visual_rows[position.visual_row], position.segment, row_width);
    if (row_bounds_animator_.IsAnimating(row)) {
      if (row_bounds_animator_.GetTargetBounds(row) != target_bounds) {
        if (row_bounds_animation_pending_ && rich_motion) {
          row_bounds_animator_.AnimateViewTo(row, target_bounds);
        } else {
          row_bounds_animator_.SetTargetBounds(row, target_bounds);
        }
      }
    } else if (row_bounds_animation_pending_ && rich_motion &&
               was_materialized && row->bounds() != target_bounds) {
      row_bounds_animator_.AnimateViewTo(row, target_bounds);
    } else if (row_bounds_animation_pending_ && rich_motion &&
               !was_materialized &&
               row_bounds_animation_from_height_.value_or(0) > 0 &&
               position.visual_row > 0) {
      gfx::Rect start_bounds = target_bounds;
      start_bounds.set_y(visual_rows[position.visual_row - 1].y);
      row->SetBoundsRect(start_bounds);
      row_bounds_animator_.AnimateViewTo(row, target_bounds);
    } else {
      row->SetBoundsRect(target_bounds);
    }
    ReorderChildView(row, child_order++);
  }
  row_bounds_animation_pending_ = false;
  row_bounds_animation_from_height_.reset();
  UpdateInsertionMarker();
  UpdateActiveDescendant();
}

SidebarTreeRowView* SidebarTreeView::AcquireRow() {
  std::unique_ptr<SidebarTreeRowView> row;
  if (recycled_rows_.empty()) {
    row = std::make_unique<SidebarTreeRowView>(this, split_with_prefix_);
    row->set_drag_controller(this);
  } else {
    row = std::move(recycled_rows_.back());
    recycled_rows_.pop_back();
  }
  return AddChildView(std::move(row));
}

void SidebarTreeView::RecycleRow(const base::Uuid& node_id) {
  auto found = materialized_rows_.find(node_id);
  if (found == materialized_rows_.end()) {
    return;
  }
  SidebarTreeRowView* row = found->second;
  materialized_rows_.erase(found);
  row_bounds_animator_.StopAnimatingView(row);
  row->Unbind();
  recycled_rows_.push_back(RemoveChildViewT(row));
}

void SidebarTreeView::UpdateActiveDescendant() {
  if (!model().selected_node_id().has_value()) {
    GetViewAccessibility().ClearActiveDescendant();
    return;
  }
  SidebarTreeRowView* selected =
      GetMaterializedRowForTesting(*model().selected_node_id());
  if (selected) {
    GetViewAccessibility().SetActiveDescendant(*selected);
  } else {
    GetViewAccessibility().ClearActiveDescendant();
  }
}

void SidebarTreeView::EnsureRowVisible(size_t row_index) {
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::vector<VisualPosition> positions =
      BuildVisualPositions(visual_rows);
  if (row_index >= positions.size() || !positions[row_index].present) {
    return;
  }
  const VisualRow& visual_row = visual_rows[positions[row_index].visual_row];
  ScrollRectToVisible(
      gfx::Rect(0, visual_row.y, std::max(width(), 1), visual_row.height));
  SynchronizeRows(GetVisibleBounds());
}

void SidebarTreeView::SelectRow(size_t row_index) {
  if (row_index >= model().rows().size() ||
      runtime_composite_suppressed_nodes_.contains(
          model().rows()[row_index].node_id)) {
    return;
  }
  std::ignore = controller_->SelectNode(model().rows()[row_index].node_id);
  EnsureRowVisible(row_index);
}

void SidebarTreeView::SelectRelativeRow(int delta) {
  const auto& rows = model().rows();
  if (rows.empty()) {
    return;
  }
  if (!model().selected_node_id().has_value()) {
    size_t target = delta < 0 ? rows.size() : 0;
    while (delta < 0 && target > 0) {
      --target;
      if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
        SelectRow(target);
        return;
      }
    }
    while (delta >= 0 && target < rows.size()) {
      if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
        SelectRow(target);
        return;
      }
      ++target;
    }
    return;
  }
  const std::optional<size_t> selected =
      model().GetRowForNode(*model().selected_node_id());
  if (!selected.has_value()) {
    SelectRow(0);
    return;
  }
  size_t target = *selected;
  while (delta < 0 && target > 0) {
    --target;
    if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
      SelectRow(target);
      return;
    }
  }
  while (delta >= 0 && target + 1 < rows.size()) {
    ++target;
    if (!runtime_composite_suppressed_nodes_.contains(rows[target].node_id)) {
      SelectRow(target);
      return;
    }
  }
}

void SidebarTreeView::CollapseOrSelectParent() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid selected_id = *model().selected_node_id();
  const std::optional<size_t> selected_index =
      model().GetRowForNode(selected_id);
  if (!selected_index.has_value()) {
    return;
  }
  const auto& row = model().rows()[*selected_index];
  if (row.type == tab_tree::TreeNodeType::kFolder && row.expanded) {
    std::ignore = controller_->CollapseNode(selected_id);
    return;
  }
  if (row.depth == 0) {
    return;
  }
  for (size_t index = *selected_index; index > 0; --index) {
    if (model().rows()[index - 1].depth < row.depth) {
      SelectRow(index - 1);
      return;
    }
  }
}

void SidebarTreeView::ExpandOrSelectChild() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid selected_id = *model().selected_node_id();
  const std::optional<size_t> selected_index =
      model().GetRowForNode(selected_id);
  if (!selected_index.has_value()) {
    return;
  }
  const auto& row = model().rows()[*selected_index];
  if (row.type != tab_tree::TreeNodeType::kFolder) {
    return;
  }
  if (!row.expanded) {
    const auto result = controller_->ExpandNode(selected_id);
    if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
      delegate_->OnMutationFailed(result);
    }
    return;
  }
  for (size_t index = *selected_index + 1; index < model().rows().size();
       ++index) {
    const auto& child = model().rows()[index];
    if (child.depth <= row.depth) {
      return;
    }
    if (child.depth == row.depth + 1 &&
        !runtime_composite_suppressed_nodes_.contains(child.node_id)) {
      SelectRow(index);
      return;
    }
  }
}

void SidebarTreeView::ActivateSelectedNode() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid node_id = *model().selected_node_id();
  const tab_tree::TreeNode* node = model().GetNode(node_id);
  if (!node) {
    return;
  }
  if (node->type == tab_tree::TreeNodeType::kFolder) {
    if (model().IsExpanded(node_id)) {
      std::ignore = controller_->CollapseNode(node_id);
    } else {
      const auto result = controller_->ExpandNode(node_id);
      if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
        delegate_->OnMutationFailed(result);
      }
    }
  } else if (delegate_) {
    delegate_->ActivateSavedPage(*node);
  }
}

std::optional<base::Uuid> SidebarTreeView::NodeAtPoint(
    const gfx::Point& point) const {
  if (point.y() < 0) {
    return std::nullopt;
  }
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::optional<size_t> visual_index =
      FindVisualRowAtY(visual_rows, point.y());
  if (!visual_index.has_value()) {
    return std::nullopt;
  }
  const VisualRow& visual_row = visual_rows[*visual_index];
  size_t closest_segment = 0;
  int closest_distance = std::numeric_limits<int>::max();
  for (size_t segment = 0; segment < visual_row.model_indices.size();
       ++segment) {
    const gfx::Rect bounds =
        GetSegmentBounds(visual_row, segment, std::max(width(), 1));
    if (bounds.Contains(point)) {
      closest_segment = segment;
      break;
    }
    const int distance = std::abs(point.x() - bounds.CenterPoint().x());
    if (distance < closest_distance) {
      closest_distance = distance;
      closest_segment = segment;
    }
  }
  return model().rows()[visual_row.model_indices[closest_segment]].node_id;
}

}  // namespace ahoi::sidebar
