// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/portable_workspace_import_plan.h"

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/session/portable_workspace_bundle.h"
#include "ahoi/browser/session/portable_workspace_import_destination.h"
#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace ahoi::session {
namespace {

tab_tree::TreeNode MakePage(base::Uuid id,
                            base::Uuid workspace_id,
                            std::optional<base::Uuid> parent_id,
                            std::u16string title,
                            std::string sort_key,
                            bool is_temporary,
                            const sync::SharedTabTarget& target,
                            const std::optional<sync::SharedTabTarget>& home,
                            base::Time now) {
  tab_tree::TreeNode node;
  node.id = id;
  node.workspace_id = workspace_id;
  node.parent_id = parent_id;
  node.type = tab_tree::TreeNodeType::kSavedPage;
  node.title = std::move(title);
  node.sort_key = std::move(sort_key);
  node.is_temporary = is_temporary;
  node.target_kind = target.kind;
  if (target.kind == sync::SharedTabTargetKind::kWeb) {
    node.url = GURL(target.url);
  }
  if (home) {
    node.home_target_kind = home->kind;
    if (home->kind == sync::SharedTabTargetKind::kWeb) {
      node.home_url = GURL(home->url);
    }
  }
  node.created_at = now;
  node.modified_at = now;
  return node;
}

bool StampNew(sync::SyncRecord* record,
              sync::HybridLogicalClock* clock,
              base::Time now) {
  const auto stamp = clock->Tick(now);
  std::visit([&](auto& value) { value.version = {.stamp = stamp}; }, *record);
  return sync::StampLocalMutation(nullptr, record) &&
         sync::ValidateRecord(*record);
}

bool AddSplit(const sync::SharedSplitMetadata& split,
              WorkspaceStructureState* state,
              sync::HybridLogicalClock* clock,
              base::Time now,
              bool* changed) {
  const auto existing = state->entries.find(split.id);
  if (existing != state->entries.end()) {
    const auto* record =
        std::get_if<sync::SplitGroupRecord>(&existing->second.record);
    return record && !record->tombstone &&
           record->workspace_id == split.workspace_id &&
           record->topology == split.topology && record->ratios == split.ratios;
  }
  WorkspaceStructureEntry entry;
  entry.record = sync::SplitGroupRecord{.id = split.id,
                                        .workspace_id = split.workspace_id,
                                        .topology = split.topology,
                                        .ratios = split.ratios};
  if (!StampNew(&entry.record, clock, now)) {
    return false;
  }
  state->entries.emplace(split.id, std::move(entry));
  *changed = true;
  return true;
}

}  // namespace

std::optional<PortableWorkspaceImportPlan> PreparePortableWorkspaceImport(
    const PortableWorkspaceStructure& imported,
    const tab_tree::TabTreeSnapshot& current_tree,
    const WorkspaceStructureState& current_structure,
    const base::Uuid& local_device_id,
    base::Time now) {
  if (!local_device_id.is_valid() || now.is_null() ||
      !EncodePortableWorkspaceBundle(imported)) {
    return std::nullopt;
  }
  const auto destination = AnalyzePortableWorkspaceDestination(
      imported, current_tree, current_structure);
  if (destination.conflicting_nodes || destination.conflicting_splits ||
      destination.conflicting_archives ||
      std::ranges::any_of(destination.workspaces, [](const auto& workspace) {
        return workspace.kind == PortableDestinationKind::kConflict;
      })) {
    return std::nullopt;
  }

  PortableWorkspaceImportPlan plan{.tree = current_tree,
                                   .structure = current_structure};
  std::set<base::Uuid> existing_workspaces;
  std::set<base::Uuid> existing_nodes;
  for (const auto& workspace : current_tree.workspaces) {
    existing_workspaces.insert(workspace.id);
  }
  for (const auto& node : current_tree.nodes) {
    existing_nodes.insert(node.id);
  }
  for (const auto& workspace : imported.tree.workspaces) {
    if (existing_workspaces.contains(workspace.id)) {
      continue;
    }
    plan.tree.workspaces.push_back(
        {.id = workspace.id,
         .name = workspace.name,
         .icon = workspace.icon,
         .sort_key = workspace.sort_key,
         .accent_argb = workspace.accent_argb,
         .created_at = now,
         .modified_at = now,
         .archive_policy = workspace.archive_policy});
    plan.changed = true;
  }
  for (const auto& source : imported.tree.nodes) {
    if (existing_nodes.contains(source.id)) {
      continue;
    }
    tab_tree::TreeNode node;
    if (source.type == tab_tree::TreeNodeType::kFolder) {
      node.id = source.id;
      node.workspace_id = source.workspace_id;
      node.parent_id = source.parent_id;
      node.type = source.type;
      node.title = source.title;
      node.icon = source.icon;
      node.accent_argb = source.accent_argb;
      node.sort_key = source.sort_key;
      node.created_at = now;
      node.modified_at = now;
    } else {
      if (!source.target) {
        return std::nullopt;
      }
      node = MakePage(source.id, source.workspace_id, source.parent_id,
                      source.title, source.sort_key, source.is_temporary,
                      *source.target, source.home_target, now);
    }
    plan.tree.nodes.push_back(std::move(node));
    existing_nodes.insert(source.id);
    plan.changed = true;
  }

  sync::HybridLogicalClock clock(local_device_id.AsLowercaseString());
  clock.Restore(current_structure.clock);
  bool structure_changed = false;
  for (const auto& split : imported.splits) {
    if (!AddSplit(split, &plan.structure, &clock, now, &structure_changed)) {
      return std::nullopt;
    }
  }
  for (const auto& archive : imported.archives) {
    if (archive.snapshot.split &&
        !AddSplit(*archive.snapshot.split, &plan.structure, &clock, now,
                  &structure_changed)) {
      return std::nullopt;
    }
    if (current_structure.entries.contains(archive.id)) {
      continue;
    }
    if (plan.structure.entries.contains(archive.id)) {
      return std::nullopt;
    }
    WorkspaceStructureEntry entry;
    entry.record =
        sync::TabArchiveEntryRecord{.id = archive.id,
                                    .snapshot = archive.snapshot,
                                    .reason = archive.reason,
                                    .archived_at = archive.archived_at};
    if (!StampNew(&entry.record, &clock, now)) {
      return std::nullopt;
    }
    for (const auto& page : archive.snapshot.pages) {
      if (!existing_nodes.insert(page.tree_node_id).second) {
        return std::nullopt;
      }
      auto node =
          MakePage(page.tree_node_id, archive.snapshot.workspace_id,
                   page.parent_id, base::UTF8ToUTF16(page.title), page.sort_key,
                   true, page.target, page.home_target, now);
      entry.private_nodes.push_back(node);
      plan.tree.nodes.push_back(std::move(node));
    }
    entry.archived_locally = true;
    plan.structure.entries.emplace(archive.id, std::move(entry));
    structure_changed = true;
  }
  if (structure_changed) {
    plan.structure.clock = clock.last();
    plan.changed = true;
  }
  const auto encoded = EncodeWorkspaceStructureState(plan.structure);
  if (!encoded) {
    return std::nullopt;
  }
  tab_tree::TabTreeStore validator;
  tab_tree::TabTreeStore::PersistenceSnapshot candidate{
      .tree = plan.tree, .workspace_structure_state = *encoded};
  if (!validator.InitializeInMemory() ||
      validator.ReplacePersistenceSnapshot(candidate) !=
          tab_tree::TabTreeStore::Result::kOk ||
      validator.ExportSnapshot(&plan.tree) !=
          tab_tree::TabTreeStore::Result::kOk) {
    return std::nullopt;
  }
  plan.encoded_structure = *encoded;
  return plan;
}

}  // namespace ahoi::session
