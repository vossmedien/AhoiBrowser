// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_drop_intent.h"

#include <algorithm>
#include <array>
#include <utility>

namespace ahoi::split_drop {

namespace {

constexpr size_t kMaximumPaneCount = 4;
constexpr int kOuterEdgeTolerance = 2;

int DetachDropZoneExtent(int pane_extent) {
  constexpr double kEdgeFraction = 0.36;
  constexpr int kMinimumExtent = 72;
  constexpr int kMaximumExtent = 160;
  const int scaled = static_cast<int>(pane_extent * kEdgeFraction);
  return std::min(std::max(1, pane_extent / 2),
                  std::clamp(scaled, kMinimumExtent, kMaximumExtent));
}

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

gfx::Rect HighlightForDetachZone(const gfx::Rect& pane, DropZone zone) {
  gfx::Rect highlight = pane;
  if (zone == DropZone::kLeft || zone == DropZone::kRight) {
    const int width = DetachDropZoneExtent(pane.width());
    highlight.set_width(width);
    if (zone == DropZone::kRight) {
      highlight.set_x(pane.right() - width);
    }
  } else {
    const int height = DetachDropZoneExtent(pane.height());
    highlight.set_height(height);
    if (zone == DropZone::kBottom) {
      highlight.set_y(pane.bottom() - height);
    }
  }
  highlight.Inset(4);
  return highlight;
}

gfx::Rect VisiblePaneBounds(const std::vector<SplitDropPane>& visible_panes) {
  gfx::Rect bounds;
  for (const SplitDropPane& pane : visible_panes) {
    if (!pane.bounds.IsEmpty()) {
      bounds.Union(pane.bounds);
    }
  }
  return bounds;
}

std::optional<DropZone> DetachZoneForPoint(const gfx::Point& point,
                                           const gfx::Rect& source_pane,
                                           const gfx::Rect& all_panes,
                                           split_tabs::SplitTabLayout layout) {
  if (source_pane.IsEmpty() || all_panes.IsEmpty() ||
      !source_pane.Contains(point)) {
    return std::nullopt;
  }

  std::optional<std::pair<int, DropZone>> nearest;
  const auto consider = [&nearest](bool eligible, int distance, DropZone zone) {
    if (eligible && distance >= 0 &&
        (!nearest.has_value() || distance < nearest->first)) {
      nearest = std::pair(distance, zone);
    }
  };

  if (layout == split_tabs::SplitTabLayout::kSideBySide) {
    // Hit and preview derive from the same semantic extent (the preview keeps
    // its four-DIP chrome inset). This makes leading and trailing panes equally
    // easy to pull out and avoids a tinted-but-rejected edge band.
    const int hit_extent = DetachDropZoneExtent(source_pane.width());
    consider(source_pane.x() <= all_panes.x() + kOuterEdgeTolerance &&
                 point.x() - source_pane.x() <= hit_extent,
             point.x() - source_pane.x(), DropZone::kLeft);
    consider(source_pane.right() >= all_panes.right() - kOuterEdgeTolerance &&
                 source_pane.right() - point.x() <= hit_extent,
             source_pane.right() - point.x(), DropZone::kRight);
  } else {
    const int hit_extent = DetachDropZoneExtent(source_pane.height());
    consider(source_pane.y() <= all_panes.y() + kOuterEdgeTolerance &&
                 point.y() - source_pane.y() <= hit_extent,
             point.y() - source_pane.y(), DropZone::kTop);
    consider(source_pane.bottom() >= all_panes.bottom() - kOuterEdgeTolerance &&
                 source_pane.bottom() - point.y() <= hit_extent,
             source_pane.bottom() - point.y(), DropZone::kBottom);
  }
  return nearest.has_value() ? std::make_optional(nearest->second)
                             : std::nullopt;
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

  // Dragging a visible split pane onto its own physical outer edge means
  // detaching that pane, not reordering it before/after itself. The split's
  // orientation selects the valid axis, while the pane/group geometry selects
  // the actual leading or trailing outside edge. Interior edges deliberately
  // keep their existing split/reorder semantics.
  if (source_state.has_value() && source_state->split_id.has_value() &&
      source_state->tab_handle == target_state.tab_handle &&
      source_state->split_id == target_state.split_id &&
      source_state->split_order == target_order &&
      source_state->split_order.size() >= 2u &&
      source_state->split_order.size() == visible_panes.size()) {
    const std::optional<DropZone> detach_zone = DetachZoneForPoint(
        point, pane_it->bounds, VisiblePaneBounds(visible_panes),
        source_state->split_layout);
    if (!detach_zone.has_value()) {
      return std::nullopt;
    }
    return DropIntent{.source = payload,
                      .action = DropAction::kDetachFromSplit,
                      .zone = *detach_zone,
                      .target_tab_handle = target_state.tab_handle,
                      .target_pane_index = target_pane_index,
                      .layout = source_state->split_layout,
                      .highlight_bounds = HighlightForDetachZone(
                          pane_it->bounds, *detach_zone)};
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
