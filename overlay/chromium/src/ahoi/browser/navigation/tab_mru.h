// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_TAB_MRU_H_
#define AHOI_BROWSER_NAVIGATION_TAB_MRU_H_

#include <cstdint>
#include <deque>
#include <optional>

#include "base/functional/function_ref.h"

namespace ahoi {

// Activation order of one window's tabs, most recent first. It records only
// real tab activations, not visibility, so the panes of a split that stay
// visible together do not count as used. Callers pass tab handle values.
class TabMru {
 public:
  static constexpr size_t kMaxEntries = 256;

  TabMru();
  TabMru(const TabMru&);
  TabMru& operator=(const TabMru&);
  ~TabMru();

  void RecordActivation(int32_t tab_id);
  void Remove(int32_t tab_id);

  // The most recently activated tab other than `current` that is still
  // `eligible` (same Workspace, still open, not a Peek or modal surface).
  // Pressing the command again returns to the tab it came from.
  std::optional<int32_t> LastUsedBefore(
      int32_t current,
      base::FunctionRef<bool(int32_t)> eligible) const;

  size_t size() const { return order_.size(); }

 private:
  std::deque<int32_t> order_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_NAVIGATION_TAB_MRU_H_
