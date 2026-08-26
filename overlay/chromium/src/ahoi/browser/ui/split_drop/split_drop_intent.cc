// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_intent.h"

#include <algorithm>
#include <array>
#include <utility>

namespace ahoi::split_drop {

namespace {

constexpr size_t kMaximumPaneCount = 4;

bool IsLeadingZone(DropZone zone) {
  return zone == DropZone::kLeft || zone == DropZone::kTop;
}

split_tabs::SplitTabLayout LayoutForZone(DropZone zone) {
  return zone == DropZone::kLeft || zone == DropZone::kRight
             ? split_tabs::SplitTabLayout::kSideBySide
             : split_tabs::SplitTabLayout::kStacked;
}

gfx::Rect HighlightForZone(const gfx::Rect& pane, DropZone zone) {
  gfx::Rect highlight = pane;
  constexpr double kEdgeFraction = 0.42;
  if (zone == DropZone::kLeft || zone == DropZone::kRight) {
    const int width =
        std::max(1, static_cast<int>(pane.width() * kEdgeFraction));
    highlight.set_width(width);
    if (zone == DropZone::kRight) {
      highlight.set_x(pane.right() - width);
    }
  } else {
    const int height =
        std::max(1, static_cast<int>(pane.height() * kEdgeFraction));
    highlight.set_height(height);
    if (zone == DropZone::kBottom) {
      highlight.set_y(pane.bottom() - height);
    }
  }
  highlight.Inset(4);
  return highlight;
}

std::vector<int> NormalizedTargetOrder(const SplitDropTabState& target) {
  if (!target.split_order.empty()) {
    return target.split_order;
  }
  return target.tab_handle >= 0 ? std::vector<int>{target.tab_handle}
                                : std::vector<int>();
}

std::vector<DropOrderEntry> ToEntries(const std::vector<int>& handles) {
  std::vector<DropOrderEntry> entries;
  entries.reserve(handles.size());
  for (int handle : handles) {
    entries.push_back(DropOrderEntry::Existing(handle));
  }
  return entries;
}

}  // namespace

DropOrderEntry DropOrderEntry::Source() {
  return {.is_source = true};
}

DropOrderEntry DropOrderEntry::Existing(int tab_handle) {
  return {.is_source = false, .existing_tab_handle = tab_handle};
}

std::optional<size_t> HitTestVisiblePane(
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) {
  for (size_t index = 0; index < visible_panes.size(); ++index) {
    if (!visible_panes[index].bounds.IsEmpty() &&
        visible_panes[index].bounds.Contains(point)) {
      return index;
    }
  }
  return std::nullopt;
}

DropZone ClassifyDropZone(const gfx::Point& point,
                          const gfx::Rect& pane_bounds) {
  if (pane_bounds.IsEmpty()) {
    return DropZone::kRight;
  }
  const double width = std::max(1, pane_bounds.width());
  const double height = std::max(1, pane_bounds.height());
  const std::array<std::pair<double, DropZone>, 4> distances = {{
      {static_cast<double>(point.x() - pane_bounds.x()) / width,
       DropZone::kLeft},
      {static_cast<double>(pane_bounds.right() - point.x()) / width,
       DropZone::kRight},
      {static_cast<double>(point.y() - pane_bounds.y()) / height,
       DropZone::kTop},
      {static_cast<double>(pane_bounds.bottom() - point.y()) / height,
       DropZone::kBottom},
  }};
  return std::min_element(distances.begin(), distances.end(),
                          [](const auto& left, const auto& right) {
                            return left.first < right.first;
                          })
      ->second;
}

std::optional<DropIntent> CalculateDropIntent(
    const drag::SidebarTabDragPayload& payload,
    const std::optional<SplitDropTabState>& source_state,
    const SplitDropTabState& target_state,
    size_t target_pane_index,
    const gfx::Point& point,
    const std::vector<SplitDropPane>& visible_panes) {
  if (!payload.is_valid() || target_state.tab_handle < 0 ||
      (!source_state.has_value() && !payload.is_saved_tab())) {
    return std::nullopt;
  }
  auto pane_it = std::ranges::find(visible_panes, target_pane_index,
                                   &SplitDropPane::pane_index);
  if (pane_it == visible_panes.end() || !pane_it->bounds.Contains(point)) {
    return std::nullopt;
  }

  const std::vector<int> target_order = NormalizedTargetOrder(target_state);
  auto target_position =
      std::ranges::find(target_order, target_state.tab_handle);
  if (target_order.empty() || target_order.size() > kMaximumPaneCount ||
      target_position == target_order.end()) {
    return std::nullopt;
  }

  const DropZone zone = ClassifyDropZone(point, pane_it->bounds);
  DropIntent intent{
      .source = payload,
      .zone = zone,
      .target_tab_handle = target_state.tab_handle,
      .target_pane_index = target_pane_index,
      .layout = LayoutForZone(zone),
      .highlight_bounds = HighlightForZone(pane_it->bounds, zone)};

  if (source_state.has_value()) {
    if (source_state->tab_handle < 0 ||
        source_state->tab_handle == target_state.tab_handle) {
      return std::nullopt;
    }
    if (source_state->split_id.has_value()) {
      if (!target_state.split_id.has_value() ||
          source_state->split_id != target_state.split_id) {
        return std::nullopt;
      }
      std::vector<int> reordered = target_order;
      auto source_position =
          std::ranges::find(reordered, source_state->tab_handle);
      if (source_position == reordered.end()) {
        return std::nullopt;
      }
      intent.action = DropAction::kReorderInSplit;
      const size_t source_index =
          static_cast<size_t>(source_position - reordered.begin());
      const size_t target_index =
          static_cast<size_t>(target_position - target_order.begin());

      if (reordered.size() == 2u) {
        reordered = IsLeadingZone(zone)
                        ? std::vector<int>{source_state->tab_handle,
                                           target_state.tab_handle}
                        : std::vector<int>{target_state.tab_handle,
                                           source_state->tab_handle};
        intent.layout = LayoutForZone(zone);
      } else if (reordered.size() == 3u &&
                 LayoutForZone(zone) != target_state.split_layout) {
        const auto other = std::ranges::find_if(reordered, [&](int handle) {
          return handle != source_state->tab_handle &&
                 handle != target_state.tab_handle;
        });
        if (other == reordered.end()) {
          return std::nullopt;
        }
        const size_t other_index =
            static_cast<size_t>(other - reordered.begin());
        intent.layout = target_state.split_layout;
        intent.arrangement = other_index < target_index
                                 ? split_tabs::SplitTabArrangement::kMainStart
                                 : split_tabs::SplitTabArrangement::kMainEnd;
        reordered = IsLeadingZone(zone)
                        ? std::vector<int>{*other, source_state->tab_handle,
                                           target_state.tab_handle}
                        : std::vector<int>{*other, target_state.tab_handle,
                                           source_state->tab_handle};
      } else {
        // Linear 3-pane drops use the pointed edge as before/after. A four
        // pane split stays a grid and moves the source into the target slot.
        const bool grid = reordered.size() == 4u;
        const size_t destination =
            grid ? target_index
                 : target_index + (IsLeadingZone(zone) ? 0u : 1u);
        reordered.erase(reordered.begin() + source_index);
        size_t adjusted_destination = destination;
        if (!grid && source_index < destination) {
          --adjusted_destination;
        }
        reordered.insert(reordered.begin() +
                             std::min(adjusted_destination, reordered.size()),
                         source_state->tab_handle);
        intent.layout = target_state.split_layout;
      }
      intent.desired_order = ToEntries(reordered);
      return intent;
    }
  }

  if (target_order.size() >= kMaximumPaneCount) {
    return std::nullopt;
  }

  intent.action = DropAction::kCreateOrAddToSplit;
  std::vector<DropOrderEntry> desired = ToEntries(target_order);
  const size_t target_index =
      static_cast<size_t>(target_position - target_order.begin());

  if (target_order.size() == 1u) {
    desired.insert(desired.begin() + (IsLeadingZone(zone) ? 0u : 1u),
                   DropOrderEntry::Source());
  } else if (target_order.size() == 2u &&
             LayoutForZone(zone) != target_state.split_layout) {
    // Cross-axis drops make the untouched pane the main pane and split the
    // target pane with the source. Pane zero is Chromium's logical main pane;
    // the arrangement places it at the correct physical start/end edge.
    const size_t other_index = target_index == 0u ? 1u : 0u;
    const int other_handle = target_order[other_index];
    intent.layout = target_state.split_layout;
    intent.arrangement = other_index == 0u
                             ? split_tabs::SplitTabArrangement::kMainStart
                             : split_tabs::SplitTabArrangement::kMainEnd;
    desired.clear();
    desired.push_back(DropOrderEntry::Existing(other_handle));
    if (IsLeadingZone(zone)) {
      desired.push_back(DropOrderEntry::Source());
      desired.push_back(DropOrderEntry::Existing(target_state.tab_handle));
    } else {
      desired.push_back(DropOrderEntry::Existing(target_state.tab_handle));
      desired.push_back(DropOrderEntry::Source());
    }
  } else {
    // Primary-axis three-pane drops remain linear. Adding the fourth pane
    // keeps Chromium's row-major grid and places the source next to the pane
    // edge under the pointer.
    intent.layout = target_state.split_layout;
    const size_t insertion = target_index + (IsLeadingZone(zone) ? 0u : 1u);
    desired.insert(desired.begin() + insertion, DropOrderEntry::Source());
  }

  intent.desired_order = std::move(desired);
  return intent;
}

}  // namespace ahoi::split_drop
