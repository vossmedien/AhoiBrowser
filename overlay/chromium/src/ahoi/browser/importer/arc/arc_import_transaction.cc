// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_transaction.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/strings/string_number_conversions.h"

namespace ahoi::importer::arc {

namespace {

std::u16string UniqueWorkspaceName(
    std::u16string base_name,
    const std::set<std::u16string>& existing_names) {
  if (!existing_names.contains(base_name)) {
    return base_name;
  }
  const std::u16string stem = base_name + u" (Arc)";
  if (!existing_names.contains(stem)) {
    return stem;
  }
  for (size_t suffix = 2; suffix <= kMaxWorkspaceCount + 1; ++suffix) {
    const std::u16string candidate =
        stem + u" " + base::NumberToString16(suffix);
    if (!existing_names.contains(candidate)) {
      return candidate;
    }
  }
  return std::u16string();
}

bool NodesEquivalentForIdempotence(const tab_tree::TreeNode& first,
                                   const tab_tree::TreeNode& second) {
  return first.id == second.id && first.workspace_id == second.workspace_id &&
         first.parent_id == second.parent_id && first.type == second.type &&
         first.title == second.title && first.icon == second.icon &&
         first.accent_argb == second.accent_argb && first.url == second.url &&
         first.sort_key == second.sort_key &&
         first.tombstone == second.tombstone;
}

bool WorkspacesEquivalentExceptName(const tab_tree::Workspace& first,
                                    const tab_tree::Workspace& second) {
  return first.id == second.id && first.icon == second.icon &&
         first.sort_key == second.sort_key &&
         first.accent_argb == second.accent_argb &&
         first.tombstone == second.tombstone;
}

bool WorkspacesEquivalentForIdempotence(const tab_tree::Workspace& first,
                                        const tab_tree::Workspace& second) {
  return first.name == second.name &&
         WorkspacesEquivalentExceptName(first, second);
}

std::string RootSortPrefix(const tab_tree::TabTreeSnapshot& current,
                           const base::Uuid& workspace_id) {
  std::string last;
  for (const tab_tree::TreeNode& node : current.nodes) {
    if (node.workspace_id == workspace_id && !node.parent_id.has_value() &&
        !node.tombstone && node.sort_key > last) {
      last = node.sort_key;
    }
  }
  return last.empty() ? std::string("@") : last + "@";
}

bool ValidateSnapshot(const tab_tree::TabTreeSnapshot& snapshot) {
  tab_tree::TabTreeStore validator;
  return validator.InitializeInMemory() &&
         validator.ReplaceWithSnapshot(snapshot) ==
             tab_tree::TabTreeStore::Result::kOk;
}

}  // namespace

ArcImportMergeResult MergeArcImportPlan(
    const tab_tree::TabTreeSnapshot& current,
    const ArcImportPlan& import_plan,
    ArcConflictResolution conflict_resolution) {
  ArcImportMergeResult result;
  if (import_plan.schema_version != kArcImportPlanSchemaVersion ||
      !ValidateSnapshot(current) || !ValidateSnapshot(import_plan.tree)) {
    result.status = ArcImportStatus::kTransactionFailed;
    return result;
  }

  tab_tree::TabTreeSnapshot merged = current;
  ArcImportPlan applied = import_plan;
  applied.tree = {};
  applied.splits.clear();

  std::map<base::Uuid, tab_tree::Workspace> current_workspaces;
  std::map<base::Uuid, tab_tree::TreeNode> current_nodes;
  std::set<std::u16string> workspace_names;
  for (const tab_tree::Workspace& workspace : current.workspaces) {
    current_workspaces.emplace(workspace.id, workspace);
    if (!workspace.tombstone) {
      workspace_names.insert(workspace.name);
    }
  }
  for (const tab_tree::TreeNode& node : current.nodes) {
    current_nodes.emplace(node.id, node);
  }

  std::map<base::Uuid, base::Uuid> workspace_remap;
  std::set<base::Uuid> skipped_source_workspaces;
  for (tab_tree::Workspace workspace : import_plan.tree.workspaces) {
    const auto id_it = current_workspaces.find(workspace.id);
    if (id_it != current_workspaces.end()) {
      if (!WorkspacesEquivalentForIdempotence(id_it->second, workspace)) {
        std::set<std::u16string> names_without_existing;
        for (const tab_tree::Workspace& candidate : merged.workspaces) {
          if (!candidate.tombstone && candidate.id != workspace.id) {
            names_without_existing.insert(candidate.name);
          }
        }
        const std::u16string expected_renamed_name =
            UniqueWorkspaceName(workspace.name, names_without_existing);
        if (conflict_resolution != ArcConflictResolution::kRename ||
            !WorkspacesEquivalentExceptName(id_it->second, workspace) ||
            expected_renamed_name.empty() ||
            expected_renamed_name != id_it->second.name) {
          result.status = ArcImportStatus::kConflict;
          return result;
        }
      }
      workspace_remap.emplace(workspace.id, workspace.id);
      continue;
    }

    const auto name_it = std::find_if(
        current.workspaces.begin(), current.workspaces.end(),
        [&](const tab_tree::Workspace& candidate) {
          return !candidate.tombstone && candidate.name == workspace.name;
        });
    if (name_it != current.workspaces.end()) {
      if (conflict_resolution == ArcConflictResolution::kSkip) {
        skipped_source_workspaces.insert(workspace.id);
        ++result.skipped_workspace_count;
        continue;
      }
      if (conflict_resolution == ArcConflictResolution::kMerge) {
        workspace_remap.emplace(workspace.id, name_it->id);
        ++result.merged_workspace_count;
        continue;
      }
      workspace.name = UniqueWorkspaceName(workspace.name, workspace_names);
      if (workspace.name.empty()) {
        result.status = ArcImportStatus::kConflict;
        return result;
      }
      ++result.renamed_workspace_count;
    }
    workspace_names.insert(workspace.name);
    workspace_remap.emplace(workspace.id, workspace.id);
    merged.workspaces.push_back(workspace);
    applied.tree.workspaces.push_back(std::move(workspace));
    result.changed = true;
  }

  std::map<base::Uuid, std::string> merge_sort_prefixes;
  for (tab_tree::TreeNode node : import_plan.tree.nodes) {
    if (skipped_source_workspaces.contains(node.workspace_id)) {
      continue;
    }
    const auto workspace_it = workspace_remap.find(node.workspace_id);
    if (workspace_it == workspace_remap.end()) {
      result.status = ArcImportStatus::kTransactionFailed;
      return result;
    }
    const base::Uuid source_workspace_id = node.workspace_id;
    node.workspace_id = workspace_it->second;
    if (node.workspace_id != source_workspace_id &&
        !node.parent_id.has_value()) {
      std::string& prefix = merge_sort_prefixes[node.workspace_id];
      if (prefix.empty()) {
        prefix = RootSortPrefix(current, node.workspace_id);
      }
      node.sort_key = prefix + node.sort_key;
    }

    const auto existing_it = current_nodes.find(node.id);
    if (existing_it != current_nodes.end()) {
      if (!NodesEquivalentForIdempotence(existing_it->second, node)) {
        result.status = ArcImportStatus::kConflict;
        return result;
      }
      continue;
    }
    merged.nodes.push_back(node);
    applied.tree.nodes.push_back(std::move(node));
    result.changed = true;
  }

  std::set<base::Uuid> applied_node_ids;
  for (const tab_tree::TreeNode& node : applied.tree.nodes) {
    applied_node_ids.insert(node.id);
  }
  for (const ArcSplitDescriptor& split : import_plan.splits) {
    if (!applied_node_ids.contains(split.folder_node_id)) {
      continue;
    }
    if (!std::ranges::all_of(split.member_node_ids, [&](const base::Uuid& id) {
          return applied_node_ids.contains(id);
        })) {
      result.status = ArcImportStatus::kTransactionFailed;
      return result;
    }
    applied.splits.push_back(split);
  }

  if (!result.changed) {
    result.status = ArcImportStatus::kNoChanges;
    result.merged_tree = current;
    result.applied_plan = std::move(applied);
    return result;
  }
  if (!ValidateSnapshot(merged)) {
    result.status = ArcImportStatus::kTransactionFailed;
    return result;
  }
  result.status = ArcImportStatus::kOk;
  result.merged_tree = std::move(merged);
  result.applied_plan = std::move(applied);
  return result;
}

}  // namespace ahoi::importer::arc
