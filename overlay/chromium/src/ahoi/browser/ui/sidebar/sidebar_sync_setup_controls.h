// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SYNC_SETUP_CONTROLS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SYNC_SETUP_CONTROLS_H_

#include <memory>

namespace views {
class View;
}
namespace ahoi::sync {
class ProfileSyncService;
}

namespace ahoi::sidebar {
std::unique_ptr<views::View> CreateSidebarSyncSetupControls();
void UpdateSidebarSyncSetupControls(views::View* view,
                                    sync::ProfileSyncService* service);
}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SYNC_SETUP_CONTROLS_H_
