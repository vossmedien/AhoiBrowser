// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_INTENT_H_
#define AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_INTENT_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "base/memory/raw_ptr.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

namespace ahoi::split_drop {

enum class DropZone { kLeft, kRight, kTop, kBottom };
enum class DropAction {
  kReorderInSplit,
  kCreateOrAddToSplit,
  kDetachFromSplit,
};

// BrowserView supplies these in MultiContents-local coordinates. Hidden panes
// must not be included.
struct SplitDropPane {
  size_t pane_index = 0;
  gfx::Rect bounds;
  raw_ptr<content::WebContents> web_contents = nullptr;
};

// A stable snapshot used by the UI-independent intent mapper. `split_order`
// is the current visual pane order and contains `tab_handle`.
struct SplitDropTabState {
  int tab_handle = -1;
  std::optional<split_tabs::SplitTabId> split_id;
  std::vector<int> split_order;
  split_tabs::SplitTabLayout split_layout =
      split_tabs::SplitTabLayout::kSideBySide;
};

struct DropOrderEntry {
  bool is_source = false;
  int existing_tab_handle = -1;

  static DropOrderEntry Source();
  static DropOrderEntry Existing(int tab_handle);

  bool operator==(const DropOrderEntry&) const = default;
};

struct DropIntent {
  drag::SidebarTabDragPayload source;
  DropAction action = DropAction::kCreateOrAddToSplit;
  DropZone zone = DropZone::kRight;
  int target_tab_handle = -1;
  size_t target_pane_index = 0;
  split_tabs::SplitTabLayout layout = split_tabs::SplitTabLayout::kSideBySide;
  split_tabs::SplitTabArrangement arrangement =
      split_tabs::SplitTabArrangement::kLinear;
  std::vector<DropOrderEntry> desired_order;
  gfx::Rect highlight_bounds;

  bool operator==(const DropIntent&) const = default;
};

std::optional<size_t> HitTestVisiblePane(
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes);

// Returns no zone for the neutral center. `retained_zone` is used only while
// the pointer remains inside that zone's slightly larger hysteresis surface;
// it prevents adjacent edge targets from flickering around their boundary.
std::optional<DropZone> ClassifyDropZone(
    const gfx::Point& point,
    const gfx::Rect& pane_bounds,
    std::optional<DropZone> retained_zone = std::nullopt);

// A null source state is valid only for a closed saved page. Runtime payloads
// always require a live source. A source belonging to a different split is
// intentionally rejected because Chromium's add-from-drop API accepts only an
// unsplit source.
std::optional<DropIntent> CalculateDropIntent(
    const drag::SidebarTabDragPayload& payload,
    const std::optional<SplitDropTabState>& source_state,
    const SplitDropTabState& target_state,
    size_t target_pane_index,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes,
    std::optional<DropZone> retained_zone = std::nullopt);

}  // namespace ahoi::split_drop

#endif  // AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_INTENT_H_
