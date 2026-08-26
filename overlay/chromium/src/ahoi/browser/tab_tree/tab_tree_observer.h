// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_TAB_TREE_TAB_TREE_OBSERVER_H_
#define AHOI_BROWSER_TAB_TREE_TAB_TREE_OBSERVER_H_

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/observer_list_types.h"

namespace ahoi::tab_tree {

class TabTreeObserver : public base::CheckedObserver {
 public:
  virtual void OnTabTreeChanged(const TabTreeChange& change) = 0;

 protected:
  ~TabTreeObserver() override = default;
};

}  // namespace ahoi::tab_tree

#endif  // AHOI_BROWSER_TAB_TREE_TAB_TREE_OBSERVER_H_
