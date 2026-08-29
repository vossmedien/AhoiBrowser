// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/sequenced_task_runner.h"
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
#include "ui/views/background.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/drag_utils.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

SidebarTreeView::SidebarTreeView(SidebarTreeController* controller,
                                 SidebarTreeViewDelegate* delegate,
                                 std::u16string accessible_name,
                                 std::u16string split_with_prefix)
    : controller_(controller),
      delegate_(delegate),
      split_with_prefix_(std::move(split_with_prefix)) {
  CHECK(controller_);
  CHECK(!accessible_name.empty());
  CHECK(!split_with_prefix_.empty());
  SetFocusBehavior(FocusBehavior::ALWAYS);
  set_context_menu_controller(this);
  views::FocusRing::Install(this);
  views::FocusRing::Get(this)->SetColorId(visual_style::kAccent);
  GetViewAccessibility().SetRole(ax::mojom::Role::kTree);
  GetViewAccessibility().SetName(accessible_name);
  GetViewAccessibility().SetIsVertical(true);
  GetViewAccessibility().SetReadOnly(true);
  preferred_height_animation_.SetSlideDuration(
      visual_style::kTreeMotionDuration);
  row_bounds_animator_.SetAnimationDuration(visual_style::kTreeMotionDuration);
  // This paint-only child is deliberately outside materialized_rows_. The
  // full target surface is painted by the row; this topmost fixed edge only
  // disambiguates before/after and never participates in layout.
  insertion_marker_ = AddChildView(std::make_unique<views::View>());
  insertion_marker_->SetBackground(
      views::CreateRoundedRectBackground(visual_style::kAccent, 2.0f));
  insertion_marker_->SetCanProcessEventsWithinSubtree(false);
  insertion_marker_->GetViewAccessibility().SetIsIgnored(true);
  insertion_marker_->SetVisible(false);
  last_visual_height_ = GetVisualRowsHeight(BuildVisualRows());
  model().AddObserver(this);
}

SidebarTreeView::~SidebarTreeView() {
  model().RemoveObserver(this);
  set_context_menu_controller(nullptr);
  for (auto& entry : materialized_rows_) {
    entry.second->set_drag_controller(nullptr);
  }
  for (const auto& row : recycled_rows_) {
    row->set_drag_controller(nullptr);
  }
}

// static
std::unique_ptr<views::ScrollView> SidebarTreeView::CreateScrollView(
    std::unique_ptr<SidebarTreeView> tree_view) {
  CHECK(tree_view);
  auto scroll_view = std::make_unique<views::ScrollView>();
  // The owning sidebar surface supplies the themed opaque/glass background.
  // Keeping this scroll container transparent avoids painting a second full
  // grey slab above the backdrop.
  scroll_view->SetBackground(nullptr);
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view->SetDrawOverflowIndicator(false);
  scroll_view->SetContents(std::move(tree_view));
  return scroll_view;
}

// static
SidebarTreeView::VisibleRange SidebarTreeView::CalculateVisibleRange(
    const std::vector<VisualRow>& visual_rows,
    const gfx::Rect& visible_bounds,
    size_t overscan_rows) {
  if (visual_rows.empty() || visible_bounds.height() <= 0) {
    return {};
  }
  const int top = std::max(visible_bounds.y(), 0);
  const int bottom = std::max(visible_bounds.bottom(), top);
  auto first =
      std::lower_bound(visual_rows.begin(), visual_rows.end(), top,
                       [](const VisualRow& row, int y) {
                         return static_cast<int64_t>(row.y) + row.height <= y;
                       });
  auto past_last =
      std::lower_bound(first, visual_rows.end(), bottom,
                       [](const VisualRow& row, int y) { return row.y < y; });
  size_t first_index =
      static_cast<size_t>(std::distance(visual_rows.begin(), first));
  size_t past_last_index =
      static_cast<size_t>(std::distance(visual_rows.begin(), past_last));
  first_index = first_index > overscan_rows ? first_index - overscan_rows : 0;
  past_last_index =
      std::min(visual_rows.size(), past_last_index + overscan_rows);
  return {.first = first_index, .past_last = past_last_index};
}

// static
int SidebarTreeView::GetVisualRowsHeight(
    const std::vector<VisualRow>& visual_rows) {
  if (visual_rows.empty()) {
    return 0;
  }
  const VisualRow& last = visual_rows.back();
  return base::saturated_cast<int>(static_cast<int64_t>(last.y) + last.height);
}

// static
std::optional<size_t> SidebarTreeView::FindVisualRowAtY(
    const std::vector<VisualRow>& visual_rows,
    int y) {
  if (y < 0) {
    return std::nullopt;
  }
  auto found = std::lower_bound(
      visual_rows.begin(), visual_rows.end(), y,
      [](const VisualRow& row, int point_y) {
        return static_cast<int64_t>(row.y) + row.height <= point_y;
      });
  if (found == visual_rows.end() || y < found->y) {
    return std::nullopt;
  }
  return static_cast<size_t>(std::distance(visual_rows.begin(), found));
}

// static
SidebarTreeView::VisibleRange SidebarTreeView::CalculateVisibleRange(
    size_t row_count,
    const gfx::Rect& visible_bounds,
    size_t overscan_rows) {
  if (row_count == 0 || visible_bounds.height() <= 0) {
    return {};
  }
  const int top = std::max(0, visible_bounds.y());
  const int bottom = std::max(top, visible_bounds.bottom());
  size_t first = static_cast<size_t>(top / SidebarTreeRowView::kRowHeight);
  size_t past_last = static_cast<size_t>(
      (static_cast<int64_t>(bottom) + SidebarTreeRowView::kRowHeight - 1) /
      SidebarTreeRowView::kRowHeight);
  first = std::min(first, row_count);
  past_last = std::min(std::max(past_last, first), row_count);
  first = first > overscan_rows ? first - overscan_rows : 0;
  past_last = std::min(row_count, past_last + overscan_rows);
  return {.first = first, .past_last = past_last};
}

void SidebarTreeView::BeginRenameSelectedNode() {
  if (!model().selected_node_id().has_value()) {
    return;
  }
  const base::Uuid node_id = *model().selected_node_id();
  const std::optional<size_t> row_index = model().GetRowForNode(node_id);
  if (!row_index.has_value()) {
    return;
  }
  if (editing_node_id_.has_value() && editing_node_id_ != node_id) {
    CancelRename();
  }
  editing_node_id_ = node_id;
  EnsureRowVisible(*row_index);
  SynchronizeRows(GetVisibleBounds());
  if (SidebarTreeRowView* row = GetMaterializedRowForTesting(node_id)) {
    row->StartEditing();
  }
}

void SidebarTreeView::CancelRename() {
  if (!editing_node_id_.has_value()) {
    return;
  }
  CancelRename(*editing_node_id_);
}

SidebarTreeRowView* SidebarTreeView::GetMaterializedRowForTesting(
    const base::Uuid& node_id) const {
  auto row = materialized_rows_.find(node_id);
  return row == materialized_rows_.end() ? nullptr : row->second.get();
}

void SidebarTreeView::SynchronizeRowsForTesting(
    const gfx::Rect& visible_bounds) {
  SynchronizeRows(visible_bounds);
}

std::optional<SidebarTreeView::DropIndicator>
SidebarTreeView::CalculateDropIndicatorForTesting(
    const base::Uuid& source_node_id,
    const gfx::Point& point,
    SidebarTreeController::DropOperation operation) {
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  std::optional<DropIndicator> probe =
      BuildDropProbe(source_node_id, point, operation, visual_rows);
  return probe.has_value() ? CalculateDropIndicator(std::move(*probe))
                           : std::nullopt;
}

void SidebarTreeView::OnRowPressed(SidebarTreeRowView* row,
                                   const ui::MouseEvent& /*event*/,
                                   bool disclosure_hit) {
  CHECK(row);
  pressed_node_id_ = row->node_id();
  if (editing_node_id_.has_value() && editing_node_id_ != row->node_id()) {
    CancelRename();
  }
  RequestFocus();
}

void SidebarTreeView::OnRowReleased(SidebarTreeRowView* row,
                                    const ui::MouseEvent& event,
                                    bool disclosure_hit) {
  CHECK(row);
  const std::optional<base::Uuid> pressed_node_id =
      std::exchange(pressed_node_id_, std::nullopt);
  if (!pressed_node_id.has_value() || !event.IsLeftMouseButton() ||
      *pressed_node_id != row->node_id() ||
      !row->GetLocalBounds().Contains(event.location())) {
    return;
  }
  const tab_tree::TreeNode* node = model().GetNode(*pressed_node_id);
  if ((disclosure_hit ||
       (node && node->type == tab_tree::TreeNodeType::kFolder &&
        event.GetClickCount() == 1)) &&
      node && node->type == tab_tree::TreeNodeType::kFolder) {
    if (!disclosure_hit) {
      std::ignore = controller_->SelectNode(*pressed_node_id);
    }
    // Model updates and row rebinding are deliberately coalesced. A click can
    // therefore arrive while the recycled row still reflects the previous
    // frame. The model is authoritative for the toggle direction.
    if (model().IsExpanded(*pressed_node_id)) {
      std::ignore = controller_->CollapseNode(*pressed_node_id);
    } else {
      const auto result = controller_->ExpandNode(*pressed_node_id);
      if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
        delegate_->OnMutationFailed(result);
      }
    }
    return;
  }
  if (node && node->type == tab_tree::TreeNodeType::kSavedPage &&
      event.GetClickCount() == 1) {
    if (controller_->SelectNode(*pressed_node_id)) {
      ActivateSelectedNode();
    }
  }
}

void SidebarTreeView::OnRowTrailingAction(SidebarTreeRowView* row) {
  CHECK(row && row->is_bound());
  if (delegate_) {
    delegate_->PerformSavedPageTrailingAction(row->node_id());
  }
}

void SidebarTreeView::OnRowHoverChanged(SidebarTreeRowView* row, bool hovered) {
  if (!delegate_ || !row || !row->is_bound()) {
    return;
  }
  if (row->is_folder()) {
    delegate_->OnFolderHoverChanged(row->node_id(), row, hovered);
  } else {
    delegate_->OnSavedPageHoverChanged(row->node_id(), row, hovered);
  }
}

std::vector<gfx::ImageSkia> SidebarTreeView::GetSavedPageDragThumbnailsForNode(
    const base::Uuid& node_id) const {
  return delegate_ ? delegate_->GetSavedPageDragThumbnails(node_id)
                   : std::vector<gfx::ImageSkia>();
}

bool SidebarTreeView::OnRowAccessibilityFocused(SidebarTreeRowView* row) {
  CHECK(row && row->is_bound());
  if (!controller_->SelectNode(row->node_id())) {
    return false;
  }
  RequestFocus();
  return true;
}

bool SidebarTreeView::OnRowAccessibilityActivated(SidebarTreeRowView* row) {
  CHECK(row && row->is_bound());
  if (!controller_->SelectNode(row->node_id())) {
    return false;
  }
  ActivateSelectedNode();
  return true;
}

void SidebarTreeView::CommitRename(const base::Uuid& node_id,
                                   std::u16string title) {
  if (!editing_node_id_.has_value() || *editing_node_id_ != node_id) {
    return;
  }
  if (title.empty()) {
    if (delegate_) {
      delegate_->OnMutationFailed(
          tab_tree::TabTreeStore::Result::kInvalidArgument);
    }
    return;
  }
  if (SidebarTreeRowView* row = GetMaterializedRowForTesting(node_id)) {
    row->StopEditing(/*restore_model_title=*/false);
  }
  editing_node_id_.reset();
  RequestFocus();
  const auto result =
      controller_->RenameNode(node_id, std::move(title), base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
    delegate_->OnMutationFailed(result);
  }
}

void SidebarTreeView::CancelRename(const base::Uuid& node_id) {
  if (!editing_node_id_.has_value() || *editing_node_id_ != node_id) {
    return;
  }
  if (SidebarTreeRowView* row = GetMaterializedRowForTesting(node_id)) {
    row->StopEditing(/*restore_model_title=*/true);
  }
  editing_node_id_.reset();
  RequestFocus();
}

void SidebarTreeView::OnRowDragDone() {
  pressed_node_id_.reset();
  CancelFolderAutoExpand();
  if (delegate_) {
    delegate_->OnSidebarDragStateChanged(std::nullopt);
  }
}

void SidebarTreeView::OnSplitGroupsChanged() {
  last_drop_probe_.reset();
  SetDropIndicator(std::nullopt);
  HandleVisualLayoutChanged();
  ScheduleSynchronization(/*preferred_size_changed=*/true);
  SchedulePaint();
}

void SidebarTreeView::OnRuntimePresentationChanged() {
  ScheduleSynchronization(/*preferred_size_changed=*/false);
}

void SidebarTreeView::Layout(PassKey) {
  SynchronizeRows(GetVisibleBounds());
}

gfx::Size SidebarTreeView::CalculatePreferredSize(
    const views::SizeBounds& /*available_size*/) const {
  const int visual_height = GetVisualRowsHeight(BuildVisualRows());
  // Keep an empty workspace as a real drop surface. A zero-height tree means
  // Views never routes the native drag into the saved section, so the first
  // temporary tab cannot be pinned without creating a folder first.
  int height = std::max(visual_height, SidebarTreeRowView::kRowHeight);
  if (preferred_height_animation_active_) {
    const double value = preferred_height_animation_.GetCurrentValue();
    height =
        animated_height_from_ +
        static_cast<int>((animated_height_to_ - animated_height_from_) * value);
  }
  // The host owns the sidebar width. Advertising the design-time default here
  // makes ScrollView keep a wider contents layer after the native resize strip
  // has narrowed the sidebar, so labels are clipped instead of being laid out
  // against the new viewport. A zero preferred width lets ScrollView stretch
  // the virtualized tree to the live viewport while preserving the exact row
  // height and the host's independent default/min/max width contract.
  return gfx::Size(0, height);
}

bool SidebarTreeView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if ((event.IsControlDown() || event.IsCommandDown()) &&
      event.key_code() == ui::VKEY_Z) {
    const auto result = controller_->UndoLastMutation();
    if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
      delegate_->OnMutationFailed(result);
    }
    return true;
  }
  const bool selected_node_suppressed =
      model().selected_node_id().has_value() &&
      runtime_composite_suppressed_nodes_.contains(*model().selected_node_id());

  switch (event.key_code()) {
    case ui::VKEY_UP:
      SelectRelativeRow(-1);
      return true;
    case ui::VKEY_DOWN:
      SelectRelativeRow(1);
      return true;
    case ui::VKEY_HOME:
      for (size_t index = 0; index < model().rows().size(); ++index) {
        if (!runtime_composite_suppressed_nodes_.contains(
                model().rows()[index].node_id)) {
          SelectRow(index);
          break;
        }
      }
      return true;
    case ui::VKEY_END:
      for (size_t index = model().rows().size(); index > 0; --index) {
        if (!runtime_composite_suppressed_nodes_.contains(
                model().rows()[index - 1].node_id)) {
          SelectRow(index - 1);
          break;
        }
      }
      return true;
    case ui::VKEY_LEFT:
      if (base::i18n::IsRTL()) {
        ExpandOrSelectChild();
      } else {
        CollapseOrSelectParent();
      }
      return true;
    case ui::VKEY_RIGHT:
      if (base::i18n::IsRTL()) {
        CollapseOrSelectParent();
      } else {
        ExpandOrSelectChild();
      }
      return true;
    case ui::VKEY_RETURN:
      if (!selected_node_suppressed) {
        ActivateSelectedNode();
      }
      return true;
    case ui::VKEY_F2:
      if (!selected_node_suppressed) {
        BeginRenameSelectedNode();
      }
      return true;
    case ui::VKEY_BACK:
    case ui::VKEY_DELETE:
      if (model().selected_node_id().has_value() && !selected_node_suppressed) {
        const auto result = controller_->DeleteNode(*model().selected_node_id(),
                                                    base::Time::Now());
        if (result != tab_tree::TabTreeStore::Result::kOk && delegate_) {
          delegate_->OnMutationFailed(result);
        }
      }
      return true;
    default:
      return false;
  }
}

bool SidebarTreeView::GetNeedsNotificationWhenVisibleBoundsChange() const {
  return true;
}

void SidebarTreeView::OnVisibleBoundsChanged() {
  // View::SetBoundsRect() notifies registered descendants by iterating a
  // raw-pointer vector owned by each ancestor. SynchronizeRows() can recycle
  // rows, and every row owns a Textfield that unregisters itself from those
  // same vectors. Doing that synchronously invalidates Chromium's active
  // iterator and can leave a null entry behind while the sidebar collapses.
  // Coalesce the virtualized-row update onto the next UI task so the Views
  // notification pass always finishes before the child hierarchy changes.
  if (visible_bounds_synchronization_pending_) {
    return;
  }
  visible_bounds_synchronization_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&SidebarTreeView::SynchronizeRowsAfterVisibleBoundsChange,
                     weak_ptr_factory_.GetWeakPtr()));
}

void SidebarTreeView::SynchronizeRowsAfterVisibleBoundsChange() {
  visible_bounds_synchronization_pending_ = false;
  SynchronizeRows(GetVisibleBounds());
}

void SidebarTreeView::OnBoundsChanged(const gfx::Rect& /*previous_bounds*/) {
  SynchronizeRows(GetVisibleBounds());
  UpdateInsertionMarker();
}

void SidebarTreeView::SetDragTargetVisible(bool visible) {
  const bool changed = drag_target_visible_ != visible;
  drag_target_visible_ = visible;
  if (!visible) {
    ClearDropTargetPresentation();
  }
  if (changed) {
    SchedulePaint();
  }
}

void SidebarTreeView::SetRuntimeCompositeSuppressedNodes(
    std::set<base::Uuid> node_ids) {
  if (model().selected_node_id().has_value() &&
      node_ids.contains(*model().selected_node_id())) {
    std::ignore = controller_->SelectNode(std::nullopt);
  }
  if (runtime_composite_suppressed_nodes_ == node_ids) {
    return;
  }
  runtime_composite_suppressed_nodes_ = std::move(node_ids);
  OnSplitGroupsChanged();
}

void SidebarTreeView::OnPaintBackground(gfx::Canvas* canvas) {
  const std::vector<VisualRow> visual_rows = BuildVisualRows();
  const std::vector<VisualPosition> visual_positions =
      BuildVisualPositions(visual_rows);
  const std::vector<SidebarTreeViewModel::Row>& rows = model().rows();
  const int row_width = std::max(width(), 1);
  const ui::ColorProvider* colors = GetColorProvider();

  // A concrete saved-row target paints its own exact, validated zone. Painting
  // the complete section at the same time creates two competing highlights
  // and makes a pointer transition look like two accepted targets. The broad
  // surface is needed only for an empty workspace, where no row can own it.
  const bool empty_root_accepting =
      rows.empty() && drop_indicator_.has_value() &&
      !drop_indicator_->target_node_id.has_value();
  if (empty_root_accepting) {
    gfx::RectF target(GetLocalBounds());
    target.Inset(gfx::InsetsF(visual_style::kSidebarDropTargetInset));
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(colors->GetColor(visual_style::kDropTargetSurface));
    canvas->DrawRoundRect(target, visual_style::kRowCornerRadius, fill);
    cc::PaintFlags outline;
    outline.setAntiAlias(true);
    outline.setStyle(cc::PaintFlags::kStroke_Style);
    outline.setStrokeWidth(
        visual_style::kSidebarDropTargetAcceptingOutlineThickness);
    outline.setColor(colors->GetColor(visual_style::kAccent));
    canvas->DrawRoundRect(target, visual_style::kRowCornerRadius, outline);
  }

  // A colored folder owns one quiet visual bubble through all of its visible
  // descendants. Parent bubbles are painted first so nested folders retain a
  // clear hierarchy without adding extra indentation or separate card views.
  for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const SidebarTreeViewModel::Row& row = rows[row_index];
    const tab_tree::TreeNode* node = model().GetNode(row.node_id);
    if (!node || node->type != tab_tree::TreeNodeType::kFolder ||
        !node->accent_argb.has_value()) {
      continue;
    }
    size_t last_row_index = row_index;
    while (last_row_index + 1 < rows.size() &&
           rows[last_row_index + 1].depth > row.depth) {
      ++last_row_index;
    }
    size_t first_visible_index = row_index;
    while (first_visible_index <= last_row_index &&
           !visual_positions[first_visible_index].present) {
      ++first_visible_index;
    }
    if (first_visible_index > last_row_index) {
      continue;
    }
    size_t last_visible_index = last_row_index;
    while (last_visible_index > first_visible_index &&
           !visual_positions[last_visible_index].present) {
      --last_visible_index;
    }
    const size_t first_visual =
        visual_positions[first_visible_index].visual_row;
    const size_t last_visual = visual_positions[last_visible_index].visual_row;
    const int bubble_x = std::min(
        base::saturated_cast<int>(row.depth) * SidebarTreeRowView::kIndentWidth,
        std::max(row_width - 8, 0));
    gfx::RectF bubble(static_cast<float>(bubble_x + 2),
                      static_cast<float>(visual_rows[first_visual].y + 1),
                      static_cast<float>(std::max(row_width - bubble_x - 4, 1)),
                      static_cast<float>(visual_rows[last_visual].y +
                                         visual_rows[last_visual].height -
                                         visual_rows[first_visual].y - 2));
    cc::PaintFlags accent_fill;
    accent_fill.setAntiAlias(true);
    accent_fill.setStyle(cc::PaintFlags::kFill_Style);
    accent_fill.setColor(
        SkColorSetA(static_cast<SkColor>(*node->accent_argb), 34));
    canvas->DrawRoundRect(bubble, visual_style::kControlCornerRadius,
                          accent_fill);
  }

  cc::PaintFlags group_fill;
  group_fill.setAntiAlias(true);
  group_fill.setColor(colors->GetColor(visual_style::kRaisedSurface));
  group_fill.setStyle(cc::PaintFlags::kFill_Style);
  cc::PaintFlags separator;
  separator.setAntiAlias(true);
  separator.setColor(colors->GetColor(visual_style::kDivider));
  separator.setStrokeWidth(1.0f);
  separator.setStyle(cc::PaintFlags::kStroke_Style);

  for (size_t visual_index = 0; visual_index < visual_rows.size();
       ++visual_index) {
    const VisualRow& visual_row = visual_rows[visual_index];
    if (visual_row.model_indices.size() < 2) {
      continue;
    }
    std::vector<gfx::Rect> segment_bounds;
    segment_bounds.reserve(visual_row.model_indices.size());
    gfx::Rect group_bounds;
    for (size_t segment_index = 0;
         segment_index < visual_row.model_indices.size(); ++segment_index) {
      segment_bounds.push_back(
          GetSegmentBounds(visual_row, segment_index, row_width));
      group_bounds.Union(segment_bounds.back());
    }
    gfx::RectF background(group_bounds);
    background.Inset(gfx::InsetsF::VH(2.0f, 4.0f));
    canvas->DrawRoundRect(background, visual_style::kRowCornerRadius,
                          group_fill);
    gfx::RectF outline_bounds = background;
    outline_bounds.Inset(separator.getStrokeWidth() / 2.0f);
    canvas->DrawRoundRect(outline_bounds,
                          std::max(0.0f, visual_style::kRowCornerRadius -
                                             separator.getStrokeWidth() / 2.0f),
                          separator);

    // Derive separators from actual pane adjacency rather than pane order.
    // Inset by half the stroke as well as the painted background margin so no
    // anti-aliased endpoint protrudes above, below or beside the group bubble.
    gfx::RectF separator_bounds = outline_bounds;
    for (const SidebarSplitSeparator& split_separator :
         GetSidebarSplitSeparators(segment_bounds, separator_bounds)) {
      canvas->DrawLine(split_separator.start, split_separator.end, separator);
    }
  }
}

gfx::Point SidebarTreeView::GetKeyboardContextMenuLocation() {
  if (model().selected_node_id().has_value()) {
    SidebarTreeRowView* row =
        GetMaterializedRowForTesting(*model().selected_node_id());
    if (row) {
      return row->GetBoundsInScreen().CenterPoint();
    }
  }
  return views::View::GetKeyboardContextMenuLocation();
}

bool SidebarTreeView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::PICKLED_DATA;
  format_types->insert(drag::SavedSidebarTabDragFormat());
  format_types->insert(drag::RuntimeSidebarTabDragFormat());
  return true;
}

BEGIN_METADATA(SidebarTreeView)
END_METADATA

}  // namespace ahoi::sidebar
