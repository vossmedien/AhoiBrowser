// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_directory_order.h"

#include <algorithm>
#include <cstddef>
#include <tuple>

namespace ahoi::session {

std::vector<DirectoryWorkspace> OrderDirectoryWorkspaces(
    const std::vector<DirectoryWorkspace>& main_workspaces,
    const std::vector<IsolatedProfileEntry>& isolated_entries) {
  struct Ranked {
    DirectoryWorkspace workspace;
    bool unkeyed;
    bool isolated;
    size_t input_index;
  };
  std::vector<Ranked> ranked;
  ranked.reserve(main_workspaces.size() + isolated_entries.size());
  for (const DirectoryWorkspace& workspace : main_workspaces) {
    ranked.push_back({workspace, false, false, ranked.size()});
  }
  for (const IsolatedProfileEntry& entry : isolated_entries) {
    if (entry.state == IsolatedProfileState::kDeleting ||
        entry.state == IsolatedProfileState::kConverting) {
      continue;
    }
    ranked.push_back({{.workspace_id = entry.workspace_id,
                       .sort_key = entry.sort_key,
                       .profile_dir = entry.profile_dir},
                      entry.sort_key.empty(),
                      true,
                      ranked.size()});
  }
  std::ranges::stable_sort(ranked, [](const Ranked& a, const Ranked& b) {
    return std::tie(a.unkeyed, a.workspace.sort_key, a.isolated,
                    a.input_index) < std::tie(b.unkeyed, b.workspace.sort_key,
                                              b.isolated, b.input_index);
  });
  std::vector<DirectoryWorkspace> result;
  result.reserve(ranked.size());
  for (Ranked& item : ranked) {
    result.push_back(std::move(item.workspace));
  }
  return result;
}

std::string NextDirectorySortKey(
    const std::vector<DirectoryWorkspace>& ordered) {
  std::string greatest;
  for (const DirectoryWorkspace& workspace : ordered) {
    greatest = std::max(greatest, workspace.sort_key);
  }
  return greatest.empty() ? std::string("00000000") : greatest + '@';
}

}  // namespace ahoi::session
