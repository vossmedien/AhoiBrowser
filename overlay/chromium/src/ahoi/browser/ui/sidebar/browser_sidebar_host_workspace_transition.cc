// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <optional>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/compositor/layer.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

bool BrowserSidebarHostView::ActivateWorkspaceAtIndex(size_t index) {
  const auto& workspaces = workspace_service_->ordered_workspaces();
  if (index >= workspaces.size()) {
    return false;
  }
  const auto active = session_bridge_->GetActiveWorkspaceForWindow(browser_);
  const auto current = std::ranges::find_if(
      workspaces, [&](const auto& item) { return active == item.id; });
  if (current == workspaces.end()) {
    // No outgoing workspace exists during initial restoration.
    return session_bridge_->SetActiveWorkspaceForWindow(
        browser_, workspaces[index].id, WorkspaceActivationSource::kKeyboard);
  }
  const int delta = base::checked_cast<int>(index) -
                    base::checked_cast<int>(current - workspaces.begin());
  return delta == 0 || ActivateRelativeWorkspaceWithTransition(
                           delta, WorkspaceActivationSource::kKeyboard);
}

bool BrowserSidebarHostView::ActivateRelativeWorkspace(int delta) {
  return ActivateRelativeWorkspaceWithTransition(
      delta, WorkspaceActivationSource::kKeyboard);
}

bool BrowserSidebarHostView::ActivateRelativeWorkspaceByGesture(int delta) {
  return ActivateRelativeWorkspaceWithTransition(
      delta, WorkspaceActivationSource::kGesture);
}

bool BrowserSidebarHostView::ActivateRelativeWorkspaceWithTransition(
    int delta,
    WorkspaceActivationSource source) {
  if (delta == 0) {
    return false;
  }

  // Preempt the preceding compositor transition before the domain mutation.
  // The WorkspaceService observer remains the one authoritative
  // identity/tree/tab/WebContents commit; no presentation state leaks into
  // that transaction.
  CancelWorkspaceTransition();
  const std::optional<base::Uuid> previous_workspace =
      session_bridge_->GetActiveWorkspaceForWindow(browser_);
  content::WebContents* const previous_contents =
      tab_strip_model_ ? tab_strip_model_->GetActiveWebContents() : nullptr;
  const std::optional<base::Uuid> activated_workspace =
      session_bridge_->ActivateRelativeWorkspaceForWindow(browser_, delta,
                                                          source);
  if (!activated_workspace.has_value()) {
    return false;
  }
  if (previous_workspace != activated_workspace) {
    // WorkspaceService observers are synchronous. At this point the selector,
    // inactive dots, tree projection, runtime selection, empty state and live
    // WebContents surface all represent `activated_workspace` and enter as one
    // Arc-like surface rather than repainting in separate stages.
    content::WebContents* const activated_contents =
        tab_strip_model_ ? tab_strip_model_->GetActiveWebContents() : nullptr;
    StartWorkspaceTransition(delta, previous_contents != activated_contents);
  }
  return true;
}

void BrowserSidebarHostView::StartWorkspaceTransition(
    int delta,
    bool active_web_contents_changed) {
  if (delta == 0 || !browser_ || !browser_->GetWindow() || !scroll_view_ ||
      !scroll_view_->contents()) {
    return;
  }
  // Animate inside ScrollView's clipped viewport. The host, workspace header,
  // bookmark shelf and fixed footer keep their geometry and focus positions.
  views::View* const sidebar_contents = scroll_view_->contents();
  if (!sidebar_contents->layer()) {
    sidebar_contents->SetPaintToLayer();
    sidebar_contents->layer()->SetFillsBoundsOpaquely(false);
  }
  views::View* const contents = browser_->GetBrowserView().contents_container();
  workspace_transition_animator_.Start(
      sidebar_contents->layer(), contents ? contents->layer() : nullptr,
      delta > 0 ? WorkspaceTransitionDirection::kNext
                : WorkspaceTransitionDirection::kPrevious,
      active_web_contents_changed, reduced_motion_);
}

void BrowserSidebarHostView::CancelWorkspaceTransition() {
  workspace_transition_animator_.Cancel();
}

}  // namespace ahoi::sidebar
