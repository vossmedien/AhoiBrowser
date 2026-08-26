// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_KEYBOARD_ACTIONS_H_
#define AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_KEYBOARD_ACTIONS_H_

#include <cstddef>
#include <optional>

#include "components/split_tabs/split_tab_visual_data.h"

namespace ahoi::split_drop {

// Returns the exact pane selected by a one-based keyboard shortcut. Invalid or
// currently hidden panes fail closed so the keystroke can continue to the web
// page or Chromium command dispatcher.
std::optional<size_t> ResolveKeyboardPane(size_t one_based_pane,
                                          size_t pane_count);

// Returns the adjacent pane for keyboard reordering. Reordering stops at the
// split edge instead of wrapping, so repeated key presses cannot unexpectedly
// move a pane from the last position to the first.
std::optional<size_t> ResolveReorderTarget(size_t current_pane,
                                           size_t pane_count,
                                           int direction);

// Cycles every supported visible layout without recreating WebContents. Two
// panes toggle columns/rows; three panes cycle columns, rows and the four
// main-pane arrangements; four panes toggle the persisted 2x2 orientation.
split_tabs::SplitTabVisualData NextKeyboardLayout(
    const split_tabs::SplitTabVisualData& current,
    size_t pane_count);

// Applies one bounded keyboard divider step. The 10%-90% safety range keeps
// both sides usable even in very large windows and rejects non-finite input.
double AdjustKeyboardSplitRatio(double current_ratio, int direction);

}  // namespace ahoi::split_drop

#endif  // AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_KEYBOARD_ACTIONS_H_
