// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_controller.h"

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"
#include "ahoi/browser/ui/split_drop/split_drop_overlay_view.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace ahoi::split_drop {

namespace {

struct SplitStateSnapshot {
  split_tabs::SplitTabId split_id;
  split_tabs::SplitTabVisualData visual_data;
  std::vector<int> member_handles;
};

struct DropModelSnapshot {
  int source_handle = -1;
  int source_index = -1;
  std::optional<split_tabs::SplitTabId> source_split_id;
  int target_handle = -1;
  int active_handle = -1;
  std::vector<int> tab_order;
  std::set<split_tabs::SplitTabId> split_ids;
  std::optional<SplitStateSnapshot> target_split;
};

std::vector<int> TabHandles(TabStripModel* tab_strip_model) {
  std::vector<int> handles;
  handles.reserve(tab_strip_model->count());
  for (tabs::TabInterface* tab : *tab_strip_model) {
    if (!tab) {
      return {};
    }
    handles.push_back(tab->GetHandle().raw_value());
  }
  return handles;
}

std::vector<int> SplitHandles(const split_tabs::SplitTabData& split_data) {
  std::vector<int> handles;
  for (tabs::TabInterface* tab : split_data.ListTabs()) {
    if (!tab) {
      return {};
    }
    handles.push_back(tab->GetHandle().raw_value());
  }
  return handles;
}

tabs::TabInterface* FindTabByHandleInModel(TabStripModel* tab_strip_model,
                                           int handle);

bool SplitExtractionMatchesSnapshot(
    TabStripModel* tab_strip_model,
    int source_handle,
    const sidebar::SplitTabExtractionSnapshot& snapshot) {
  if (!tab_strip_model || snapshot.member_handles.size() < 2u) {
    return false;
  }
  tabs::TabInterface* const source =
      FindTabByHandleInModel(tab_strip_model, source_handle);
  if (!source || source->GetSplit().has_value()) {
    return false;
  }

  std::vector<int> expected_remainder;
  expected_remainder.reserve(snapshot.member_handles.size() - 1u);
  for (int handle : snapshot.member_handles) {
    if (handle != source_handle) {
      expected_remainder.push_back(handle);
    }
  }
  if (expected_remainder.size() + 1u != snapshot.member_handles.size()) {
    return false;
  }

  if (expected_remainder.size() == 1u) {
    tabs::TabInterface* const remainder =
        FindTabByHandleInModel(tab_strip_model, expected_remainder.front());
    return remainder && !remainder->GetSplit().has_value() &&
           !tab_strip_model->ContainsSplit(snapshot.split_id);
  }
  if (expected_remainder.size() < 2u ||
      !tab_strip_model->ContainsSplit(snapshot.split_id)) {
    return false;
  }

  const split_tabs::SplitTabData* const remainder_split =
      tab_strip_model->GetSplitData(snapshot.split_id);
  if (!remainder_split || !remainder_split->visual_data() ||
      SplitHandles(*remainder_split) != expected_remainder) {
    return false;
  }
  const split_tabs::SplitTabVisualData expected_visual_data =
      expected_remainder.size() == 3u
          ? split_tabs::SplitTabVisualData::ForThreePane(
                snapshot.visual_data.split_layout(),
                snapshot.visual_data.arrangement())
          : split_tabs::SplitTabVisualData(
                snapshot.visual_data.split_layout());
  return *remainder_split->visual_data() == expected_visual_data;
}

tabs::TabInterface* FindTabByHandleInModel(TabStripModel* tab_strip_model,
                                           int handle) {
  if (!tab_strip_model || handle < 0) {
    return nullptr;
  }
  for (tabs::TabInterface* tab : *tab_strip_model) {
    if (tab && tab->GetHandle().raw_value() == handle) {
      return tab;
    }
  }
  return nullptr;
}

bool RestoreDropModelSnapshot(TabStripModel* tab_strip_model,
                              const DropModelSnapshot& snapshot) {
  if (!tab_strip_model || snapshot.tab_order.empty()) {
    return false;
  }
  std::vector<int> current_handles = TabHandles(tab_strip_model);
  std::vector<int> expected_handles = snapshot.tab_order;
  std::ranges::sort(current_handles);
  std::ranges::sort(expected_handles);
  if (current_handles != expected_handles) {
    return false;
  }

  // Remove only the split created/expanded by this transaction. Existing
  // unrelated splits remain untouched. An existing target split is rebuilt
  // from its complete snapshot so an added source pane cannot leak through.
  for (split_tabs::SplitTabId split_id : tab_strip_model->ListSplits()) {
    const bool is_transaction_split =
        !snapshot.split_ids.contains(split_id) ||
        (snapshot.target_split.has_value() &&
         snapshot.target_split->split_id == split_id);
    if (is_transaction_split && tab_strip_model->ContainsSplit(split_id)) {
      tab_strip_model->RemoveSplit(split_id);
    }
  }

  for (size_t desired_index = 0; desired_index < snapshot.tab_order.size();
       ++desired_index) {
    tabs::TabInterface* const tab = FindTabByHandleInModel(
        tab_strip_model, snapshot.tab_order[desired_index]);
    const int current_index = tab ? tab_strip_model->GetIndexOfTab(tab) : -1;
    if (!tab || current_index < 0) {
      return false;
    }
    if (current_index != static_cast<int>(desired_index)) {
      tab_strip_model->MoveWebContentsAt(current_index,
                                         static_cast<int>(desired_index),
                                         /*select_after_move=*/false);
    }
  }

  if (snapshot.target_split.has_value()) {
    std::vector<int> restore_indices;
    restore_indices.reserve(snapshot.target_split->member_handles.size());
    for (int handle : snapshot.target_split->member_handles) {
      tabs::TabInterface* const member =
          FindTabByHandleInModel(tab_strip_model, handle);
      const int member_index =
          member ? tab_strip_model->GetIndexOfTab(member) : -1;
      if (!member || member_index < 0 || member->GetSplit().has_value()) {
        return false;
      }
      restore_indices.push_back(member_index);
    }
    std::ranges::sort(restore_indices);
    const bool indices_are_contiguous =
        std::ranges::adjacent_find(
            restore_indices,
            [](int left, int right) { return right != left + 1; }) ==
        restore_indices.end();
    if (restore_indices.size() < 2u || restore_indices.size() > 4u ||
        !indices_are_contiguous ||
        tab_strip_model->ContainsSplit(snapshot.target_split->split_id)) {
      return false;
    }
    tab_strip_model->RestoreSplit(snapshot.target_split->split_id,
                                  restore_indices,
                                  snapshot.target_split->visual_data);
  }

  if (snapshot.active_handle >= 0) {
    tabs::TabInterface* const active =
        FindTabByHandleInModel(tab_strip_model, snapshot.active_handle);
    if (!active) {
      return false;
    }
    if (tab_strip_model->GetActiveTab() != active) {
      tab_strip_model->ActivateTab(active);
    }
  }

  tabs::TabInterface* const source =
      FindTabByHandleInModel(tab_strip_model, snapshot.source_handle);
  tabs::TabInterface* const target =
      FindTabByHandleInModel(tab_strip_model, snapshot.target_handle);
  if (!source || !target || TabHandles(tab_strip_model) != snapshot.tab_order ||
      tab_strip_model->ListSplits() != snapshot.split_ids ||
      source->GetSplit() != snapshot.source_split_id ||
      (snapshot.active_handle >= 0 &&
       (!tab_strip_model->GetActiveTab() ||
        tab_strip_model->GetActiveTab()->GetHandle().raw_value() !=
            snapshot.active_handle))) {
    return false;
  }
  if (!snapshot.target_split.has_value()) {
    return !target->GetSplit().has_value();
  }
  const split_tabs::SplitTabData* const restored_split =
      tab_strip_model->GetSplitData(snapshot.target_split->split_id);
  return restored_split && restored_split->visual_data() &&
         SplitHandles(*restored_split) ==
             snapshot.target_split->member_handles &&
         *restored_split->visual_data() == snapshot.target_split->visual_data;
}

}  // namespace

SplitDropController::SplitDropController(TabStripModel* tab_strip_model,
                                         views::View* browser_sidebar_host,
                                         SplitDropOverlayView* overlay_view)
    : tab_strip_model_(tab_strip_model),
      browser_sidebar_host_tracker_(browser_sidebar_host),
      overlay_view_tracker_(overlay_view) {
  CHECK(tab_strip_model_);
}

SplitDropController::~SplitDropController() {
  CompleteDrag();
}

bool SplitDropController::CanDrop(const ui::OSExchangeData& data) const {
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(data);
  if (!payload.has_value()) {
    return false;
  }
  return ResolveSource(*payload, /*activate_saved_page=*/false).valid;
}

std::optional<DropIntent> SplitDropController::UpdateDrag(
    const ui::OSExchangeData& data,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) {
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(data);
  if (!payload.has_value()) {
    EndOverlayPresentation();
    return std::nullopt;
  }
  const ResolvedSource source =
      ResolveSource(*payload, /*activate_saved_page=*/false);
  if (!source.valid) {
    EndOverlayPresentation();
    return std::nullopt;
  }

  if (overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->BeginDragPresentation();
  }
  std::optional<DropIntent> intent =
      BuildIntent(*payload, source.tab.get(), point, visible_panes);
  if (intent.has_value() && overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->SetIntent(*intent);
  } else {
    ClearOverlayIntent();
  }
  return intent;
}

bool SplitDropController::PerformDrop(
    const ui::OSExchangeData& data,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) {
  // Capture the target tab identity before activating a closed saved tab.
  // Activation is reentrant and may replace a WebContents, while the process-
  // local TabHandle remains the authoritative identity within this model.
  const std::optional<size_t> hit = HitTestVisiblePane(point, visible_panes);
  content::WebContents* const initial_target_contents =
      hit.has_value() ? visible_panes[*hit].web_contents : nullptr;
  const int initial_target_index =
      initial_target_contents
          ? tab_strip_model_->GetIndexOfWebContents(initial_target_contents)
          : -1;
  tabs::TabInterface* const initial_target =
      initial_target_index >= 0
          ? tab_strip_model_->GetTabAtIndex(initial_target_index)
          : nullptr;
  const int target_handle =
      initial_target ? initial_target->GetHandle().raw_value() : -1;
  const std::optional<drag::SidebarTabDragPayload> payload =
      drag::ReadSidebarTabDragPayload(data);

  // This callback is an authoritative native completion boundary. Defer
  // presentation cleanup until after every model mutation so refresh gating
  // cannot recycle the drag source halfway through the transaction.
  base::ScopedClosureRunner complete_drag(base::BindOnce(
      &SplitDropController::CompleteDrag, weak_ptr_factory_.GetWeakPtr()));
  if (!payload.has_value() || target_handle < 0) {
    return false;
  }

  const base::WeakPtr<SplitDropController> weak_self =
      weak_ptr_factory_.GetWeakPtr();
  ResolvedSource source = ResolveSource(*payload, /*activate_saved_page=*/true);
  base::ScopedClosureRunner rollback_materialized_source(
      std::move(source.rollback));
  // Saved-page materialization can synchronously activate a window, navigate
  // and rebuild BrowserView. Never continue through a destroyed coordinator.
  if (!weak_self || !source.valid || !source.tab) {
    return false;
  }

  tabs::TabInterface* const current_target = FindTabByHandle(target_handle);
  if (!current_target || !current_target->GetContents()) {
    return false;
  }
  std::vector<SplitDropPane> current_panes = visible_panes;
  current_panes[*hit].web_contents = current_target->GetContents();
  const std::optional<DropIntent> intent =
      BuildIntent(*payload, source.tab.get(), point, current_panes);
  if (!intent.has_value()) {
    return false;
  }

  tabs::TabInterface* target = FindTabByHandle(intent->target_tab_handle);
  const bool detaching = intent->action == DropAction::kDetachFromSplit;
  if (!target || (detaching && source.tab.get() != target) ||
      (!detaching && source.tab.get() == target)) {
    return false;
  }

  if (detaching) {
    TabStripModel* const model = tab_strip_model_;
    const int source_handle = source.tab->GetHandle().raw_value();
    const std::optional<sidebar::SplitTabExtractionSnapshot>
        extraction_snapshot = sidebar::CaptureSplitTabExtractionSnapshot(
            model, source.tab.get());
    if (!extraction_snapshot.has_value() ||
        !sidebar::ExtractTabFromSplitPreservingRemainder(model,
                                                         source.tab.get())) {
      return false;
    }

    // Extraction itself fails only before mutation. Once it succeeds, keep the
    // exact prior split shape armed until all postconditions have been checked
    // using stable handles; any stale or reentrant state restores the original
    // split id, orientation and pane order.
    // TabStripModel is Browser-owned and outlives this BrowserView-owned
    // controller. Keep it directly in the synchronous scope guard so a
    // reentrant BrowserView rebuild cannot disarm rollback.
    base::ScopedClosureRunner rollback_extraction(base::BindOnce(
        [](TabStripModel* tab_strip_model,
           sidebar::SplitTabExtractionSnapshot snapshot) {
          if (!sidebar::RestoreSplitTabExtraction(tab_strip_model, snapshot)) {
            LOG(ERROR) << "Split pane detach failed and its model rollback was "
                          "incomplete";
          }
        },
        base::Unretained(model), *extraction_snapshot));
    if (!SplitExtractionMatchesSnapshot(model, source_handle,
                                        *extraction_snapshot)) {
      return false;
    }
    rollback_extraction.ReplaceClosure(base::OnceClosure());
    rollback_materialized_source.ReplaceClosure(base::OnceClosure());
    return true;
  }
  const base::WeakPtr<tabs::TabInterface> weak_target = target->GetWeakPtr();

  DropModelSnapshot snapshot{
      .source_handle = source.tab->GetHandle().raw_value(),
      .source_index = tab_strip_model_->GetIndexOfTab(source.tab.get()),
      .source_split_id = source.tab->GetSplit(),
      .target_handle = target->GetHandle().raw_value(),
      .active_handle =
          tab_strip_model_->GetActiveTab()
              ? tab_strip_model_->GetActiveTab()->GetHandle().raw_value()
              : -1,
      .tab_order = TabHandles(tab_strip_model_),
      .split_ids = tab_strip_model_->ListSplits(),
  };
  if (snapshot.source_index < 0 || !weak_target) {
    return false;
  }
  if (target->GetSplit().has_value()) {
    const split_tabs::SplitTabId target_split_id = *target->GetSplit();
    if (!tab_strip_model_->ContainsSplit(target_split_id)) {
      return false;
    }
    const split_tabs::SplitTabData* const target_split_data =
        tab_strip_model_->GetSplitData(target_split_id);
    if (!target_split_data || !target_split_data->visual_data()) {
      return false;
    }
    std::vector<int> target_members = SplitHandles(*target_split_data);
    if (target_members.size() < 2u) {
      return false;
    }
    snapshot.target_split = SplitStateSnapshot{
        .split_id = target_split_id,
        .visual_data = *target_split_data->visual_data(),
        .member_handles = std::move(target_members),
    };
  }

  std::vector<int> expected_handles =
      snapshot.target_split.has_value()
          ? snapshot.target_split->member_handles
          : std::vector<int>{snapshot.target_handle};
  if (intent->action == DropAction::kCreateOrAddToSplit) {
    if (source.tab->GetSplit().has_value()) {
      return false;
    }
    expected_handles.push_back(snapshot.source_handle);
  }
  const std::optional<std::vector<int>> desired_handles = ResolveDesiredHandles(
      snapshot.source_handle, intent->desired_order, expected_handles);
  if (!desired_handles.has_value()) {
    return false;
  }

  base::ScopedClosureRunner rollback_model;
  const auto arm_model_rollback = [&]() {
    rollback_model.ReplaceClosure(base::BindOnce(
        [](base::WeakPtr<SplitDropController> controller,
           DropModelSnapshot model_snapshot) {
          if (!controller) {
            return;
          }
          if (!RestoreDropModelSnapshot(controller->tab_strip_model_,
                                        model_snapshot)) {
            LOG(ERROR) << "Split drop failed and its model rollback was "
                          "incomplete";
          }
        },
        weak_self, snapshot));
  };

  if (intent->action == DropAction::kReorderInSplit) {
    if (!source.tab->GetSplit().has_value() ||
        source.tab->GetSplit() != target->GetSplit()) {
      return false;
    }
    const split_tabs::SplitTabId split_id = *source.tab->GetSplit();
    arm_model_rollback();
    tab_strip_model_->UpdateSplitLayout(split_id, intent->layout);
    if (!weak_self || !source.tab || !weak_target) {
      return false;
    }
    tab_strip_model_->UpdateSplitArrangement(split_id, intent->arrangement);
    if (!weak_self || !source.tab || !weak_target) {
      return false;
    }
    const bool order_applied = ApplyDesiredOrder(split_id, *desired_handles);
    if (!weak_self || !order_applied) {
      return false;
    }
    if (!weak_self || !source.tab) {
      return false;
    }
    const int active_source_index =
        tab_strip_model_->GetIndexOfTab(source.tab.get());
    if (active_source_index < 0) {
      return false;
    }
    tab_strip_model_->ActivateTabAt(active_source_index);
    if (!weak_self || !source.tab) {
      return false;
    }
    rollback_model.ReplaceClosure(base::OnceClosure());
    rollback_materialized_source.ReplaceClosure(base::OnceClosure());
    return true;
  }

  const int source_index = tab_strip_model_->GetIndexOfTab(source.tab.get());
  const int target_index = tab_strip_model_->GetIndexOfTab(target);
  if (source_index < 0 || target_index < 0) {
    return false;
  }
  arm_model_rollback();
  std::optional<split_tabs::SplitTabId> split_id =
      tab_strip_model_->CreateOrAddToSplitFromDrop(source_index, target_index,
                                                   intent->arrangement);
  if (!weak_self || !source.tab || !weak_target || !split_id.has_value()) {
    return false;
  }

  tab_strip_model_->UpdateSplitLayout(*split_id, intent->layout);
  if (!weak_self || !source.tab || !weak_target) {
    return false;
  }
  tab_strip_model_->UpdateSplitArrangement(*split_id, intent->arrangement);
  if (!weak_self || !source.tab || !weak_target) {
    return false;
  }
  const bool order_applied = ApplyDesiredOrder(*split_id, *desired_handles);
  if (!weak_self || !order_applied) {
    return false;
  }
  if (!weak_self || !source.tab) {
    return false;
  }
  const int active_source_index =
      tab_strip_model_->GetIndexOfTab(source.tab.get());
  if (active_source_index < 0) {
    return false;
  }
  tab_strip_model_->ActivateTabAt(active_source_index);
  if (!weak_self || !source.tab) {
    return false;
  }
  rollback_model.ReplaceClosure(base::OnceClosure());
  rollback_materialized_source.ReplaceClosure(base::OnceClosure());
  return true;
}

void SplitDropController::OnTargetExited() {
  EndOverlayPresentation();
}

void SplitDropController::CompleteDrag() {
  EndOverlayPresentation();
  if (views::View* const browser_sidebar_host =
          browser_sidebar_host_tracker_.view()) {
    sidebar::CancelBrowserSidebarSplitDropDrag(browser_sidebar_host);
  }
}

void SplitDropController::FailApplyDesiredOrderAfterForTesting(
    size_t successful_reorders) {
  fail_apply_desired_order_after_for_testing_ = successful_reorders;
}

SplitDropController::ResolvedSource SplitDropController::ResolveSource(
    const drag::SidebarTabDragPayload& payload,
    bool activate_saved_page) const {
  if (!payload.is_valid()) {
    return {};
  }
  if (payload.runtime_tab_handle.has_value()) {
    tabs::TabInterface* const tab =
        FindTabByHandle(*payload.runtime_tab_handle);
    return {
        .valid = tab != nullptr,
        .tab = tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()};
  }
  views::View* const browser_sidebar_host =
      const_cast<views::View*>(browser_sidebar_host_tracker_.view());
  if (!browser_sidebar_host) {
    return {};
  }
  sidebar::BrowserSidebarSplitDropSource source =
      sidebar::ResolveBrowserSidebarSplitDropSource(
          browser_sidebar_host, payload, activate_saved_page);
  return {.valid = source.valid,
          .tab = source.tab,
          .rollback = std::move(source.rollback)};
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
    const std::vector<int>& desired_handles) {
  if (!tab_strip_model_->ContainsSplit(split_id)) {
    return false;
  }
  const split_tabs::SplitTabData* const split_data =
      tab_strip_model_->GetSplitData(split_id);
  if (!split_data) {
    return false;
  }
  const std::vector<int> original_handles = SplitHandles(*split_data);
  if (original_handles.size() < 2u || original_handles.size() > 4u) {
    return false;
  }
  if (!ReorderSplitTo(split_id, desired_handles,
                      std::exchange(fail_apply_desired_order_after_for_testing_,
                                    std::nullopt))) {
    if (!ReorderSplitTo(split_id, original_handles, std::nullopt)) {
      LOG(ERROR) << "Split drop ordering failed and its immediate order "
                    "rollback was incomplete";
    }
    return false;
  }
  return true;
}

std::optional<std::vector<int>> SplitDropController::ResolveDesiredHandles(
    int source_tab_handle,
    const std::vector<DropOrderEntry>& desired_order,
    const std::vector<int>& expected_handles) const {
  if (desired_order.size() < 2u || desired_order.size() > 4u ||
      desired_order.size() != expected_handles.size()) {
    return std::nullopt;
  }
  std::vector<int> desired_handles;
  desired_handles.reserve(desired_order.size());
  for (const DropOrderEntry& entry : desired_order) {
    const int handle =
        entry.is_source ? source_tab_handle : entry.existing_tab_handle;
    if (handle < 0 || !FindTabByHandle(handle)) {
      return std::nullopt;
    }
    desired_handles.push_back(handle);
  }
  std::vector<int> sorted_desired = desired_handles;
  std::vector<int> sorted_expected = expected_handles;
  std::ranges::sort(sorted_desired);
  std::ranges::sort(sorted_expected);
  if (sorted_desired != sorted_expected) {
    return std::nullopt;
  }
  return desired_handles;
}

bool SplitDropController::ReorderSplitTo(
    split_tabs::SplitTabId split_id,
    const std::vector<int>& desired_handles,
    std::optional<size_t> fail_after_successful_reorders) {
  if (!tab_strip_model_->ContainsSplit(split_id) ||
      desired_handles.size() < 2u || desired_handles.size() > 4u) {
    return false;
  }
  const split_tabs::SplitTabData* const initial_split_data =
      tab_strip_model_->GetSplitData(split_id);
  if (!initial_split_data) {
    return false;
  }
  const std::vector<int> current_handles = SplitHandles(*initial_split_data);
  std::vector<int> sorted_current = current_handles;
  std::vector<int> sorted_desired = desired_handles;
  std::ranges::sort(sorted_current);
  std::ranges::sort(sorted_desired);
  if (sorted_current != sorted_desired) {
    return false;
  }

  size_t successful_reorders = 0;
  for (size_t index = 0; index < desired_handles.size(); ++index) {
    if (!tab_strip_model_->ContainsSplit(split_id)) {
      return false;
    }
    const split_tabs::SplitTabData* split_data =
        tab_strip_model_->GetSplitData(split_id);
    if (!split_data) {
      return false;
    }
    const std::vector<tabs::TabInterface*> current = split_data->ListTabs();
    if (current.size() != desired_handles.size() || !current[index]) {
      return false;
    }
    if (current[index]->GetHandle().raw_value() == desired_handles[index]) {
      continue;
    }
    if (fail_after_successful_reorders.has_value() &&
        successful_reorders == *fail_after_successful_reorders) {
      return false;
    }
    tabs::TabInterface* desired_tab = FindTabByHandle(desired_handles[index]);
    if (!desired_tab ||
        !tab_strip_model_->ReorderTabInSplit(desired_tab, current[index])) {
      return false;
    }
    ++successful_reorders;
  }
  return true;
}

void SplitDropController::ClearOverlayIntent() {
  if (overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->ClearIntent();
  }
}

void SplitDropController::EndOverlayPresentation() {
  if (overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->EndDragPresentation();
  }
}

}  // namespace ahoi::split_drop
