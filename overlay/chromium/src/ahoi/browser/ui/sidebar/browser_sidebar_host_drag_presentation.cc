// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_preview_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"

namespace ahoi::sidebar {

void BrowserSidebarHostView::BeginSplitPaneDrag(
    const drag::SidebarTabDragPayload& payload) {
  if (!sidebar_discovery_query_.empty() || !payload.is_valid()) {
    ResetDragPresentation();
    return;
  }
  if (payload.saved_node_id.has_value()) {
    OnSidebarDragStateChanged(*payload.saved_node_id);
    return;
  }
  OnTemporaryTabDragStateChanged(*payload.runtime_tab_handle);
}

void BrowserSidebarHostView::CancelSplitDropDrag() {
  ResetDragPresentation();
}

void BrowserSidebarHostView::OnSidebarDragStateChanged(
    std::optional<base::Uuid> dragged_node_id) {
  if (dragged_node_id.has_value() && tab_preview_controller_) {
    tab_preview_controller_->Hide();
  }
  const bool source_already_current =
      dragged_node_id_ == dragged_node_id &&
      (!dragged_node_id.has_value() ||
       !dragged_runtime_tab_handle_.has_value());
  if (source_already_current) {
    return;
  }
  ClearDropTargetPresentation();
  dragged_node_id_ = std::move(dragged_node_id);
  if (dragged_node_id_.has_value()) {
    // Exactly one source owns a native drag. Making that invariant explicit
    // prevents a missed completion from the preceding drag from influencing
    // which target rows appear for the new source.
    dragged_runtime_tab_handle_.reset();
  }
  SetBrowserSidebarDragRoutingActive(this, IsSidebarDragActive());
  tree_view_->SetDragTargetVisible(IsSidebarDragActive());
  SetOpenTabsDropTargetAcceptingTab(
      open_tabs_container_,
      dragged_node_id_.has_value() ||
          (dragged_runtime_tab_handle_.has_value() &&
           CanDropOpenTabToTemporary(
               {.saved_node_id = std::nullopt,
                .runtime_tab_handle = dragged_runtime_tab_handle_})));
  const bool has_open_tabs = !open_tabs_container_->children().empty();
  open_tabs_header_->SetVisible(has_open_tabs);
  // The empty flex surface stays mounted before, during and after a drag.
  // Only its background/drop acceptance changes, so saved rows cannot jump.
  open_tabs_container_->SetVisible(true);
  UpdateNewGroupDropTargetVisibility();
  MaybeScheduleDeferredRuntimePresentationRefresh();
}

void BrowserSidebarHostView::OnTemporaryTabDragStateChanged(
    std::optional<int> runtime_tab_handle) {
  if (runtime_tab_handle.has_value() && tab_preview_controller_) {
    tab_preview_controller_->Hide();
  }
  const bool source_already_current =
      dragged_runtime_tab_handle_ == runtime_tab_handle &&
      (!runtime_tab_handle.has_value() || !dragged_node_id_.has_value());
  if (source_already_current) {
    return;
  }
  ClearDropTargetPresentation();
  dragged_runtime_tab_handle_ = runtime_tab_handle;
  if (dragged_runtime_tab_handle_.has_value()) {
    dragged_node_id_.reset();
  }
  SetOpenTabsDropTargetAcceptingTab(
      open_tabs_container_,
      dragged_node_id_.has_value() ||
          (dragged_runtime_tab_handle_.has_value() &&
           CanDropOpenTabToTemporary(
               {.saved_node_id = std::nullopt,
                .runtime_tab_handle = dragged_runtime_tab_handle_})));
  SetBrowserSidebarDragRoutingActive(this, IsSidebarDragActive());
  tree_view_->SetDragTargetVisible(IsSidebarDragActive());
  UpdateNewGroupDropTargetVisibility();
  MaybeScheduleDeferredRuntimePresentationRefresh();
}

void BrowserSidebarHostView::UpdateNewGroupDropTargetVisibility() {
  const bool visible =
      sidebar_discovery_query_.empty() &&
      (dragged_node_id_.has_value() || dragged_runtime_tab_handle_.has_value());
  SetNewGroupDropTargetVisible(new_group_drop_target_, visible);
  new_group_drop_target_->SchedulePaint();
  SchedulePaint();
}

void BrowserSidebarHostView::ClearDropTargetPresentation() {
  ClaimDropTargetPresentation(nullptr);
}

void BrowserSidebarHostView::ClaimDropTargetPresentation(
    views::View* claimant) {
  if (tree_view_ && claimant != tree_view_) {
    tree_view_->ClearDropTargetPresentation();
  }
  if (new_group_drop_target_ && claimant != new_group_drop_target_) {
    ClearNewGroupDropTargetHighlight(new_group_drop_target_);
  }
  if (open_tabs_container_ && claimant != open_tabs_container_) {
    ClearOpenTabsDropTargetHighlight(open_tabs_container_);
  }
  ClearOpenTabRowDropTargetPresentation(open_tabs_container_, claimant);
}

void BrowserSidebarHostView::OnSidebarDropTargetClaimed() {
  // The pointer is owned by the tree surface even when its current row/zone
  // rejects the payload. Keep the tree's raw validation cache intact while
  // clearing every sibling; SetDropIndicator() immediately decides whether
  // the tree itself paints a strong target.
  ClaimDropTargetPresentation(tree_view_);
}

void BrowserSidebarHostView::ResetDragPresentation() {
  const bool had_drag_presentation =
      dragged_node_id_.has_value() || dragged_runtime_tab_handle_.has_value();
  dragged_node_id_.reset();
  dragged_runtime_tab_handle_.reset();
  ClearDropTargetPresentation();
  SetBrowserSidebarDragRoutingActive(this, false);
  tree_view_->SetDragTargetVisible(false);
  SetOpenTabsDropTargetAcceptingTab(open_tabs_container_, false);

  const bool has_open_tabs = !open_tabs_container_->children().empty();
  open_tabs_header_->SetVisible(has_open_tabs);
  open_tabs_container_->SetVisible(true);
  SetNewGroupDropTargetVisible(new_group_drop_target_, false);
  MaybeScheduleDeferredRuntimePresentationRefresh();

  if (!had_drag_presentation) {
    return;
  }
  new_group_drop_target_->SchedulePaint();
  SchedulePaint();
}

}  // namespace ahoi::sidebar
