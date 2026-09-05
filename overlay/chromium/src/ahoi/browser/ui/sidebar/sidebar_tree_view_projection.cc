// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_resize_area.h"
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
  CancelSelectionReveal();
  pending_folder_reveal_.reset();
  exiting_split_clip_groups_.clear();
  editing_node_id_.reset();
  last_drop_probe_.reset();
  SetDropIndicator(std::nullopt);
  materialized_split_clip_groups_.clear();
  row_bounds_animator_.Cancel();
  row_bounds_animation_pending_ = false;
  preferred_height_animation_.Reset(1.0);
  preferred_height_animation_active_ = false;
  last_visual_height_ = GetVisualRowsHeight(BuildVisualRows());
  pending_animation_from_height_.reset();
  ScheduleSynchronization(/*preferred_size_changed=*/true);
}

void SidebarTreeView::OnRowsInserted(size_t /*first_row*/, size_t /*count*/) {
  if (pending_folder_reveal_ && pending_folder_reveal_->expanded) {
    pending_folder_reveal_->splice_ready = true;
  }
  last_drop_probe_.reset();
  HandleVisualLayoutChanged();
  ScheduleSynchronization(/*preferred_size_changed=*/true);
}

void SidebarTreeView::OnRowsRemoved(size_t first_row, size_t count) {
  if (pending_folder_reveal_ && !pending_folder_reveal_->expanded) {
    pending_folder_reveal_->splice_ready = true;
  }
  PrepareFolderExit(first_row, count);
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


void SidebarTreeView::WriteDragDataForView(views::View* sender,
                                           const gfx::Point& press_pt,
                                           ui::OSExchangeData* data) {
  if (model().is_search_projection_active()) {
    return;
  }
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
      if (model().is_search_projection_active()) {
        // Search is a hierarchy projection, so a live split must not visually
        // reparent a partner from another folder into the exact match's row.
        // Only panes that already occupy one contiguous sibling slot can be
        // presented as a single segmented row inside that projection.
        const tab_tree::TreeNode* first_node =
            model().GetNode(rows[indices.front()].node_id);
        const size_t common_depth = rows[indices.front()].depth;
        size_t minimum_index = indices.front();
        size_t maximum_index = indices.front();
        bool shares_one_hierarchy_slot = first_node != nullptr;
        for (const size_t index : indices) {
          const tab_tree::TreeNode* node = model().GetNode(rows[index].node_id);
          shares_one_hierarchy_slot =
              shares_one_hierarchy_slot && node &&
              node->parent_id == first_node->parent_id &&
              rows[index].depth == common_depth;
          minimum_index = std::min(minimum_index, index);
          maximum_index = std::max(maximum_index, index);
        }
        shares_one_hierarchy_slot =
            shares_one_hierarchy_slot &&
            maximum_index - minimum_index + 1u == indices.size();
        if (!shares_one_hierarchy_slot) {
          continue;
        }
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
  if (in_batch_update_ ||
      (pending_folder_reveal_ && !pending_folder_reveal_->splice_ready)) {
    // A folder changes its own expanded flag before the child splice. Do not
    // recycle completed exit rows against that intermediate model: reopening
    // must get the chance to reclaim those same UUIDs in OnRowsInserted.
    synchronization_pending_ = true;
    return;
  }
  if (visible_bounds.IsEmpty()) {
    // A presentation animation can temporarily clip the effective viewport to
    // zero even though the persistent model and the tree's own bounds are
    // unchanged. Treat that as deferred materialization. Recycling here makes
    // the saved section stay blank after reveal because the final compositor
    // frame need not change any descendant's numerical bounds. The retained
    // set is already bounded to the previous viewport plus overscan. The frame
    // host explicitly schedules a stable non-empty synchronization after a
    // completed reveal so hidden mutations are still reconciled.
    return;
  }
  // A search result is a transient navigation surface. If filtering begins
  // while a rename field is open, end that edit without stealing focus back
  // from the search field or committing text against a projected row.
  if (model().is_search_projection_active() && editing_node_id_.has_value()) {
    if (SidebarTreeRowView* editing_row =
            GetMaterializedRowForTesting(*editing_node_id_)) {
      editing_row->StopEditing(/*restore_model_title=*/true);
    }
    editing_node_id_.reset();
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
  if (!rich_motion && preferred_height_animation_active_) {
    preferred_height_animation_.Reset(1.0);
    preferred_height_animation_active_ = false;
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
    if (!entry.second) {
      continue;
    }
    const bool moving_through_viewport =
        rich_motion &&
        (row_bounds_animation_pending_ ||
         row_bounds_animator_.IsAnimating(entry.second)) &&
        entry.second->bounds().Intersects(visible_bounds);
    if (!entry.second->is_dragging_for_presentation() &&
        !moving_through_viewport) {
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
    auto* row = GetMaterializedRowForTesting(node_id);
    if (exiting_rows_.contains(node_id) && rich_motion && row &&
        row_bounds_animator_.IsAnimating(row)) {
      continue;
    }
    RecycleRow(node_id);
  }

  // ScrollView can momentarily keep the contents view at its previous width
  // while reporting the already-resized viewport through `visible_bounds`.
  // Rows must follow that live viewport immediately; using the larger stale
  // contents width clips titles and trailing actions during sidebar resize.
  const int row_width = visible_bounds.width() > 0 ? visible_bounds.width()
                                                   : std::max(width(), 1);
  materialized_split_clip_groups_.clear();
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
    // Reopening during collapse restores the very same row/UUID and retargets
    // from its current bounds; a queued old cleanup cannot recycle it.
    exiting_rows_.erase(node_id);
    row->SetExiting(false);
    // Search rows preserve activation and keyboard navigation, but cannot be
    // a reorder source. Clearing the projection restores the same recycled
    // row's controller on the next synchronization.
    row->set_drag_controller(model().is_search_projection_active() ? nullptr
                                                                   : this);
    const VisualPosition& position = visual_positions[index];
    if (position.segment == 0 && position.segment_count > 1) {
      auto& group = materialized_split_clip_groups_.emplace_back();
      for (size_t member : visual_rows[position.visual_row].model_indices) {
        group.push_back(rows[member].node_id);
      }
    }
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
    const std::optional<int> entry_origin = FolderEntryOrigin(index);
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
               !was_materialized && entry_origin.has_value()) {
      gfx::Rect start_bounds = target_bounds;
      start_bounds.set_y(*entry_origin);
      start_bounds.set_height(0);
      row->SetBoundsRect(start_bounds);
      row_bounds_animator_.AnimateViewTo(row, target_bounds);
    } else {
      row->SetBoundsRect(target_bounds);
    }
    if (position.segment_count == 1) {
      row->SetSplitGroupClipBounds(std::nullopt);
    }
    ReorderChildView(row, child_order++);
  }
  row_bounds_animation_pending_ = false;
  if (pending_folder_reveal_ && pending_folder_reveal_->splice_ready) {
    pending_folder_reveal_.reset();
  }
  std::erase_if(exiting_split_clip_groups_, [this](const auto& group) {
    return std::ranges::none_of(group, [this](const base::Uuid& id) {
      return exiting_rows_.contains(id);
    });
  });
  UpdateAnimatedSplitClips();
  SynchronizeSplitResizeAreas(visual_rows, range, row_width,
                              native_drag_in_progress);
  UpdateInsertionMarker();
  ReorderChildView(insertion_marker_, children().size() - 1u);
  UpdateActiveDescendant();
}

void SidebarTreeView::SynchronizeSplitResizeAreas(
    const std::vector<VisualRow>& visual_rows,
    const VisibleRange& visible_range,
    int row_width,
    bool native_drag_in_progress) {
  std::set<std::string> desired_keys;
  if (!delegate_ || model().is_search_projection_active()) {
    for (auto iterator = split_resize_areas_.begin();
         iterator != split_resize_areas_.end();) {
      if (iterator->second->is_resizing()) {
        ++iterator;
        continue;
      }
      RemoveChildViewT(iterator->second);
      iterator = split_resize_areas_.erase(iterator);
    }
    return;
  }

  const auto& rows = model().rows();
  for (size_t visual_index = visible_range.first;
       visual_index < visible_range.past_last; ++visual_index) {
    const VisualRow& visual_row = visual_rows[visual_index];
    if (visual_row.model_indices.size() < 2 ||
        !visual_row.split_visual_data.has_value()) {
      continue;
    }
    std::vector<base::Uuid> node_ids;
    std::vector<gfx::Rect> segment_bounds;
    node_ids.reserve(visual_row.model_indices.size());
    segment_bounds.reserve(visual_row.model_indices.size());
    gfx::Rect group_bounds;
    for (size_t segment = 0; segment < visual_row.model_indices.size();
         ++segment) {
      const size_t model_index = visual_row.model_indices[segment];
      if (model_index >= rows.size()) {
        node_ids.clear();
        break;
      }
      node_ids.push_back(rows[model_index].node_id);
      segment_bounds.push_back(
          GetSegmentBounds(visual_row, segment, row_width));
      group_bounds.Union(segment_bounds.back());
    }
    if (node_ids.size() < 2) {
      continue;
    }
    gfx::RectF paint_bounds(group_bounds);
    paint_bounds.Inset(
        gfx::InsetsF::VH(visual_style::kSidebarTabRowVerticalInset,
                         visual_style::kSidebarTabRowHorizontalInset));
    for (const SidebarSplitDivider& divider :
         GetSidebarSplitDividers(group_bounds, segment_bounds, paint_bounds,
                                 *visual_row.split_visual_data)) {
      const std::string key = node_ids.front().AsLowercaseString() + ":" +
                              std::to_string(divider.divider_index);
      desired_keys.insert(key);
      const SidebarSplitResizeCallback callback = base::BindRepeating(
          [](base::WeakPtr<SidebarTreeView> tree_view,
             const std::vector<base::Uuid>& split_node_ids,
             size_t divider_index, double ratio, bool done_resizing) {
            return tree_view &&
                   tree_view->ResizeSavedSplit(split_node_ids, divider_index,
                                               ratio, done_resizing);
          },
          weak_ptr_factory_.GetWeakPtr(), node_ids);
      SidebarSplitResizeArea* resize_area = nullptr;
      if (const auto existing = split_resize_areas_.find(key);
          existing != split_resize_areas_.end()) {
        resize_area = existing->second;
        resize_area->UpdateConfiguration(divider, callback);
      } else {
        resize_area = AddChildView(
            std::make_unique<SidebarSplitResizeArea>(divider, callback));
        split_resize_areas_.emplace(key, resize_area);
      }
      gfx::Rect hit_bounds = GetSidebarSplitDividerHitBounds(divider);
      hit_bounds.Intersect(GetLocalBounds());
      resize_area->SetBoundsRect(hit_bounds);
      resize_area->SetEnabled(!native_drag_in_progress);
      resize_area->SetVisible(!hit_bounds.IsEmpty());
      ReorderChildView(resize_area, children().size() - 1u);
    }
  }

  for (auto iterator = split_resize_areas_.begin();
       iterator != split_resize_areas_.end();) {
    if (desired_keys.contains(iterator->first) ||
        iterator->second->is_resizing()) {
      ++iterator;
      continue;
    }
    RemoveChildViewT(iterator->second);
    iterator = split_resize_areas_.erase(iterator);
  }
}

bool SidebarTreeView::ResizeSavedSplit(const std::vector<base::Uuid>& node_ids,
                                       size_t divider_index,
                                       double ratio,
                                       bool done_resizing) {
  if (!delegate_ || !delegate_->ResizeSavedPageSplit(node_ids, divider_index,
                                                     ratio, done_resizing)) {
    return false;
  }
  // The callback updated SplitTabVisualData synchronously. Re-layout the same
  // materialized rows in place; rebuilding/recycling them while a child owns
  // mouse capture would invalidate the native resize gesture.
  InvalidateLayout();
  SchedulePaint();
  return true;
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
  // AppKit still owns the source until OnDragDone, even if its model row was
  // removed by a concurrent fold/reset. The drag-end synchronization retries.
  if (row->is_native_drag_in_progress() ||
      row->is_dragging_for_presentation()) {
    return;
  }
  materialized_rows_.erase(found);
  exiting_rows_.erase(node_id);
  row_bounds_animator_.StopAnimatingView(row);
  row->Unbind();
  recycled_rows_.push_back(RemoveChildViewT(row));
}

}  // namespace ahoi::sidebar
