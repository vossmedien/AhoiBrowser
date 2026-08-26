// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstddef>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "base/check.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

namespace {

constexpr char kSelectActiveWorkspaceNodesSql[] =
    "SELECT model_version,id,workspace_id,parent_id,node_type,title,icon,"
    "accent_argb,url,sort_key,created_at,modified_at,tombstone FROM "
    "tree_nodes WHERE workspace_id=? AND tombstone=0 ORDER BY id";

constexpr char kInsertWorkspaceSql[] =
    "INSERT INTO workspaces(model_version,id,name,icon,sort_key,accent_argb,"
    "created_at,modified_at,tombstone) VALUES(?,?,?,?,?,?,?,?,?)";

constexpr char kInsertNodeSql[] =
    "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
    "node_type,title,icon,accent_argb,url,sort_key,created_at,modified_at,"
    "tombstone) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";

}  // namespace

TabTreeStore::Result TabTreeStore::DuplicateWorkspace(
    const base::Uuid& source_workspace_id,
    const Workspace& duplicate_workspace) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!source_workspace_id.is_valid() ||
      !ValidateWorkspace(duplicate_workspace) ||
      duplicate_workspace.tombstone) {
    return Result::kInvalidArgument;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }

  Workspace source_workspace;
  const Result source_result =
      ReadWorkspace(source_workspace_id, &source_workspace);
  if (source_result != Result::kOk) {
    return source_result;
  }
  if (source_workspace.tombstone) {
    return Result::kNotFound;
  }

  Workspace existing_workspace;
  const Result destination_result =
      ReadWorkspace(duplicate_workspace.id, &existing_workspace);
  if (destination_result == Result::kOk) {
    return Result::kAlreadyExists;
  }
  if (destination_result != Result::kNotFound) {
    return destination_result;
  }

  sql::Statement source_nodes(
      db_.GetUniqueStatement(kSelectActiveWorkspaceNodesSql));
  source_nodes.BindString(0, source_workspace_id.AsLowercaseString());

  std::vector<TreeNode> nodes;
  std::unordered_map<base::Uuid, size_t, base::UuidHash> node_indexes;
  while (source_nodes.Step()) {
    TreeNode node;
    if (!internal::DecodeNode(source_nodes, &node) || !ValidateNode(node) ||
        node.tombstone || node.workspace_id != source_workspace_id ||
        !node_indexes.emplace(node.id, nodes.size()).second) {
      return Result::kDatabaseError;
    }
    nodes.push_back(std::move(node));
  }
  if (!source_nodes.Succeeded()) {
    return Result::kDatabaseError;
  }

  // Assign generated IDs in source-ID order. The visible child order is
  // sort_key,id, so this preserves the old ID tie-breaker for equal sort keys.
  std::vector<base::Uuid> generated_ids;
  generated_ids.reserve(nodes.size());
  std::unordered_set<base::Uuid, base::UuidHash> generated_id_set;
  generated_id_set.reserve(nodes.size());
  while (generated_ids.size() < nodes.size()) {
    base::Uuid generated_id = base::Uuid::GenerateRandomV4();
    if (generated_id_set.insert(generated_id).second) {
      generated_ids.push_back(std::move(generated_id));
    }
  }
  std::sort(generated_ids.begin(), generated_ids.end(),
            [](const base::Uuid& left, const base::Uuid& right) {
              return left.AsLowercaseString() < right.AsLowercaseString();
            });

  std::unordered_map<base::Uuid, base::Uuid, base::UuidHash> remapped_ids;
  remapped_ids.reserve(nodes.size());
  for (size_t index = 0; index < nodes.size(); ++index) {
    remapped_ids.emplace(nodes[index].id, generated_ids[index]);
  }

  std::vector<size_t> indegrees(nodes.size());
  std::unordered_map<base::Uuid, std::vector<size_t>, base::UuidHash>
      children_by_parent;
  children_by_parent.reserve(nodes.size());
  for (size_t index = 0; index < nodes.size(); ++index) {
    const TreeNode& node = nodes[index];
    if (!node.parent_id.has_value()) {
      continue;
    }
    const auto parent = node_indexes.find(*node.parent_id);
    if (parent == node_indexes.end() ||
        nodes[parent->second].type != TreeNodeType::kFolder) {
      return Result::kDatabaseError;
    }
    indegrees[index] = 1;
    children_by_parent[nodes[parent->second].id].push_back(index);
  }

  std::deque<size_t> ready;
  for (size_t index = 0; index < indegrees.size(); ++index) {
    if (indegrees[index] == 0) {
      ready.push_back(index);
    }
  }
  std::vector<size_t> insertion_order;
  insertion_order.reserve(nodes.size());
  while (!ready.empty()) {
    const size_t index = ready.front();
    ready.pop_front();
    insertion_order.push_back(index);
    const auto children = children_by_parent.find(nodes[index].id);
    if (children == children_by_parent.end()) {
      continue;
    }
    for (size_t child_index : children->second) {
      if (--indegrees[child_index] == 0) {
        ready.push_back(child_index);
      }
    }
  }
  if (insertion_order.size() != nodes.size()) {
    return Result::kDatabaseError;
  }

  sql::Statement insert_workspace(
      db_.GetCachedStatement(SQL_FROM_HERE, kInsertWorkspaceSql));
  internal::BindWorkspaceForInsert(insert_workspace, duplicate_workspace);
  if (!insert_workspace.Run()) {
    return Result::kDatabaseError;
  }

  sql::Statement insert_node(
      db_.GetCachedStatement(SQL_FROM_HERE, kInsertNodeSql));
  for (size_t index : insertion_order) {
    TreeNode duplicate_node = nodes[index];
    const auto remapped_node_id = remapped_ids.find(duplicate_node.id);
    if (remapped_node_id == remapped_ids.end()) {
      return Result::kDatabaseError;
    }
    duplicate_node.id = remapped_node_id->second;
    duplicate_node.workspace_id = duplicate_workspace.id;
    if (duplicate_node.parent_id.has_value()) {
      const auto remapped_parent_id =
          remapped_ids.find(*duplicate_node.parent_id);
      if (remapped_parent_id == remapped_ids.end()) {
        return Result::kDatabaseError;
      }
      duplicate_node.parent_id = remapped_parent_id->second;
    }
    insert_node.Reset(/*clear_bound_vars=*/true);
    internal::BindNodeForInsert(insert_node, duplicate_node);
    if (!insert_node.Run()) {
      return Result::kDatabaseError;
    }
  }

  return transaction.Commit() ? Result::kOk : Result::kDatabaseError;
}

}  // namespace ahoi::tab_tree
