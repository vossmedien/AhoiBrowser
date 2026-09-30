// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_profile_mapping.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_source_model.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"

namespace ahoi::importer::arc {

namespace {

// Moves the nodes of `workspace_ids` (in this order) into `separated`. With
// more than one source Workspace each gets a top-level folder.
void BuildSeparatedTree(const ArcImportPlan& plan,
                        const std::vector<base::Uuid>& workspace_ids,
                        ArcSeparatedWorkspacePlan* separated) {
  const std::set<base::Uuid> split_folders = [&plan] {
    std::set<base::Uuid> ids;
    for (const ArcSplitDescriptor& split : plan.splits) {
      ids.insert(split.folder_node_id);
    }
    return ids;
  }();
  const bool one_workspace = workspace_ids.size() == 1;
  for (size_t index = 0; index < workspace_ids.size(); ++index) {
    const base::Uuid& source_id = workspace_ids[index];
    std::optional<base::Uuid> root_parent;
    if (!one_workspace) {
      const auto source_it = std::ranges::find(
          plan.tree.workspaces, source_id, &tab_tree::Workspace::id);
      const base::Uuid folder_id = MakeDeterministicArcId(
          kArcSeparatedSpaceFolderIdDomain, source_id.AsLowercaseString());
      separated->tree.nodes.push_back(tab_tree::TreeNode{
          .id = folder_id,
          .workspace_id = separated->workspace_id,
          .type = tab_tree::TreeNodeType::kFolder,
          .title = source_it != plan.tree.workspaces.end()
                       ? source_it->name
                       : std::u16string(u"Imported Workspace"),
          .icon = u"folder",
          .sort_key = internal::SortKey(index),
          .created_at = base::Time::UnixEpoch(),
          .modified_at = base::Time::UnixEpoch(),
      });
      ++separated->folder_count;
      root_parent = folder_id;
    }
    for (const tab_tree::TreeNode& node : plan.tree.nodes) {
      if (node.workspace_id != source_id) {
        continue;
      }
      tab_tree::TreeNode moved = node;
      moved.workspace_id = separated->workspace_id;
      if (!moved.parent_id.has_value()) {
        moved.parent_id = root_parent;
      }
      // No native split exists in the new Profile; the members stay in a
      // plain folder, as a degraded split does in the main Profile.
      if (split_folders.contains(moved.id)) {
        moved.icon = u"folder";
      }
      if (moved.type == tab_tree::TreeNodeType::kSavedPage) {
        ++separated->page_count;
      } else {
        ++separated->folder_count;
      }
      separated->tree.nodes.push_back(std::move(moved));
    }
  }
}

}  // namespace

base::Uuid ArcSeparatedWorkspaceId(std::string_view arc_profile) {
  return MakeDeterministicArcId(kArcSeparatedWorkspaceIdDomain, arc_profile);
}

std::u16string ArcSeparatedWorkspaceName(
    const ArcImportProfileSpaces& profile) {
  if (profile.space_titles.size() == 1 &&
      !profile.space_titles.front().empty()) {
    return base::UTF8ToUTF16(profile.space_titles.front());
  }
  return u"Arc – " + base::UTF8ToUTF16(profile.directory_name);
}

bool IsValidArcSeparatedProfiles(const ArcImportPlan& plan,
                                 const std::vector<std::string>& separated) {
  std::set<std::string> seen;
  for (const std::string& name : separated) {
    const bool known = std::ranges::any_of(
        plan.arc_profiles, [&name](const ArcImportProfileSpaces& profile) {
          return profile.directory_name == name &&
                 !profile.space_titles.empty();
        });
    if (!known || !seen.insert(name).second ||
        !ArcSeparatedWorkspaceId(name).is_valid()) {
      return false;
    }
  }
  return true;
}

ArcProfileMapping MapArcImportPlanByProfile(
    const ArcImportPlan& plan,
    const std::vector<std::string>& separated) {
  ArcProfileMapping mapping;
  mapping.main_plan = plan;
  if (separated.empty()) {
    return mapping;
  }
  const std::set<std::string> separated_names(separated.begin(),
                                              separated.end());
  // Source Workspace IDs per separated profile, in plan order.
  std::map<std::string, std::vector<base::Uuid>> moved_workspaces;
  std::set<base::Uuid> moved_ids;
  for (const tab_tree::Workspace& workspace : plan.tree.workspaces) {
    const auto owner = plan.workspace_arc_profiles.find(workspace.id);
    const std::string arc_profile = owner != plan.workspace_arc_profiles.end()
                                        ? owner->second
                                        : std::string(kArcDefaultProfileName);
    if (separated_names.contains(arc_profile)) {
      moved_workspaces[arc_profile].push_back(workspace.id);
      moved_ids.insert(workspace.id);
    }
  }

  for (const ArcImportProfileSpaces& profile : plan.arc_profiles) {
    if (!separated_names.contains(profile.directory_name)) {
      continue;
    }
    ArcSeparatedWorkspacePlan entry{
        .arc_profile = profile.directory_name,
        .workspace_id = ArcSeparatedWorkspaceId(profile.directory_name),
        .name = ArcSeparatedWorkspaceName(profile),
        .space_count = profile.space_titles.size(),
    };
    entry.tree.workspaces.push_back(tab_tree::Workspace{
        .id = entry.workspace_id,
        .name = entry.name,
        .sort_key = "0",
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    BuildSeparatedTree(plan, moved_workspaces[profile.directory_name],
                       &entry);
    mapping.separated.push_back(std::move(entry));
  }

  // The main plan keeps everything else unchanged, including its IDs.
  ArcImportPlan& main = mapping.main_plan;
  std::set<base::Uuid> moved_nodes;
  for (const tab_tree::TreeNode& node : plan.tree.nodes) {
    if (moved_ids.contains(node.workspace_id)) {
      moved_nodes.insert(node.id);
    }
  }
  std::erase_if(main.tree.workspaces,
                [&moved_ids](const tab_tree::Workspace& workspace) {
                  return moved_ids.contains(workspace.id);
                });
  std::erase_if(main.tree.nodes,
                [&moved_nodes](const tab_tree::TreeNode& node) {
                  return moved_nodes.contains(node.id);
                });
  std::erase_if(main.splits, [&moved_nodes](const ArcSplitDescriptor& split) {
    return moved_nodes.contains(split.folder_node_id);
  });
  std::erase_if(main.degraded_split_folder_node_ids,
                [&moved_nodes](const base::Uuid& id) {
                  return moved_nodes.contains(id);
                });
  std::erase_if(main.global_top_app_page_node_ids,
                [&moved_nodes](const base::Uuid& id) {
                  return moved_nodes.contains(id);
                });
  for (const base::Uuid& id : moved_ids) {
    main.workspace_arc_profiles.erase(id);
  }
  return mapping;
}

std::optional<tab_tree::PortableWorkspaceSelection>
SelectArcSeparatedPortableTree(const ArcSeparatedWorkspacePlan& plan) {
  if (!plan.workspace_id.is_valid() || plan.tree.workspaces.size() != 1 ||
      plan.tree.workspaces.front().id != plan.workspace_id) {
    return std::nullopt;
  }
  return tab_tree::SelectPortableWorkspaceStructure(
      plan.tree, {plan.workspace_id}, /*include_temporary_pages=*/false);
}

}  // namespace ahoi::importer::arc
