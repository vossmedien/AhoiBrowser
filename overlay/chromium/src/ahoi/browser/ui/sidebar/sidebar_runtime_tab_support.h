// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_TAB_SUPPORT_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_TAB_SUPPORT_H_

#include <string>

namespace tabs {
class TabInterface;
}

namespace views {
class View;
}

namespace ahoi::sidebar::internal {

bool IsNewTabPage(tabs::TabInterface* tab);
std::u16string StableTabTitle(tabs::TabInterface* tab);

// Typed bridge into the private runtime-row implementation. The public tree
// traversal lives in the support translation unit and never assumes that a
// composite split child is itself a row.
void ClearOpenTabRowDropTargetPresentationForView(views::View* view);

}  // namespace ahoi::sidebar::internal

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_TAB_SUPPORT_H_
