// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/tab_switcher/tab_switcher_model.h"

#include <algorithm>
#include <map>

#include "base/strings/utf_string_conversions.h"

namespace ahoi::tab_switcher {

namespace {

std::u16string HostOf(const GURL& url) {
  return base::UTF8ToUTF16(url.has_host() ? url.host() : url.spec());
}

// Row of each entry in the stacked section grid.
std::vector<size_t> GridRows(const std::vector<Entry>& entries) {
  std::vector<size_t> rows(entries.size());
  size_t row = 0;
  size_t column = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index > 0 && entries[index].section != entries[index - 1].section) {
      ++row;
      column = 0;
    } else if (index > 0 && column == kColumns) {
      ++row;
      column = 0;
    }
    rows[index] = row;
    ++column;
  }
  return rows;
}

}  // namespace

std::vector<Entry> BuildEntries(const std::vector<TabInput>& tabs) {
  std::vector<Entry> entries;
  std::map<int, size_t> split_entry;
  for (const TabInput& tab : tabs) {
    if (tab.split.has_value()) {
      auto [it, inserted] = split_entry.emplace(*tab.split, entries.size());
      if (!inserted) {
        Entry& split = entries[it->second];
        split.tab_ids.push_back(tab.id);
        split.active |= tab.active;
        if (tab.last_active > split.last_active) {
          split.last_active = tab.last_active;
          split.title = tab.title;
          split.host = HostOf(tab.url);
        }
        continue;
      }
    }
    entries.push_back({.section = tab.split.has_value() ? Section::kSplits
                                  : tab.saved           ? Section::kSaved
                                                        : Section::kTemporary,
                       .tab_ids = {tab.id},
                       .title = tab.title,
                       .host = HostOf(tab.url),
                       .active = tab.active,
                       .last_active = tab.last_active});
  }
  // Keep the most recently used entries, then group them by section.
  std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) {
    return a.last_active > b.last_active;
  });
  if (entries.size() > kMaxEntries) {
    entries.resize(kMaxEntries);
  }
  std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) {
    return a.section < b.section;
  });
  return entries;
}

size_t InitialFocus(const std::vector<Entry>& entries) {
  std::optional<size_t> best;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (!entries[index].active &&
        (!best || entries[index].last_active > entries[*best].last_active)) {
      best = index;
    }
  }
  return best.value_or(0);
}

size_t MoveFocus(const std::vector<Entry>& entries, size_t focus, Move move) {
  if (entries.empty()) {
    return 0;
  }
  focus = std::min(focus, entries.size() - 1);
  switch (move) {
    case Move::kNext:
      return (focus + 1) % entries.size();
    case Move::kLeft:
      return focus > 0 ? focus - 1 : focus;
    case Move::kRight:
      return std::min(focus + 1, entries.size() - 1);
    case Move::kUp:
    case Move::kDown:
      break;
  }
  const std::vector<size_t> rows = GridRows(entries);
  const size_t row = rows[focus];
  if ((move == Move::kUp && row == 0) ||
      (move == Move::kDown && row == rows.back())) {
    return focus;
  }
  const size_t target_row = move == Move::kUp ? row - 1 : row + 1;
  size_t first_in_row = focus;
  while (first_in_row > 0 && rows[first_in_row - 1] == row) {
    --first_in_row;
  }
  const size_t column = focus - first_in_row;
  // The target row's entries; the same column, else its last entry.
  size_t result = focus;
  size_t seen = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (rows[index] != target_row) {
      continue;
    }
    result = index;
    if (seen++ == column) {
      break;
    }
  }
  return result;
}

}  // namespace ahoi::tab_switcher
