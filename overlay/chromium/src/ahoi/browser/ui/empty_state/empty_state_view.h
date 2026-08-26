// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_EMPTY_STATE_EMPTY_STATE_VIEW_H_
#define AHOI_BROWSER_UI_EMPTY_STATE_EMPTY_STATE_VIEW_H_

#include <memory>

namespace views {
class View;
}

namespace ahoi::empty_state {

// Creates the calm, native surface shown by an Ahoi window when its active
// workspace contains no live tabs. It owns no WebContents and is deliberately
// independent of the sidebar, so Cmd+T and sidebar actions remain available.
std::unique_ptr<views::View> CreateEmptyStateView();

}  // namespace ahoi::empty_state

#endif  // AHOI_BROWSER_UI_EMPTY_STATE_EMPTY_STATE_VIEW_H_
