// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_transaction.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_split_receipt.h"
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

std::string MergeRootSortPrefix(const base::Uuid& source_workspace_id) {
  return "arc@" + source_workspace_id.AsLowercaseString() + "@";
}

bool ValidateSnapshot(
    const tab_tree::TabTreeSnapshot& snapshot,
    tab_tree::TabTreeSnapshot* canonical_snapshot = nullptr) {
  tab_tree::TabTreeStore validator;
  return validator.InitializeInMemory() &&
         validator.ReplaceWithSnapshot(snapshot) ==
             tab_tree::TabTreeStore::Result::kOk &&
         (!canonical_snapshot ||
          validator.ExportSnapshot(canonical_snapshot) ==
              tab_tree::TabTreeStore::Result::kOk);
}

}  // namespace

ArcImportMergeResult MergeArcImportPlan(
    const tab_tree::TabTreeSnapshot& current,
    const ArcImportPlan& import_plan,
    ArcConflictResolution conflict_resolution) {
  ArcImportMergeResult result;
  if (import_plan.schema_version != kArcImportPlanSchemaVersion ||
      !ValidateSnapshot(current) || !ValidateSnapshot(import_plan.tree) ||
      !IsValidArcSplitStructure(import_plan)) {
    result.status = ArcImportStatus::kTransactionFailed;
    return result;
  }

  tab_tree::TabTreeSnapshot merged = current;
  ArcImportPlan applied = import_plan;
  applied.tree = {};
  applied.splits.clear();
  applied.degraded_split_folder_node_ids.clear();
  applied.global_top_app_page_node_ids.clear();
  // Parser statistics describe the source plan. Commit/preview result
  // statistics must instead describe exactly what this merge would add.
  applied.stats.imported_workspace_count = 0;
  applied.stats.imported_folder_count = 0;
  applied.stats.imported_page_count = 0;
  applied.stats.imported_split_count = 0;
  applied.stats.degraded_split_count = 0;
  applied.stats.imported_global_top_app_count = 0;
  applied.stats.deduplicated_workspace_count = 0;
  applied.stats.deduplicated_item_count = 0;
  applied.stats.deduplicated_split_count = 0;

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
      ++applied.stats.deduplicated_workspace_count;
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
        ++applied.stats.deduplicated_workspace_count;
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
    ++applied.stats.imported_workspace_count;
    result.changed = true;
  }

  const std::set<base::Uuid> global_top_app_page_ids(
      import_plan.global_top_app_page_node_ids.begin(),
      import_plan.global_top_app_page_node_ids.end());
  const std::set<base::Uuid> degraded_split_folder_ids(
      import_plan.degraded_split_folder_node_ids.begin(),
      import_plan.degraded_split_folder_node_ids.end());
  std::map<base::Uuid, base::Uuid> source_node_workspaces;
  for (const tab_tree::TreeNode& node : import_plan.tree.nodes) {
    source_node_workspaces.emplace(node.id, node.workspace_id);
    if ((global_top_app_page_ids.contains(node.id) &&
         node.type != tab_tree::TreeNodeType::kSavedPage) ||
        (degraded_split_folder_ids.contains(node.id) &&
         node.type != tab_tree::TreeNodeType::kFolder)) {
      result.status = ArcImportStatus::kTransactionFailed;
      return result;
    }
  }
  if (global_top_app_page_ids.size() !=
          import_plan.global_top_app_page_node_ids.size() ||
      degraded_split_folder_ids.size() !=
          import_plan.degraded_split_folder_node_ids.size() ||
      !std::ranges::all_of(global_top_app_page_ids,
                           [&](const base::Uuid& id) {
                             return source_node_workspaces.contains(id);
                           }) ||
      !std::ranges::all_of(degraded_split_folder_ids,
                           [&](const base::Uuid& id) {
                             return source_node_workspaces.contains(id);
                           })) {
    result.status = ArcImportStatus::kTransactionFailed;
    return result;
  }

  std::map<base::Uuid, tab_tree::TreeNode> runtime_candidate_nodes;
  std::set<base::Uuid> added_node_ids;
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
      node.sort_key = MergeRootSortPrefix(source_workspace_id) + node.sort_key;
    }
    if (!runtime_candidate_nodes.emplace(node.id, node).second) {
      result.status = ArcImportStatus::kTransactionFailed;
      return result;
    }

    const auto existing_it = current_nodes.find(node.id);
    if (existing_it != current_nodes.end()) {
      if (!NodesEquivalentForIdempotence(existing_it->second, node)) {
        result.status = ArcImportStatus::kConflict;
        return result;
      }
      ++applied.stats.deduplicated_item_count;
      continue;
    }
    if (node.type == tab_tree::TreeNodeType::kFolder) {
      ++applied.stats.imported_folder_count;
      if (degraded_split_folder_ids.contains(node.id)) {
        applied.degraded_split_folder_node_ids.push_back(node.id);
        ++applied.stats.degraded_split_count;
      }
    } else {
      ++applied.stats.imported_page_count;
      if (global_top_app_page_ids.contains(node.id)) {
        applied.global_top_app_page_node_ids.push_back(node.id);
        ++applied.stats.imported_global_top_app_count;
      }
    }
    merged.nodes.push_back(node);
    added_node_ids.insert(node.id);
    applied.tree.nodes.push_back(std::move(node));
    result.changed = true;
  }

  std::set<base::Uuid> runtime_node_ids;
  for (const tab_tree::TreeNode& node : applied.tree.nodes) {
    runtime_node_ids.insert(node.id);
  }
  for (const ArcSplitDescriptor& split : import_plan.splits) {
    const auto source_workspace =
        source_node_workspaces.find(split.folder_node_id);
    if (source_workspace == source_node_workspaces.end()) {
      result.status = ArcImportStatus::kTransactionFailed;
      return result;
    }
    if (skipped_source_workspaces.contains(source_workspace->second)) {
      continue;
    }

    bool split_changed = added_node_ids.contains(split.folder_node_id);
    std::vector<base::Uuid> runtime_ids = {split.folder_node_id};
    runtime_ids.insert(runtime_ids.end(), split.member_node_ids.begin(),
                       split.member_node_ids.end());
    for (const base::Uuid& id : runtime_ids) {
      const auto node_it = runtime_candidate_nodes.find(id);
      if (node_it == runtime_candidate_nodes.end()) {
        result.status = ArcImportStatus::kTransactionFailed;
        return result;
      }
      split_changed = split_changed || added_node_ids.contains(id);
      if (runtime_node_ids.insert(id).second) {
        applied.tree.nodes.push_back(node_it->second);
      }
    }
    applied.splits.push_back(split);
    if (split_changed) {
      ++applied.stats.imported_split_count;
    } else {
      ++applied.stats.deduplicated_split_count;
    }
  }

  std::set<base::Uuid> runtime_workspace_ids;
  for (const tab_tree::TreeNode& node : applied.tree.nodes) {
    runtime_workspace_ids.insert(node.workspace_id);
  }
  std::set<base::Uuid> applied_workspace_ids;
  for (const tab_tree::Workspace& workspace : applied.tree.workspaces) {
    applied_workspace_ids.insert(workspace.id);
  }
  for (const tab_tree::Workspace& workspace : merged.workspaces) {
    if (runtime_workspace_ids.contains(workspace.id) &&
        applied_workspace_ids.insert(workspace.id).second) {
      applied.tree.workspaces.push_back(workspace);
    }
  }

  if (!result.changed) {
    result.status = ArcImportStatus::kNoChanges;
    result.merged_tree = current;
    result.applied_plan = std::move(applied);
    return result;
  }
  tab_tree::TabTreeSnapshot canonical_merged;
  if (!ValidateSnapshot(merged, &canonical_merged)) {
    result.status = ArcImportStatus::kTransactionFailed;
    return result;
  }
  result.status = ArcImportStatus::kOk;
  // The service compares this snapshot exactly with the real store readback.
  // Appending source rows preserves their fields, but not ExportSnapshot's
  // canonical workspace/node/undo ordering. Reuse the validation store's
  // export so a successful persistence write cannot look like a foreign edit.
  // The runtime plan keeps its independent source/member ordering unchanged.
  result.merged_tree = std::move(canonical_merged);
  result.applied_plan = std::move(applied);
  return result;
}

}  // namespace ahoi::importer::arc
