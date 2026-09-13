// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AHOI_BROWSER_SESSION_NATIVE_WORKSPACE_STRUCTURE_H_
#define AHOI_BROWSER_SESSION_NATIVE_WORKSPACE_STRUCTURE_H_

#include <optional>
#include "ahoi/browser/sync/shared_workspace_structure_types.h"
#include "ahoi/browser/sync/sync_authorization.h"
#include "components/split_tabs/split_tab_id.h"

class TabStripModel;
namespace ahoi {
class SessionBridge;
}

namespace ahoi::session {

std::optional<sync::SharedSplitMetadata> CaptureNativeSplit(
    SessionBridge& bridge,
    TabStripModel& model,
    split_tabs::SplitTabId native_id,
    const base::Uuid& logical_id,
    const sync::SharedSplitMetadata* previous);

// Only existing, normally bound members of ONE window/workspace. Absent pages
// remain deferred; passive materialization never navigates or changes profiles.
// Removed members remain ordinary native tabs, including active/form pages.
bool MaterializeNativeSplit(SessionBridge& bridge,
                            const sync::SharedSplitMetadata& desired,
                            std::optional<split_tabs::SplitTabId> native_id,
                            split_tabs::SplitTabId* applied_id,
                            sync::SyncAuthorization authorization);

}  // namespace ahoi::session
#endif
