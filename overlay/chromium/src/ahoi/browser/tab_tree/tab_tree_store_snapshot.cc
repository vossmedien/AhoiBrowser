// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <cstddef>
#include <deque>
#include <set>
#include <unordered_map>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "base/check.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

TabTreeStore::Result TabTreeStore::ReplaceWithSnapshot(
    const TabTreeSnapshot& snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }

  std::unordered_map<base::Uuid, const Workspace*, base::UuidHash> workspaces;
  workspaces.reserve(snapshot.workspaces.size());
  for (const Workspace& workspace : snapshot.workspaces) {
    if (!ValidateWorkspace(workspace) ||
        !workspaces.emplace(workspace.id, &workspace).second) {
      return Result::kInvalidArgument;
    }
  }

  std::unordered_map<base::Uuid, size_t, base::UuidHash> node_indexes;
  node_indexes.reserve(snapshot.nodes.size());
  for (size_t index = 0; index < snapshot.nodes.size(); ++index) {
    const TreeNode& node = snapshot.nodes[index];
    if (!ValidateNode(node) || !workspaces.contains(node.workspace_id) ||
        !node_indexes.emplace(node.id, index).second) {
      return Result::kInvalidArgument;
    }
  }

  std::vector<size_t> indegrees(snapshot.nodes.size());
  std::unordered_map<base::Uuid, std::vector<size_t>, base::UuidHash>
      children_by_parent;
  children_by_parent.reserve(snapshot.nodes.size());
  for (size_t index = 0; index < snapshot.nodes.size(); ++index) {
    const TreeNode& node = snapshot.nodes[index];
    if (!node.parent_id.has_value()) {
      continue;
    }
    auto parent = node_indexes.find(*node.parent_id);
    if (parent == node_indexes.end()) {
      return Result::kInvalidArgument;
    }
    const TreeNode& parent_node = snapshot.nodes[parent->second];
    if (parent_node.type != TreeNodeType::kFolder ||
        parent_node.workspace_id != node.workspace_id) {
      return Result::kInvalidArgument;
    }
    indegrees[index] = 1;
    children_by_parent[parent_node.id].push_back(index);
  }

  std::deque<size_t> ready;
  for (size_t index = 0; index < indegrees.size(); ++index) {
    if (indegrees[index] == 0) {
      ready.push_back(index);
    }
  }
  std::vector<size_t> insertion_order;
  insertion_order.reserve(snapshot.nodes.size());
  while (!ready.empty()) {
    const size_t index = ready.front();
    ready.pop_front();
    insertion_order.push_back(index);
    auto children = children_by_parent.find(snapshot.nodes[index].id);
    if (children == children_by_parent.end()) {
      continue;
    }
    for (size_t child_index : children->second) {
      if (--indegrees[child_index] == 0) {
        ready.push_back(child_index);
      }
    }
  }
  if (insertion_order.size() != snapshot.nodes.size()) {
    return Result::kCycle;
  }

  std::set<int64_t> operation_ids;
  for (const UndoOperationSnapshot& operation : snapshot.undo_operations) {
    switch (operation.kind) {
      case UndoMutationKind::kCreate:
      case UndoMutationKind::kRename:
      case UndoMutationKind::kMove:
      case UndoMutationKind::kDelete:
        break;
      default:
        return Result::kInvalidArgument;
    }
    if (operation.operation_id <= 0 ||
        !operation_ids.insert(operation.operation_id).second ||
        !operation.subject_node_id.is_valid() ||
        operation.created_at.is_null() || operation.nodes.empty()) {
      return Result::kInvalidArgument;
    }
    for (const UndoNodeSnapshot& node_snapshot : operation.nodes) {
      if (!node_snapshot.node_id.is_valid() ||
          (node_snapshot.previous.has_value() &&
           (node_snapshot.previous->id != node_snapshot.node_id ||
            !ValidateNode(*node_snapshot.previous) ||
            !workspaces.contains(node_snapshot.previous->workspace_id)))) {
        return Result::kInvalidArgument;
      }
    }
  }

  sql::Transaction transaction(&db_);
  // Parent rows normally precede their children in the persisted snapshot.
  // ON DELETE RESTRICT is checked per row, so a bulk DELETE cannot safely
  // remove that self-referencing tree. Detach the old edges inside the same
  // transaction before clearing it. Foreign keys stay enabled throughout;
  // any later failure rolls back both the edges and all other old state.
  if (!transaction.Begin() ||
      !db_.Execute("UPDATE tree_nodes SET parent_id=NULL "
                   "WHERE parent_id IS NOT NULL") ||
      !db_.Execute("DELETE FROM undo_node_snapshots") ||
      !db_.Execute("DELETE FROM undo_operations") ||
      !db_.Execute("DELETE FROM tree_nodes") ||
      !db_.Execute("DELETE FROM workspaces")) {
    return Result::kDatabaseError;
  }

  sql::Statement insert_workspace(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO workspaces(model_version,id,name,icon,sort_key,accent_argb,"
      "created_at,modified_at,tombstone) VALUES(?,?,?,?,?,?,?,?,?)"));
  for (const Workspace &workspace : snapshot.workspaces) {
    insert_workspace.Reset(/*clear_bound_vars=*/true);
    internal::BindWorkspaceForInsert(insert_workspace, workspace);
    if (!insert_workspace.Run()) {
      return Result::kDatabaseError;
    }
  }

  sql::Statement insert_node(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
      "node_type,title,icon,accent_argb,url,sort_key,created_at,modified_at,"
      "tombstone) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  for (size_t index : insertion_order) {
    insert_node.Reset(/*clear_bound_vars=*/true);
    internal::BindNodeForInsert(insert_node, snapshot.nodes[index]);
    if (!insert_node.Run()) {
      return Result::kDatabaseError;
    }
  }

  sql::Statement insert_operation(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO undo_operations(operation_id,mutation_kind,"
      "subject_node_id,created_at) VALUES(?,?,?,?)"));
  sql::Statement insert_undo_node(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO undo_node_snapshots(operation_id,ordinal,existed,node_id,"
      "model_version,workspace_id,parent_id,node_type,title,icon,accent_argb,"
      "url,sort_key,created_at,modified_at,tombstone) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  for (const UndoOperationSnapshot& operation : snapshot.undo_operations) {
    insert_operation.Reset(/*clear_bound_vars=*/true);
    insert_operation.BindInt64(0, operation.operation_id);
    insert_operation.BindInt(1, static_cast<int>(operation.kind));
    insert_operation.BindString(2,
                                operation.subject_node_id.AsLowercaseString());
    insert_operation.BindTime(3, operation.created_at);
    if (!insert_operation.Run()) {
      return Result::kDatabaseError;
    }
    for (size_t ordinal = 0; ordinal < operation.nodes.size(); ++ordinal) {
      const UndoNodeSnapshot& node_snapshot = operation.nodes[ordinal];
      insert_undo_node.Reset(/*clear_bound_vars=*/true);
      insert_undo_node.BindInt64(0, operation.operation_id);
      insert_undo_node.BindInt64(1, static_cast<int64_t>(ordinal));
      insert_undo_node.BindBool(2, node_snapshot.previous.has_value());
      insert_undo_node.BindString(3, node_snapshot.node_id.AsLowercaseString());
      if (!node_snapshot.previous.has_value()) {
        for (int column = 4; column <= 15; ++column) {
          insert_undo_node.BindNull(column);
        }
      } else {
        const TreeNode& node = *node_snapshot.previous;
        insert_undo_node.BindInt(4, node.model_version);
        insert_undo_node.BindString(5, node.workspace_id.AsLowercaseString());
        if (node.parent_id.has_value()) {
          insert_undo_node.BindString(6, node.parent_id->AsLowercaseString());
        } else {
          insert_undo_node.BindNull(6);
        }
        insert_undo_node.BindInt(7, static_cast<int>(node.type));
        insert_undo_node.BindString16(8, node.title);
        insert_undo_node.BindString16(9, node.icon);
        if (node.accent_argb.has_value()) {
          insert_undo_node.BindInt64(10, *node.accent_argb);
        } else {
          insert_undo_node.BindNull(10);
        }
        insert_undo_node.BindString(11, node.url.spec());
        insert_undo_node.BindString(12, node.sort_key);
        insert_undo_node.BindTime(13, node.created_at);
        insert_undo_node.BindTime(14, node.modified_at);
        insert_undo_node.BindBool(15, node.tombstone);
      }
      if (!insert_undo_node.Run()) {
        return Result::kDatabaseError;
      }
    }
  }

  return transaction.Commit() ? Result::kOk : Result::kDatabaseError;
}

}  // namespace ahoi::tab_tree
