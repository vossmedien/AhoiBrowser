// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_SYNC_CONTROL_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_SYNC_CONTROL_H_

#include <memory>

class Profile;
namespace views {
class View;
}

namespace ahoi::sidebar {
// Profile-local explicit category consent. Constructing/opening this control
// never enables sync or authorizes bookmark upload.
std::unique_ptr<views::View> CreateBookmarkSyncControl(Profile* profile);
}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_SYNC_CONTROL_H_
