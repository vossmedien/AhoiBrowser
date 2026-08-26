// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_EDGE_REVEAL_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_EDGE_REVEAL_VIEW_H_

#include <memory>

#include "base/functional/callback.h"

namespace views {
class View;
}

namespace ahoi::sidebar {

// Creates the transparent client-area hot zone used while the persisted
// sidebar mode is hidden. BrowserView owns its geometry so revealing the
// overlay never reserves renderer viewport width.
std::unique_ptr<views::View> CreateSidebarEdgeRevealView(
    base::RepeatingClosure reveal_callback);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_EDGE_REVEAL_VIEW_H_
