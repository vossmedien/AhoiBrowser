// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/tab_mru.h"

#include <algorithm>

namespace ahoi {

TabMru::TabMru() = default;
TabMru::TabMru(const TabMru&) = default;
TabMru& TabMru::operator=(const TabMru&) = default;
TabMru::~TabMru() = default;

void TabMru::RecordActivation(int32_t tab_id) {
  Remove(tab_id);
  order_.push_front(tab_id);
  if (order_.size() > kMaxEntries) {
    order_.pop_back();
  }
}

void TabMru::Remove(int32_t tab_id) {
  std::erase(order_, tab_id);
}

std::optional<int32_t> TabMru::LastUsedBefore(
    int32_t current,
    base::FunctionRef<bool(int32_t)> eligible) const {
  for (int32_t tab_id : order_) {
    if (tab_id != current && eligible(tab_id)) {
      return tab_id;
    }
  }
  return std::nullopt;
}

}  // namespace ahoi
