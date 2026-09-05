// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SHARED_TAB_TARGET_POLICY_H_
#define AHOI_BROWSER_SESSION_SHARED_TAB_TARGET_POLICY_H_

#include <optional>

#include "ahoi/browser/sync/shared_tab_target_types.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::session {

// Native policy uses the same atomic target values as Sync. These aliases do
// not encode wire data, announce capability or authorize shared-tab writes.
using SharedTabTargetKind = ahoi::sync::SharedTabTargetKind;
using SharedTabTarget = ahoi::sync::SharedTabTarget;

enum class NativeSharedTabParticipation {
  // Includes private tabs and automatic startup/recovery placeholders. The
  // caller must establish normal-profile/user-tab ownership before opting in.
  kExcluded,
  kNormal,
  // Pass the explicit logical empty target, not a platform's private NTP URL.
  kExplicitEmptyTemporary,
};

// Drops local-only URL bytes entirely; keeps only the bounded scheme class.
// Empty or excluded runtime targets cannot silently become shared new tabs.
// Does not inspect/authorize titles, cookies, storage or any other metadata.
std::optional<SharedTabTarget> DescribeNativeSharedTabTarget(
    const GURL& native_url,
    NativeSharedTabParticipation participation =
        NativeSharedTabParticipation::kExcluded);

// Consumes a page target already selected by the versioned URL field group.
// Folders/legacy records are handled by their existing adapters, not here.
bool IsValidSharedPageTarget(const SharedTabTarget& target, bool is_temporary);

// A linked Presence decorates exactly its known page, never another page with
// the same URL. Unlinked legacy presence stays outside this shared-row path.
bool SharedTabPresenceMatchesPage(
    const SharedTabTarget& presence,
    const std::optional<base::Uuid>& presence_tree_node_id,
    const base::Uuid& page_id,
    const SharedTabTarget& page,
    bool page_is_temporary);

enum class SharedTabTargetAction {
  kUnavailable,
  kWebNavigation,
  kPlatformNewTab,
  // Activate an existing local binding only: no empty-URL navigation, OS/file
  // launch, JavaScript execution or fallback URL is authorized by this result.
  kExistingLocalRuntime,
};

// Pure policy for a later EXPLICIT activation. Never call navigation merely
// because a remote record arrived. The identity-aware caller reuses an existing
// bound tab and retains its native target for kExistingLocalRuntime; this API
// deliberately cannot return private file/code/extension URL bytes.
SharedTabTargetAction SelectSharedTabTargetAction(
    const base::Uuid& page_id,
    const SharedTabTarget& target,
    bool is_temporary,
    const std::optional<base::Uuid>& local_runtime_tree_node_id = std::nullopt);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_SHARED_TAB_TARGET_POLICY_H_
