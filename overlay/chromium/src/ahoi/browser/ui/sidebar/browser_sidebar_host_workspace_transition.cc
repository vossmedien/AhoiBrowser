// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
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
  const std::vector<SwitcherWorkspace> switcher = SwitcherWorkspaces();
  if (index >= switcher.size()) {
    return false;
  }
  if (!switcher[index].own) {
    return ActivateSwitcherWorkspace(switcher[index],
                                     WorkspaceActivationSource::kKeyboard);
  }
  const auto& workspaces = workspace_service_->ordered_workspaces();
  const auto target = std::ranges::find_if(workspaces, [&](const auto& item) {
    return item.id == switcher[index].key.workspace_id;
  });
  if (target == workspaces.end()) {
    return false;
  }
  const auto active = session_bridge_->GetActiveWorkspaceForWindow(browser_);
  const auto current = std::ranges::find_if(
      workspaces, [&](const auto& item) { return active == item.id; });
  if (current == workspaces.end()) {
    // No outgoing workspace exists during initial restoration.
    return session_bridge_->SetActiveWorkspaceForWindow(
        browser_, target->id, WorkspaceActivationSource::kKeyboard);
  }
  const int delta = base::checked_cast<int>(target - workspaces.begin()) -
                    base::checked_cast<int>(current - workspaces.begin());
  return delta == 0 || ActivateRelativeWorkspaceWithTransition(
                           delta, WorkspaceActivationSource::kKeyboard);
}

bool BrowserSidebarHostView::ActivateWorkspaceById(
    const base::Uuid& workspace_id) {
  const std::vector<SwitcherWorkspace> switcher = SwitcherWorkspaces();
  for (size_t index = 0; index < switcher.size(); ++index) {
    if (switcher[index].key.workspace_id == workspace_id) {
      return ActivateWorkspaceAtIndex(index);
    }
  }
  return false;
}

bool BrowserSidebarHostView::ActivateRelativeWorkspace(int delta) {
  return ActivateRelativeSwitcherWorkspace(
      delta, WorkspaceActivationSource::kKeyboard);
}

bool BrowserSidebarHostView::ActivateRelativeWorkspaceByGesture(int delta) {
  return ActivateRelativeSwitcherWorkspace(
      delta, WorkspaceActivationSource::kGesture);
}

// Keyboard and swipe cycle through the shared switcher with wrap-around, as
// WorkspaceService::ActivateRelative does inside one Profile. A neighbour in
// the same Profile keeps the animated in-window transition; one of another
// Profile hands the frame over.
bool BrowserSidebarHostView::ActivateRelativeSwitcherWorkspace(
    int delta,
    WorkspaceActivationSource source) {
  const std::vector<SwitcherWorkspace> switcher = SwitcherWorkspaces();
  const std::optional<size_t> current = ActiveSwitcherIndex(switcher);
  if (delta == 0 || switcher.empty() || !current.has_value()) {
    return ActivateRelativeWorkspaceWithTransition(delta, source);
  }
  const int64_t count = static_cast<int64_t>(switcher.size());
  const size_t next = static_cast<size_t>(
      (((static_cast<int64_t>(*current) + delta) % count) + count) % count);
  const SwitcherWorkspace& target = switcher[next];
  if (!target.own) {
    return ActivateSwitcherWorkspace(target, source);
  }
  const auto& workspaces = workspace_service_->ordered_workspaces();
  const auto from = std::ranges::find_if(workspaces, [&](const auto& item) {
    return item.id == switcher[*current].key.workspace_id;
  });
  const auto to = std::ranges::find_if(workspaces, [&](const auto& item) {
    return item.id == target.key.workspace_id;
  });
  if (from == workspaces.end() || to == workspaces.end()) {
    return false;
  }
  const int own_delta = base::checked_cast<int>(to - workspaces.begin()) -
                        base::checked_cast<int>(from - workspaces.begin());
  return own_delta == 0 ||
         ActivateRelativeWorkspaceWithTransition(own_delta, source);
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
  views::View* const contents =
      BrowserView::GetBrowserViewForBrowser(browser_.get())
          ->contents_container();
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
