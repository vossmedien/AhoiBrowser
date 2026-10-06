// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_MODEL_H_
#define AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_MODEL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "url/gurl.h"

namespace ahoi::tab_switcher {

// Arc-style tab switcher (user decision 6 October 2026, design reference
// design/references/2026-10-06/01): the open tabs of the window's active
// Workspace in three sections. Pure data, so ordering and keyboard movement
// are unit-tested without a browser.
enum class Section { kSaved, kTemporary, kSplits };

// One open tab as the controller sees it. `id` is opaque to the model.
struct TabInput {
  int id = 0;
  std::u16string title;
  GURL url;
  bool saved = false;
  // Tabs sharing a split value form one entry.
  std::optional<int> split;
  base::TimeTicks last_active;
  bool active = false;
};

struct Entry {
  Section section = Section::kSaved;
  // The split's panes in tab-strip order, else the single tab.
  std::vector<int> tab_ids;
  std::u16string title;
  std::u16string host;
  bool active = false;
  base::TimeTicks last_active;
};

// Splits use their most recently active pane for title and recency.
inline constexpr size_t kMaxEntries = 18;
inline constexpr size_t kColumns = 3;

// Sections in order saved, temporary, splits; most recent first inside each;
// at most kMaxEntries, dropping the least recently used.
std::vector<Entry> BuildEntries(const std::vector<TabInput>& tabs);

// The most recently used entry that is not the active one, so ⌃T then Return
// returns to the previous tab; 0 when there is none.
size_t InitialFocus(const std::vector<Entry>& entries);

enum class Move { kLeft, kRight, kUp, kDown, kNext };

// Grid movement over the sections, each laid out in rows of kColumns. Up and
// down keep the column where the neighbouring row has one; kNext wraps.
size_t MoveFocus(const std::vector<Entry>& entries, size_t focus, Move move);

}  // namespace ahoi::tab_switcher

#endif  // AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_MODEL_H_
