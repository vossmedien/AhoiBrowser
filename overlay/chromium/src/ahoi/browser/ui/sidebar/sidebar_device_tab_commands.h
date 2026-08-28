// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DEVICE_TAB_COMMANDS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DEVICE_TAB_COMMANDS_H_

#include <string_view>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/sync/sync_model.h"
#include "base/time/time.h"

namespace ahoi::sidebar {

// Produces a profile-local CommandService snapshot from the already validated
// Ahoi SyncProvider projection. The opaque stable id contains no URL or title.
std::vector<CommandItem> BuildDeviceTabCommandItems(
    const sync::DeviceTabsSnapshot& snapshot,
    base::Time now);

// Resolves an opaque command identity against the current immutable snapshot.
// All provider, device, session, incognito and URL boundaries are rechecked so
// a queued UI activation becomes a no-op after revoke, tombstone or expiry.
const sync::RemoteTabRecord* ResolveDeviceTabCommand(
    const sync::DeviceTabsSnapshot& snapshot,
    std::string_view stable_id,
    base::Time now);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DEVICE_TAB_COMMANDS_H_
