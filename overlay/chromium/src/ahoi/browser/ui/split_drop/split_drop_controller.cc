// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_controller.h"

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/split_drop/split_drop_overlay_view.h"
#include "base/check.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace ahoi::split_drop {

SplitDropController::SplitDropController(TabStripModel* tab_strip_model,
                                         views::View* browser_sidebar_host,
                                         SplitDropOverlayView* overlay_view)
    : tab_strip_model_(tab_strip_model),
      browser_sidebar_host_tracker_(browser_sidebar_host),
      overlay_view_tracker_(overlay_view) {
  CHECK(tab_strip_model_);
}

SplitDropController::~SplitDropController() {
  CancelDrag();
}

bool SplitDropController::CanDrop(const ui::OSExchangeData& data) const {
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(data);
  views::View* const browser_sidebar_host =
      const_cast<views::View*>(browser_sidebar_host_tracker_.view());
  if (!payload.has_value() || !browser_sidebar_host) {
    return false;
  }
  return sidebar::ResolveBrowserSidebarSplitDropSource(
             browser_sidebar_host, *payload,
             /*activate_saved_page=*/false)
      .valid;
}

std::optional<DropIntent> SplitDropController::UpdateDrag(
    const ui::OSExchangeData& data,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) {
  HideOverlay();
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(data);
  views::View* const browser_sidebar_host =
      browser_sidebar_host_tracker_.view();
  if (!payload.has_value() || !browser_sidebar_host) {
    return std::nullopt;
  }
  const sidebar::BrowserSidebarSplitDropSource source =
      sidebar::ResolveBrowserSidebarSplitDropSource(
          browser_sidebar_host, *payload,
          /*activate_saved_page=*/false);
  if (!source.valid) {
    return std::nullopt;
  }

  std::optional<DropIntent> intent =
      BuildIntent(*payload, source.tab, point, visible_panes);
  if (intent.has_value() && overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->SetIntent(*intent);
  }
  return intent;
}

bool SplitDropController::PerformDrop(
    const ui::OSExchangeData& data,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) {
  // Capture the target renderer identity before activating a closed saved tab;
  // activation can change which pane is focused but does not invalidate the
  // target WebContents or its TabInterface.
  const std::optional<size_t> hit = HitTestVisiblePane(point, visible_panes);
  content::WebContents* target_contents =
      hit.has_value() ? visible_panes[*hit].web_contents : nullptr;
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(data);

  // From this point every return path is visually clean. The payload remains
  // self-contained and can still be resolved after source-row presentation is
  // cleared.
  CancelDrag();
  views::View* const browser_sidebar_host =
      browser_sidebar_host_tracker_.view();
  if (!payload.has_value() || !browser_sidebar_host || !target_contents) {
    return false;
  }

  const sidebar::BrowserSidebarSplitDropSource source =
      sidebar::ResolveBrowserSidebarSplitDropSource(
          browser_sidebar_host, *payload,
          /*activate_saved_page=*/true);
  if (!source.valid || !source.tab) {
    return false;
  }

  std::vector<SplitDropPane> current_panes = visible_panes;
  current_panes[*hit].web_contents = target_contents;
  const std::optional<DropIntent> intent =
      BuildIntent(*payload, source.tab, point, current_panes);
  if (!intent.has_value()) {
    return false;
  }

  tabs::TabInterface* target = FindTabByHandle(intent->target_tab_handle);
  if (!target || source.tab == target) {
    return false;
  }

  if (intent->action == DropAction::kReorderInSplit) {
    if (!source.tab->GetSplit().has_value() ||
        source.tab->GetSplit() != target->GetSplit()) {
      return false;
    }
    const split_tabs::SplitTabId split_id = *source.tab->GetSplit();
    tab_strip_model_->UpdateSplitLayout(split_id, intent->layout);
    tab_strip_model_->UpdateSplitArrangement(split_id, intent->arrangement);
    if (!ApplyDesiredOrder(split_id, source.tab->GetHandle().raw_value(),
                           intent->desired_order)) {
      return false;
    }
    tab_strip_model_->ActivateTabAt(
        tab_strip_model_->GetIndexOfTab(source.tab));
    return true;
  }

  const int source_index = tab_strip_model_->GetIndexOfTab(source.tab);
  const int target_index = tab_strip_model_->GetIndexOfTab(target);
  std::optional<split_tabs::SplitTabId> split_id =
      tab_strip_model_->CreateOrAddToSplitFromDrop(source_index, target_index,
                                                   intent->arrangement);
  if (!split_id.has_value()) {
    return false;
  }

  tab_strip_model_->UpdateSplitLayout(*split_id, intent->layout);
  tab_strip_model_->UpdateSplitArrangement(*split_id, intent->arrangement);
  if (!ApplyDesiredOrder(*split_id, source.tab->GetHandle().raw_value(),
                         intent->desired_order)) {
    return false;
  }
  tab_strip_model_->ActivateTabAt(tab_strip_model_->GetIndexOfTab(source.tab));
  return true;
}

void SplitDropController::CancelDrag() {
  HideOverlay();
  if (views::View* const browser_sidebar_host =
          browser_sidebar_host_tracker_.view()) {
    sidebar::CancelBrowserSidebarSplitDropDrag(browser_sidebar_host);
  }
}

std::optional<SplitDropTabState> SplitDropController::SnapshotTab(
    tabs::TabInterface* tab) const {
  if (!tab_strip_model_ || !tab || tab_strip_model_->GetIndexOfTab(tab) < 0) {
    return std::nullopt;
  }
  SplitDropTabState state{.tab_handle = tab->GetHandle().raw_value()};
  if (!tab->GetSplit().has_value()) {
    state.split_order.push_back(state.tab_handle);
    return state;
  }
  const split_tabs::SplitTabData* split_data =
      tab_strip_model_->GetSplitData(*tab->GetSplit());
  if (!split_data || !split_data->visual_data()) {
    return std::nullopt;
  }
  state.split_id = tab->GetSplit();
  state.split_layout = split_data->visual_data()->split_layout();
  for (tabs::TabInterface* split_tab : split_data->ListTabs()) {
    if (!split_tab) {
      return std::nullopt;
    }
    state.split_order.push_back(split_tab->GetHandle().raw_value());
  }
  return state;
}

std::optional<DropIntent> SplitDropController::BuildIntent(
    const drag::SidebarTabDragPayload& payload,
    tabs::TabInterface* source_tab,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) const {
  const std::optional<size_t> hit = HitTestVisiblePane(point, visible_panes);
  if (!hit.has_value() || !visible_panes[*hit].web_contents) {
    return std::nullopt;
  }
  const int target_index =
      tab_strip_model_->GetIndexOfWebContents(visible_panes[*hit].web_contents);
  if (target_index < 0) {
    return std::nullopt;
  }
  tabs::TabInterface* target_tab =
      tab_strip_model_->GetTabAtIndex(target_index);
  const std::optional<SplitDropTabState> target_state = SnapshotTab(target_tab);
  if (!target_state.has_value()) {
    return std::nullopt;
  }
  const std::optional<SplitDropTabState> source_state = SnapshotTab(source_tab);
  if (source_tab && !source_state.has_value()) {
    // The saved identity is already live in a different browser model. Reuse
    // that tab rather than opening a duplicate, but do not pretend Chromium's
    // same-model split API can move it across windows.
    return std::nullopt;
  }
  return CalculateDropIntent(payload, source_state, *target_state,
                             visible_panes[*hit].pane_index, point,
                             visible_panes);
}

tabs::TabInterface* SplitDropController::FindTabByHandle(int tab_handle) const {
  if (!tab_strip_model_ || tab_handle < 0) {
    return nullptr;
  }
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (tab && tab->GetHandle().raw_value() == tab_handle) {
      return tab;
    }
  }
  return nullptr;
}

bool SplitDropController::ApplyDesiredOrder(
    split_tabs::SplitTabId split_id,
    int source_tab_handle,
    const std::vector<DropOrderEntry>& desired_order) {
  if (desired_order.size() < 2u || desired_order.size() > 4u) {
    return false;
  }
  std::vector<int> desired_handles;
  desired_handles.reserve(desired_order.size());
  for (const DropOrderEntry& entry : desired_order) {
    desired_handles.push_back(entry.is_source ? source_tab_handle
                                              : entry.existing_tab_handle);
  }

  for (size_t index = 0; index < desired_handles.size(); ++index) {
    const split_tabs::SplitTabData* split_data =
        tab_strip_model_->GetSplitData(split_id);
    if (!split_data) {
      return false;
    }
    const std::vector<tabs::TabInterface*> current = split_data->ListTabs();
    if (current.size() != desired_handles.size()) {
      return false;
    }
    if (current[index]->GetHandle().raw_value() == desired_handles[index]) {
      continue;
    }
    tabs::TabInterface* desired_tab = FindTabByHandle(desired_handles[index]);
    if (!desired_tab ||
        !tab_strip_model_->ReorderTabInSplit(desired_tab, current[index])) {
      return false;
    }
  }
  return true;
}

void SplitDropController::HideOverlay() {
  if (overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->ClearIntent();
  }
}

}  // namespace ahoi::split_drop
