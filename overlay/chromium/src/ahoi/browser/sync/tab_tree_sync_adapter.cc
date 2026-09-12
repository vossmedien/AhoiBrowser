// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/tab_tree_sync_adapter.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "ahoi/browser/tab_tree/shared_tab_target_policy.h"
#include "base/containers/span.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "crypto/sha2.h"

namespace ahoi::sync {
namespace {

base::Uuid RecoveryFolderId(const base::Uuid& workspace_id) {
  std::string bytes = crypto::SHA256HashString(
      base::StrCat({"ahoi-sync-recovered:", workspace_id.AsLowercaseString()}));
  bytes[6] = static_cast<char>((static_cast<uint8_t>(bytes[6]) & 0x0f) | 0x40);
  bytes[8] = static_cast<char>((static_cast<uint8_t>(bytes[8]) & 0x3f) | 0x80);
  std::string hex = base::ToLowerASCII(
      base::HexEncode(base::as_byte_span(bytes).first<16>()));
  return base::Uuid::ParseLowercase(base::StrCat(
      {hex.substr(0, 8), "-", hex.substr(8, 4), "-", hex.substr(12, 4), "-",
       hex.substr(16, 4), "-", hex.substr(20, 12)}));
}

bool WorkspaceOrder(const tab_tree::Workspace* left,
                    const tab_tree::Workspace* right) {
  return std::tie(left->sort_key, left->id) <
         std::tie(right->sort_key, right->id);
}

tab_tree::Workspace ConvertWorkspace(const WorkspaceRecord& source) {
  return {.model_version = tab_tree::kCurrentModelVersion,
          .id = source.id,
          .name = base::UTF8ToUTF16(source.name),
          .icon = base::UTF8ToUTF16(source.icon),
          .sort_key = source.sort_key,
          .accent_argb = source.accent_argb,
          .created_at = source.created_at,
          .modified_at = source.modified_at,
          .tombstone = source.tombstone,
          .archive_policy = source.archive_policy};
}

tab_tree::TreeNode ConvertNode(const TreeNodeRecord& source) {
  return {
      .model_version = tab_tree::kCurrentModelVersion,
      .id = source.id,
      .workspace_id = source.workspace_id,
      .parent_id = source.parent_id,
      .type = source.kind == TreeNodeKind::kFolder
                  ? tab_tree::TreeNodeType::kFolder
                  : tab_tree::TreeNodeType::kSavedPage,
      .title = base::UTF8ToUTF16(source.title),
      .icon = base::UTF8ToUTF16(source.icon),
      .accent_argb = source.accent_argb,
      .url = GURL(source.url),
      .sort_key = source.sort_key,
      .created_at = source.created_at,
      .modified_at = source.modified_at,
      .tombstone = source.tombstone,
      .is_temporary = source.is_temporary,
      .target_kind = source.target_kind,
      .local_scheme = source.local_scheme,
      .home_url = source.home_target ? GURL(source.home_target->url) : GURL(),
      .home_target_kind = source.home_target
                              ? std::make_optional(source.home_target->kind)
                              : std::nullopt,
      .home_local_scheme =
          source.home_target ? source.home_target->local_scheme : std::nullopt};
}

}  // namespace

WorkspaceRecord WorkspaceToSyncRecord(const tab_tree::Workspace& workspace,
                                      SyncVersion version) {
  return {.id = workspace.id,
          .name = base::UTF16ToUTF8(workspace.name),
          .icon = base::UTF16ToUTF8(workspace.icon),
          .sort_key = workspace.sort_key,
          .accent_argb = workspace.accent_argb,
          .created_at = workspace.created_at,
          .modified_at = workspace.modified_at,
          .tombstone = workspace.tombstone,
          .version = std::move(version),
          .archive_policy = workspace.archive_policy};
}

TreeNodeRecord TreeNodeToSyncRecord(const tab_tree::TreeNode& node,
                                    SyncVersion version) {
  const auto target = tab_tree::GetSharedPageTarget(node);
  return {
      .id = node.id,
      .workspace_id = node.workspace_id,
      .parent_id = node.parent_id,
      .kind = node.type == tab_tree::TreeNodeType::kFolder
                  ? TreeNodeKind::kFolder
                  : TreeNodeKind::kPage,
      .title = base::UTF16ToUTF8(tab_tree::GetSharedPageTitle(node)),
      .icon = base::UTF16ToUTF8(node.icon),
      .accent_argb = node.accent_argb,
      .url = target ? target->url : std::string(),
      .sort_key = node.sort_key,
      .created_at = node.created_at,
      .modified_at = node.modified_at,
      .tombstone = node.tombstone,
      .version = std::move(version),
      .is_temporary = node.is_temporary,
      .target_kind = target ? std::make_optional(target->kind) : std::nullopt,
      .local_scheme = target ? target->local_scheme : std::nullopt,
      .home_target = tab_tree::GetSharedHomeTarget(node)};
}

std::optional<tab_tree::TabTreeSnapshot> ReconcileTabTreeRecords(
    const tab_tree::TabTreeSnapshot& local_snapshot,
    const std::vector<WorkspaceRecord>& workspaces,
    const std::vector<TreeNodeRecord>& nodes) {
  tab_tree::TabTreeSnapshot result;
  result.undo_operations = local_snapshot.undo_operations;
  std::map<base::Uuid, size_t> workspace_indexes;
  for (const WorkspaceRecord& source : workspaces) {
    if (!source.id.is_valid() || source.name.empty() ||
        source.sort_key.empty() || source.created_at.is_null() ||
        source.modified_at.is_null() ||
        static_cast<int>(source.archive_policy) < 0 ||
        static_cast<int>(source.archive_policy) > 4 ||
        workspace_indexes.contains(source.id)) {
      return std::nullopt;
    }
    workspace_indexes[source.id] = result.workspaces.size();
    result.workspaces.push_back(ConvertWorkspace(source));
  }

  std::vector<const tab_tree::Workspace*> active;
  for (const tab_tree::Workspace& workspace : result.workspaces) {
    if (!workspace.tombstone)
      active.push_back(&workspace);
  }
  if (active.empty()) {
    for (const tab_tree::Workspace& workspace : local_snapshot.workspaces) {
      if (!workspace.tombstone && workspace.id.is_valid() &&
          !workspace_indexes.contains(workspace.id)) {
        workspace_indexes[workspace.id] = result.workspaces.size();
        result.workspaces.push_back(workspace);
        active.push_back(&result.workspaces.back());
        break;
      }
    }
  }
  if (active.empty())
    return std::nullopt;
  // Pointers can have been invalidated by the fallback append above.
  active.clear();
  for (const tab_tree::Workspace& workspace : result.workspaces) {
    if (!workspace.tombstone)
      active.push_back(&workspace);
  }
  std::sort(active.begin(), active.end(), WorkspaceOrder);
  const base::Uuid fallback_workspace = active.front()->id;

  std::map<base::Uuid, size_t> node_indexes;
  std::map<base::Uuid, const tab_tree::TreeNode*> local_nodes;
  for (const auto& node : local_snapshot.nodes) {
    local_nodes.emplace(node.id, &node);
  }
  std::set<base::Uuid> force_recovery;
  for (const TreeNodeRecord& source : nodes) {
    if (!source.id.is_valid() || source.title.empty() ||
        source.sort_key.empty() || source.created_at.is_null() ||
        source.modified_at.is_null() || node_indexes.contains(source.id)) {
      return std::nullopt;
    }
    tab_tree::TreeNode node = ConvertNode(source);
    if (source.kind == TreeNodeKind::kFolder) {
      if (source.is_temporary || source.target_kind || source.local_scheme ||
          !source.url.empty() || source.home_target) {
        return std::nullopt;
      }
    } else if (source.kind != TreeNodeKind::kPage || !source.target_kind ||
               !tab_tree::IsValidSharedPageTarget(
                   {.kind = *source.target_kind,
                    .url = source.url,
                    .local_scheme = source.local_scheme},
                   source.is_temporary)) {
      return std::nullopt;
    }
    if (source.home_target &&
        !tab_tree::IsValidSharedPageTarget(*source.home_target, false)) {
      return std::nullopt;
    }
    if (node.home_target_kind == SharedTabTargetKind::kLocalOnly) {
      const auto local = local_nodes.find(node.id);
      if (local != local_nodes.end() && !local->second->tombstone &&
          tab_tree::GetSharedHomeTarget(*local->second) == source.home_target) {
        node.home_url = local->second->home_url;
      }
    }
    if (node.target_kind == SharedTabTargetKind::kLocalOnly ||
        node.target_kind == SharedTabTargetKind::kNewTab) {
      const auto local = local_nodes.find(node.id);
      if (local != local_nodes.end() && !local->second->tombstone &&
          local->second->type == tab_tree::TreeNodeType::kSavedPage) {
        if (!local->second->url.is_empty() && local->second->url.is_valid()) {
          // An empty portable URL is not an instruction to clear this device's
          // actual destination. Runtime navigation stays separate and lazy.
          node.url = local->second->url;
          node.title = local->second->title;
        }
      }
    }
    auto workspace = workspace_indexes.find(node.workspace_id);
    if (workspace == workspace_indexes.end() ||
        (!node.tombstone && result.workspaces[workspace->second].tombstone)) {
      node.workspace_id = fallback_workspace;
      if (!node.tombstone)
        force_recovery.insert(node.id);
    }
    node_indexes[node.id] = result.nodes.size();
    result.nodes.push_back(std::move(node));
  }

  std::set<base::Uuid> recovery_workspaces;
  auto recover = [&](tab_tree::TreeNode& node) {
    if (node.tombstone) {
      node.parent_id.reset();
      return;
    }
    recovery_workspaces.insert(node.workspace_id);
    node.parent_id = RecoveryFolderId(node.workspace_id);
  };

  for (const base::Uuid& id : force_recovery) {
    recover(result.nodes[node_indexes.at(id)]);
  }

  for (tab_tree::TreeNode& node : result.nodes) {
    if (!node.parent_id)
      continue;
    const auto parent = node_indexes.find(*node.parent_id);
    if (parent == node_indexes.end()) {
      recover(node);
      continue;
    }
    const tab_tree::TreeNode& parent_node = result.nodes[parent->second];
    if (parent_node.tombstone ||
        parent_node.type != tab_tree::TreeNodeType::kFolder ||
        parent_node.workspace_id != node.workspace_id) {
      recover(node);
    }
  }

  // Cut each cycle at its lexicographically smallest identity. Re-run until
  // all parent chains terminate; each pass removes at least one cycle edge.
  for (;;) {
    std::map<base::Uuid, uint8_t> colors;
    std::vector<base::Uuid> stack;
    bool cut = false;
    std::function<void(const base::Uuid&)> visit = [&](const base::Uuid& id) {
      if (cut || colors[id] == 2)
        return;
      if (colors[id] == 1) {
        auto start = std::find(stack.begin(), stack.end(), id);
        const base::Uuid cut_id = *std::min_element(start, stack.end());
        recover(result.nodes[node_indexes.at(cut_id)]);
        cut = true;
        return;
      }
      colors[id] = 1;
      stack.push_back(id);
      const tab_tree::TreeNode& node = result.nodes[node_indexes.at(id)];
      if (node.parent_id && node_indexes.contains(*node.parent_id)) {
        visit(*node.parent_id);
      }
      stack.pop_back();
      colors[id] = 2;
    };
    for (const auto& [id, index] : node_indexes) {
      visit(id);
      if (cut)
        break;
    }
    if (!cut)
      break;
  }

  for (const base::Uuid& workspace_id : recovery_workspaces) {
    const base::Uuid folder_id = RecoveryFolderId(workspace_id);
    if (const auto collision = node_indexes.find(folder_id);
        collision != node_indexes.end()) {
      const tab_tree::TreeNode& existing = result.nodes[collision->second];
      if (existing.type != tab_tree::TreeNodeType::kFolder ||
          existing.workspace_id != workspace_id || existing.tombstone) {
        return std::nullopt;
      }
      continue;
    }
    const base::Time created = base::Time::UnixEpoch();
    node_indexes[folder_id] = result.nodes.size();
    result.nodes.push_back({.id = folder_id,
                            .workspace_id = workspace_id,
                            .type = tab_tree::TreeNodeType::kFolder,
                            .title = u"Wiederhergestellt",
                            .icon = u"folder",
                            .sort_key = "~recovered",
                            .created_at = created,
                            .modified_at = created});
  }
  // Match the native export's ordering, including appended recovery folders.
  // Common's exact tree+receipt readback must not fail merely because provider
  // records arrived in another order or a fallback workspace was appended.
  std::ranges::sort(result.workspaces, [](const auto& left, const auto& right) {
    return std::tie(left.sort_key, left.id) <
           std::tie(right.sort_key, right.id);
  });
  std::ranges::sort(result.nodes, {}, &tab_tree::TreeNode::id);
  return result;
}

}  // namespace ahoi::sync
