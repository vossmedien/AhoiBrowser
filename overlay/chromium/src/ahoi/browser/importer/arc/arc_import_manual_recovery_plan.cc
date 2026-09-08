// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_manual_recovery_plan.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"

namespace ahoi::importer::arc {

std::optional<ArcImportManualRecoveryPlan> BuildArcImportManualRecoveryPlan(
    const ArcImportPreparedState& prepared,
    const tab_tree::TabTreeSnapshot& previous,
    const tab_tree::TabTreeSnapshot& current) {
  if (prepared.phase != ArcImportPreparedPhase::kManualRecoveryRequired ||
      !prepared.native_receipt_sha256.empty() ||
      prepared.affected_ids.empty() ||
      !IsArcImportTreeFingerprint(prepared.previous_tree_sha256) ||
      !IsArcImportTreeFingerprint(prepared.expected_tree_sha256) ||
      ComputeArcImportTreeFingerprint(previous) !=
          prepared.previous_tree_sha256 ||
      current.undo_operations != previous.undo_operations) {
    return std::nullopt;
  }
  std::set<base::Uuid> affected;
  for (const std::string& value : prepared.affected_ids) {
    const base::Uuid id = base::Uuid::ParseLowercase(value);
    if (!id.is_valid() || !affected.insert(id).second) {
      return std::nullopt;
    }
  }
  std::map<base::Uuid, const tab_tree::Workspace*> baseline_workspaces;
  for (const auto& workspace : previous.workspaces) {
    if (!baseline_workspaces.emplace(workspace.id, &workspace).second) {
      return std::nullopt;
    }
  }
  std::map<base::Uuid, const tab_tree::TreeNode*> baseline_nodes;
  for (const auto& node : previous.nodes) {
    if (!baseline_nodes.emplace(node.id, &node).second) {
      return std::nullopt;
    }
  }

  tab_tree::TabTreeSnapshot comparison = current;
  ArcImportManualRecoveryPlan plan;
  plan.recovery_tree = current;
  for (auto& workspace : comparison.workspaces) {
    const auto baseline = baseline_workspaces.find(workspace.id);
    if (baseline == baseline_workspaces.end()) {
      if (!affected.contains(workspace.id)) {
        return std::nullopt;
      }
      plan.removed_workspaces.push_back(workspace.id);
    } else if (!affected.contains(workspace.id)) {
      workspace = *baseline->second;
    }
  }
  std::set<base::Uuid> independent_temporary_nodes;
  for (auto& node : comparison.nodes) {
    const auto baseline = baseline_nodes.find(node.id);
    if (affected.contains(node.id)) {
      continue;
    }
    if (baseline != baseline_nodes.end()) {
      node = *baseline->second;
      continue;
    }
    // Native temporary tabs created after the failed import are not rollback
    // targets. Preserve them only in destinations that survive the rollback;
    // never infer authority to remove a new saved row or its local tab.
    if (node.type != tab_tree::TreeNodeType::kSavedPage || !node.is_temporary ||
        !baseline_workspaces.contains(node.workspace_id) ||
        (node.parent_id && !baseline_nodes.contains(*node.parent_id))) {
      return std::nullopt;
    }
    independent_temporary_nodes.insert(node.id);
  }
  std::erase_if(comparison.nodes, [&](const auto& node) {
    return independent_temporary_nodes.contains(node.id);
  });
  const std::string fingerprint = ComputeArcImportTreeFingerprint(comparison);
  if (fingerprint != prepared.expected_tree_sha256 &&
      fingerprint != prepared.previous_tree_sha256) {
    return std::nullopt;
  }

  for (auto& workspace : plan.recovery_tree.workspaces) {
    const auto baseline = baseline_workspaces.find(workspace.id);
    if (affected.contains(workspace.id) &&
        baseline != baseline_workspaces.end()) {
      workspace = *baseline->second;
    }
  }
  std::erase_if(plan.recovery_tree.workspaces, [&](const auto& workspace) {
    return affected.contains(workspace.id) &&
           !baseline_workspaces.contains(workspace.id);
  });
  for (auto& node : plan.recovery_tree.nodes) {
    const auto baseline = baseline_nodes.find(node.id);
    if (affected.contains(node.id) && baseline != baseline_nodes.end()) {
      node = *baseline->second;
    }
  }
  std::erase_if(plan.recovery_tree.nodes, [&](const auto& node) {
    return affected.contains(node.id) && !baseline_nodes.contains(node.id);
  });

  // The normal Store validates all parent/workspace references and undo rows.
  // Export its canonical ordering for the later exact live/durable checks.
  tab_tree::TabTreeStore validator;
  if (std::ranges::none_of(
          plan.recovery_tree.workspaces,
          [](const auto& workspace) { return !workspace.tombstone; }) ||
      !validator.InitializeInMemory() ||
      validator.ReplaceWithSnapshot(plan.recovery_tree) !=
          tab_tree::TabTreeStore::Result::kOk ||
      validator.ExportSnapshot(&plan.recovery_tree) !=
          tab_tree::TabTreeStore::Result::kOk) {
    return std::nullopt;
  }
  return plan;
}

}  // namespace ahoi::importer::arc
