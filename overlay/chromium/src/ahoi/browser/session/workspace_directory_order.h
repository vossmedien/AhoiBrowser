// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_WORKSPACE_DIRECTORY_ORDER_H_
#define AHOI_BROWSER_SESSION_WORKSPACE_DIRECTORY_ORDER_H_

#include <string>
#include <vector>

#include "ahoi/browser/session/isolated_profile_registry.h"
#include "base/uuid.h"

namespace ahoi::session {

// ADR 0011 step 2: one process-wide Workspace order for the switcher, the
// command bar, link routing and Quick Window. The main Profile's Workspaces
// keep their tree `sort_key`; a fully separated Workspace carries its key in
// its registry entry. Both use the same key space, so one sort merges them.
struct DirectoryWorkspace {
  base::Uuid workspace_id;
  std::string sort_key;
  // Empty for a Workspace of the main Profile.
  std::string profile_dir;

  bool operator==(const DirectoryWorkspace&) const = default;
};

// Merges `main_workspaces` (in their Profile's order) with the separated
// Workspaces of `isolated_entries`, skipping entries being deleted. Sorted by
// `sort_key`; on equal keys main Workspaces come first, then input order.
// Separated entries without a key (written before step 2) follow all others
// in registry order, which is where they appeared until now.
std::vector<DirectoryWorkspace> OrderDirectoryWorkspaces(
    const std::vector<DirectoryWorkspace>& main_workspaces,
    const std::vector<IsolatedProfileEntry>& isolated_entries);

// Key for a Workspace appended after every Workspace in `ordered`, main or
// separated: the greatest key plus '@', as SessionBridge::CreateWorkspace
// always did within one Profile.
std::string NextDirectorySortKey(const std::vector<DirectoryWorkspace>& ordered);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_WORKSPACE_DIRECTORY_ORDER_H_
