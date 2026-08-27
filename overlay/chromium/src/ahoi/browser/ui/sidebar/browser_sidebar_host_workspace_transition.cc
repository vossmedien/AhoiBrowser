// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

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
    StartWorkspaceTransition(delta);
  }
  return true;
}

void BrowserSidebarHostView::StartWorkspaceTransition(int delta) {
  if (delta == 0 || !browser_ || !browser_->GetWindow() || !layer()) {
    return;
  }
  views::View* const contents = browser_->GetBrowserView().contents_container();
  if (!contents || !contents->layer()) {
    return;
  }
  workspace_transition_animator_.Start(
      layer(), contents->layer(),
      delta > 0 ? WorkspaceTransitionDirection::kNext
                : WorkspaceTransitionDirection::kPrevious,
      reduced_motion_);
}

void BrowserSidebarHostView::CancelWorkspaceTransition() {
  workspace_transition_animator_.Cancel();
}

}  // namespace ahoi::sidebar
