// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_keyboard_actions.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ahoi::split_drop {

std::optional<size_t> ResolveKeyboardPane(size_t one_based_pane,
                                          size_t pane_count) {
  if (one_based_pane == 0 || one_based_pane > pane_count || pane_count < 2 ||
      pane_count > split_tabs::SplitTabVisualData::kMaxPanes) {
    return std::nullopt;
  }
  return one_based_pane - 1;
}

std::optional<size_t> ResolveReorderTarget(size_t current_pane,
                                           size_t pane_count,
                                           int direction) {
  if (pane_count < 2 ||
      pane_count > split_tabs::SplitTabVisualData::kMaxPanes ||
      current_pane >= pane_count || (direction != -1 && direction != 1)) {
    return std::nullopt;
  }
  if ((direction < 0 && current_pane == 0) ||
      (direction > 0 && current_pane + 1 == pane_count)) {
    return std::nullopt;
  }
  return static_cast<size_t>(static_cast<int>(current_pane) + direction);
}

split_tabs::SplitTabVisualData NextKeyboardLayout(
    const split_tabs::SplitTabVisualData& current,
    size_t pane_count) {
  using Arrangement = split_tabs::SplitTabArrangement;
  using Layout = split_tabs::SplitTabLayout;

  if (pane_count == 2 || pane_count == 4) {
    split_tabs::SplitTabVisualData next = current;
    next.set_split_layout(current.split_layout() == Layout::kSideBySide
                              ? Layout::kStacked
                              : Layout::kSideBySide);
    if (pane_count == 4) {
      next.set_arrangement(Arrangement::kLinear);
    }
    return next;
  }
  if (pane_count != 3) {
    return current;
  }

  struct LayoutState {
    Layout layout;
    Arrangement arrangement;
  };
  constexpr std::array<LayoutState, 6> kStates = {{
      {Layout::kSideBySide, Arrangement::kLinear},
      {Layout::kStacked, Arrangement::kLinear},
      {Layout::kSideBySide, Arrangement::kMainStart},
      {Layout::kSideBySide, Arrangement::kMainEnd},
      {Layout::kStacked, Arrangement::kMainStart},
      {Layout::kStacked, Arrangement::kMainEnd},
  }};
  size_t current_index = 0;
  size_t index = 0;
  for (const LayoutState& candidate : kStates) {
    if (candidate.layout == current.split_layout() &&
        candidate.arrangement == current.arrangement()) {
      current_index = index;
      break;
    }
    ++index;
  }
  const LayoutState& state = kStates.at((current_index + 1) % kStates.size());
  split_tabs::SplitTabVisualData next = current;
  next.set_split_layout(state.layout);
  next.set_arrangement(state.arrangement);
  return next;
}

double AdjustKeyboardSplitRatio(double current_ratio, int direction) {
  if (!std::isfinite(current_ratio) || (direction != -1 && direction != 1)) {
    return current_ratio;
  }
  return std::clamp(current_ratio + static_cast<double>(direction) * 0.05, 0.1,
                    0.9);
}

}  // namespace ahoi::split_drop
