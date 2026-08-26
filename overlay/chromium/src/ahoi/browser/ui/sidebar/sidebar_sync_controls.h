// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SYNC_CONTROLS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SYNC_CONTROLS_H_

#include <memory>
#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"
#include "base/functional/callback_forward.h"

namespace views {
class View;
}

namespace ahoi::sync {
class ProfileSyncService;
}

namespace ahoi::sidebar {

// Kept stable so BrowserSidebarHostView can retain this stateful control while
// rebuilding only the asynchronous remote-tab rows around it.
inline constexpr int kSidebarSyncControlsViewId = 0x41484f49;

std::unique_ptr<views::View> CreateSidebarSyncControlsView(
    sync::ProfileSyncService* service,
    std::vector<sync::DeviceRecord> filter_devices,
    base::RepeatingClosure filter_changed_callback);

void UpdateSidebarSyncControlsView(
    views::View* view,
    sync::ProfileSyncService* service,
    std::vector<sync::DeviceRecord> filter_devices);

bool SidebarSyncControlsMatchesDevice(const views::View* view,
                                      const base::Uuid& device_id);

// Focused View-test hooks. Production callers keep using the compact Sync
// disclosure; these make the visibility contract observable without exposing
// the private concrete View type.
void SetSidebarSyncSettingsExpandedForTesting(views::View* view, bool expanded);
bool SidebarSyncSettingsExpandedForTesting(const views::View* view);
bool SidebarSyncStatusVisibleForTesting(const views::View* view);
std::u16string SidebarSyncStatusTextForTesting(const views::View* view);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SYNC_CONTROLS_H_
