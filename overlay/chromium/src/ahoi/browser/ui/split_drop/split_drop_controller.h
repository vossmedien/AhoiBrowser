// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_CONTROLLER_H_
#define AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_CONTROLLER_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "ahoi/browser/ui/split_drop/split_drop_intent.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view_tracker.h"

class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace ui {
class OSExchangeData;
}

namespace views {
class View;
}

namespace ahoi::drag {
struct SidebarTabDragPayload;
}

namespace ahoi::split_drop {

class SplitDropOverlayView;

// Per-BrowserView coordinator for tab-to-MultiContents drops. Runtime tabs may
// originate either in the sidebar or from a pane's mini toolbar; saved pages
// are still resolved through BrowserSidebarHost. It owns no Views; tracked
// View references become null during BrowserView teardown.
class SplitDropController {
 public:
  SplitDropController(TabStripModel* tab_strip_model,
                      views::View* browser_sidebar_host,
                      SplitDropOverlayView* overlay_view);
  SplitDropController(const SplitDropController&) = delete;
  SplitDropController& operator=(const SplitDropController&) = delete;
  ~SplitDropController();

  bool CanDrop(const ui::OSExchangeData& data) const;

  // Updates and returns the current intent. `point` and every pane bound are
  // in MultiContents-local coordinates.
  std::optional<DropIntent> UpdateDrag(
      const ui::OSExchangeData& data,
      const gfx::Point& point,
      const std::vector<SplitDropPane>& visible_panes);

  // Re-resolves the source at commit time, opening a closed saved page through
  // BrowserSidebarHost without duplicating an already-running page.
  bool PerformDrop(const ui::OSExchangeData& data,
                   const gfx::Point& point,
                   const std::vector<SplitDropPane>& visible_panes);

  // A target exit is not a native-drag completion on macOS: AppKit reports it
  // whenever the pointer crosses between BrowserView and sidebar targets.
  // Keep sidebar affordances alive and clear only the content overlay here.
  void OnTargetExited();

  // Authoritative completion boundary for drop, cancellation and teardown.
  // Safe and idempotent when the source View has already reported OnDragDone.
  void CompleteDrag();

  // Injects a one-shot failure after `successful_reorders` pane moves. The
  // production reorder path and rollback path continue to use TabStripModel.
  void FailApplyDesiredOrderAfterForTesting(size_t successful_reorders);

 private:
  struct ResolvedSource {
    bool valid = false;
    raw_ptr<tabs::TabInterface> tab = nullptr;
  };

  ResolvedSource ResolveSource(const drag::SidebarTabDragPayload& payload,
                               bool activate_saved_page) const;
  std::optional<SplitDropTabState> SnapshotTab(tabs::TabInterface* tab) const;
  std::optional<DropIntent> BuildIntent(
      const drag::SidebarTabDragPayload& payload,
      tabs::TabInterface* source_tab,
      const gfx::Point& point,
      const std::vector<SplitDropPane>& visible_panes) const;
  tabs::TabInterface* FindTabByHandle(int tab_handle) const;
  std::optional<std::vector<int>> ResolveDesiredHandles(
      int source_tab_handle,
      const std::vector<DropOrderEntry>& desired_order,
      const std::vector<int>& expected_handles) const;
  bool ApplyDesiredOrder(split_tabs::SplitTabId split_id,
                         const std::vector<int>& desired_handles);
  bool ReorderSplitTo(split_tabs::SplitTabId split_id,
                      const std::vector<int>& desired_handles,
                      std::optional<size_t> fail_after_successful_reorders);
  void HideOverlay();

  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  views::ViewTracker browser_sidebar_host_tracker_;
  views::ViewTracker overlay_view_tracker_;
  std::optional<size_t> fail_apply_desired_order_after_for_testing_;
};

}  // namespace ahoi::split_drop

#endif  // AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_CONTROLLER_H_
