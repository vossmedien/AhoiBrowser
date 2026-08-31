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

ui::mojom::DragOperation ToNativeDragOperation(
    SidebarTreeController::DropOperation operation) {
  return operation == SidebarTreeController::DropOperation::kCopy
             ? ui::mojom::DragOperation::kCopy
             : ui::mojom::DragOperation::kMove;
}

}  // namespace

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
