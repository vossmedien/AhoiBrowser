// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/portable_workspace_structure.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "ahoi/browser/sync/workspace_structure_sync.h"

namespace ahoi::session {
namespace {

bool IsPortableTarget(const sync::SharedTabTarget& target) {
  return target.kind != sync::SharedTabTargetKind::kLocalOnly;
}

bool IsCompleteSplit(const sync::SharedSplitMetadata& split,
                     const std::set<base::Uuid>& selected_workspace_ids,
                     const std::map<base::Uuid, base::Uuid>& selected_pages) {
  return selected_workspace_ids.contains(split.workspace_id) &&
         sync::ValidateSplitMetadata(split) &&
         std::ranges::all_of(split.topology.member_ids,
                             [&](const base::Uuid& id) {
                               const auto page = selected_pages.find(id);
                               return page != selected_pages.end() &&
                                      page->second == split.workspace_id;
                             });
}

}  // namespace

std::optional<PortableWorkspaceStructure> SelectPortableWorkspaceStructure(
    const tab_tree::TabTreeSnapshot& tree,
    const WorkspaceStructureState& structure,
    const std::vector<base::Uuid>& selected_workspace_ids,
    bool include_temporary_pages,
    bool include_archives) {
  auto selected_tree = tab_tree::SelectPortableWorkspaceStructure(
      tree, selected_workspace_ids, include_temporary_pages);
  if (!selected_tree) {
    return std::nullopt;
  }

  PortableWorkspaceStructure result{.tree = std::move(*selected_tree)};
  std::set<base::Uuid> workspace_ids;
  std::set<base::Uuid> archived_page_ids;
  std::map<base::Uuid, base::Uuid> page_workspaces;
  std::map<base::Uuid, base::Uuid> folder_workspaces;
  for (const auto& workspace : result.tree.workspaces) {
    workspace_ids.insert(workspace.id);
  }
  for (const auto& item : structure.entries) {
    const auto* archive =
        std::get_if<sync::TabArchiveEntryRecord>(&item.second.record);
    if (!archive || archive->tombstone || archive->restored ||
        !workspace_ids.contains(archive->snapshot.workspace_id)) {
      continue;
    }
    for (const auto& page : archive->snapshot.pages) {
      archived_page_ids.insert(page.tree_node_id);
    }
  }
  std::erase_if(result.tree.nodes, [&](const auto& node) {
    return archived_page_ids.contains(node.id);
  });
  for (const auto& node : result.tree.nodes) {
    (node.type == tab_tree::TreeNodeType::kFolder ? folder_workspaces
                                                  : page_workspaces)
        .emplace(node.id, node.workspace_id);
  }

  for (const auto& item : structure.entries) {
    const auto& entry = item.second;
    if (const auto* split =
            std::get_if<sync::SplitGroupRecord>(&entry.record)) {
      if (split->tombstone || !workspace_ids.contains(split->workspace_id)) {
        continue;
      }
      const sync::SharedSplitMetadata metadata{
          .id = split->id,
          .workspace_id = split->workspace_id,
          .topology = split->topology,
          .ratios = split->ratios,
      };
      if (IsCompleteSplit(metadata, workspace_ids, page_workspaces)) {
        result.splits.push_back(metadata);
      } else if (sync::ValidateSplitMetadata(metadata) &&
                 std::ranges::all_of(metadata.topology.member_ids,
                                     [&](const base::Uuid& id) {
                                       return archived_page_ids.contains(id);
                                     })) {
        // This complete group is represented inside one archive snapshot.
        continue;
      } else {
        ++result.excluded_incomplete_splits;
      }
      continue;
    }

    const auto* archive =
        std::get_if<sync::TabArchiveEntryRecord>(&entry.record);
    if (!archive || archive->tombstone || archive->restored ||
        !workspace_ids.contains(archive->snapshot.workspace_id) ||
        !include_archives) {
      continue;
    }
    bool portable = sync::ValidateArchiveEntry(*archive);
    for (const auto& page : archive->snapshot.pages) {
      portable =
          portable && IsPortableTarget(page.target) &&
          (!page.home_target || IsPortableTarget(*page.home_target)) &&
          (!page.parent_id || (folder_workspaces.contains(*page.parent_id) &&
                               folder_workspaces.at(*page.parent_id) ==
                                   archive->snapshot.workspace_id));
    }
    if (!portable) {
      ++result.excluded_nonportable_archives;
      continue;
    }
    result.archives.push_back({
        .id = archive->id,
        .snapshot = archive->snapshot,
        .reason = archive->reason,
        .archived_at = archive->archived_at,
    });
  }
  return result;
}

}  // namespace ahoi::session
