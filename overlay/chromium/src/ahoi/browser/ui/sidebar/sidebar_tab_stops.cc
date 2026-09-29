// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tab_stops.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ahoi::sidebar {

namespace {

// Wraps a signed stop offset into [0, count).
size_t WrapStopIndex(int64_t index, size_t count) {
  const int64_t signed_count = static_cast<int64_t>(count);
  return static_cast<size_t>(((index % signed_count) + signed_count) %
                             signed_count);
}

}  // namespace

SidebarTabStops::SidebarTabStops() = default;
SidebarTabStops::SidebarTabStops(SidebarTabStops&&) = default;
SidebarTabStops& SidebarTabStops::operator=(SidebarTabStops&&) = default;
SidebarTabStops::~SidebarTabStops() = default;

void SidebarTabStops::AddStop(std::vector<int> tab_handles) {
  if (tab_handles.empty()) {
    return;
  }
  stop_positions_.push_back(positions_.size());
  positions_.push_back(
      Position{.tab_handles = std::move(tab_handles), .is_stop = true});
}

void SidebarTabStops::AddHiddenTabs(std::vector<int> tab_handles) {
  if (tab_handles.empty()) {
    return;
  }
  positions_.push_back(
      Position{.tab_handles = std::move(tab_handles), .is_stop = false});
}

std::optional<int> SidebarTabStops::Step(std::optional<int> active_tab_handle,
                                         int delta) const {
  const size_t count = stop_positions_.size();
  if (count == 0u || delta == 0) {
    return std::nullopt;
  }
  const std::optional<size_t> position =
      active_tab_handle.has_value() ? FindPosition(*active_tab_handle)
                                    : std::nullopt;
  if (position.has_value() && positions_[*position].is_stop) {
    const size_t current = static_cast<size_t>(
        std::ranges::lower_bound(stop_positions_, *position) -
        stop_positions_.begin());
    const size_t target =
        WrapStopIndex(static_cast<int64_t>(current) + delta, count);
    return target == current ? std::nullopt
                             : StopTarget(active_tab_handle, target);
  }

  // A hidden or unknown tab is between stops: the first step enters the
  // neighbouring stop in that direction, every further step moves on.
  int64_t entry = 0;
  if (delta > 0) {
    if (position.has_value()) {
      const auto next = std::ranges::lower_bound(stop_positions_, *position);
      entry = next == stop_positions_.end()
                  ? 0
                  : static_cast<int64_t>(next - stop_positions_.begin());
    }
    return StopTarget(active_tab_handle,
                      WrapStopIndex(entry + delta - 1, count));
  }
  entry = static_cast<int64_t>(count) - 1;
  if (position.has_value()) {
    const auto next = std::ranges::lower_bound(stop_positions_, *position);
    entry = next == stop_positions_.begin()
                ? static_cast<int64_t>(count) - 1
                : static_cast<int64_t>(next - stop_positions_.begin()) - 1;
  }
  return StopTarget(active_tab_handle, WrapStopIndex(entry + delta + 1, count));
}

std::optional<int> SidebarTabStops::Nth(std::optional<int> active_tab_handle,
                                        size_t index) const {
  if (index >= stop_positions_.size()) {
    return std::nullopt;
  }
  return StopTarget(active_tab_handle, index);
}

std::optional<int> SidebarTabStops::Last(
    std::optional<int> active_tab_handle) const {
  if (stop_positions_.empty()) {
    return std::nullopt;
  }
  return StopTarget(active_tab_handle, stop_positions_.size() - 1u);
}

std::optional<size_t> SidebarTabStops::FindPosition(int tab_handle) const {
  for (size_t index = 0; index < positions_.size(); ++index) {
    if (std::ranges::find(positions_[index].tab_handles, tab_handle) !=
        positions_[index].tab_handles.end()) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<int> SidebarTabStops::StopTarget(
    std::optional<int> active_tab_handle,
    size_t stop_index) const {
  const std::vector<int>& tab_handles =
      positions_[stop_positions_[stop_index]].tab_handles;
  // Re-selecting the stop that already holds focus must not move focus to
  // another pane of the same split.
  if (active_tab_handle.has_value() &&
      std::ranges::find(tab_handles, *active_tab_handle) !=
          tab_handles.end()) {
    return std::nullopt;
  }
  return tab_handles.front();
}

}  // namespace ahoi::sidebar
