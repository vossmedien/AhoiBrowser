// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/check.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

TabTreeStore::Result TabTreeStore::MoveNode(const base::Uuid& node_id,
                                            const base::Uuid& workspace_id,
                                            std::optional<base::Uuid> parent_id,
                                            std::string sort_key,
                                            base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || !workspace_id.is_valid() || sort_key.empty() ||
      modified_at.is_null() ||
      (parent_id.has_value() && !parent_id->is_valid())) {
    return Result::kInvalidArgument;
  }

  TreeNode node;
  Result result = ReadNode(node_id, &node);
  if (result != Result::kOk) {
    return result;
  }
  if (node.tombstone) {
    return Result::kNotFound;
  }
  result = ValidateDestination(node_id, workspace_id, parent_id);
  if (result != Result::kOk) {
    return result;
  }
  if (node.workspace_id == workspace_id && node.parent_id == parent_id &&
      node.sort_key == sort_key) {
    return Result::kOk;
  }

  const bool changes_workspace = node.workspace_id != workspace_id;
  std::vector<TreeNode> affected_nodes;
  if (changes_workspace) {
    result = ReadSubtree(node_id, &affected_nodes);
    if (result != Result::kOk) {
      return result;
    }
  } else {
    affected_nodes.push_back(node);
  }
  std::vector<NodeSnapshot> snapshots;
  snapshots.reserve(affected_nodes.size());
  for (const TreeNode& affected_node : affected_nodes) {
    snapshots.push_back(
        {.node_id = affected_node.id, .previous = affected_node});
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  if (!InsertUndoOperation(UndoMutationKind::kMove, node.id, modified_at,
                           snapshots)) {
    return Result::kDatabaseError;
  }

  if (changes_workspace) {
    sql::Statement update_descendants(db_.GetUniqueStatement(
        "WITH RECURSIVE subtree(id) AS (SELECT id FROM tree_nodes WHERE id=? "
        "UNION SELECT child.id FROM tree_nodes child JOIN subtree parent ON "
        "child.parent_id=parent.id) UPDATE tree_nodes SET workspace_id=?,"
        "modified_at=? WHERE id IN (SELECT id FROM subtree) AND id<>?"));
    update_descendants.BindString(0, node.id.AsLowercaseString());
    update_descendants.BindString(1, workspace_id.AsLowercaseString());
    update_descendants.BindTime(2, modified_at);
    update_descendants.BindString(3, node.id.AsLowercaseString());
    if (!update_descendants.Run() ||
        db_.GetLastChangeCount() !=
            static_cast<int64_t>(affected_nodes.size() - 1)) {
      return Result::kDatabaseError;
    }
  }

  sql::Statement update_root(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET workspace_id=?,parent_id=?,sort_key=?,"
      "modified_at=? WHERE id=?"));
  update_root.BindString(0, workspace_id.AsLowercaseString());
  if (parent_id.has_value()) {
    update_root.BindString(1, parent_id->AsLowercaseString());
  } else {
    update_root.BindNull(1);
  }
  update_root.BindString(2, sort_key);
  update_root.BindTime(3, modified_at);
  update_root.BindString(4, node.id.AsLowercaseString());
  if (!update_root.Run() || db_.GetLastChangeCount() != 1 ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  std::vector<base::Uuid> changed_ids;
  if (changes_workspace) {
    changed_ids.reserve(affected_nodes.size());
    for (const TreeNode& affected_node : affected_nodes) {
      changed_ids.push_back(affected_node.id);
    }
  } else {
    changed_ids.push_back(node.id);
  }
  Notify(MutationKind::kMoved, node.id, std::move(changed_ids));
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::MoveSavedPagesAtomically(
    const std::vector<SavedPageMove>& moves,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (moves.empty() || modified_at.is_null()) {
    return Result::kInvalidArgument;
  }

  std::unordered_set<base::Uuid, base::UuidHash> unique_ids;
  unique_ids.reserve(moves.size());
  std::vector<TreeNode> nodes;
  nodes.reserve(moves.size());
  std::vector<size_t> changed_indices;
  changed_indices.reserve(moves.size());
  for (size_t index = 0; index < moves.size(); ++index) {
    const SavedPageMove& move = moves[index];
    if (!move.node_id.is_valid() || !move.workspace_id.is_valid() ||
        move.sort_key.empty() ||
        (move.parent_id.has_value() && !move.parent_id->is_valid()) ||
        !unique_ids.insert(move.node_id).second) {
      return Result::kInvalidArgument;
    }
    TreeNode node;
    Result result = ReadNode(move.node_id, &node);
    if (result != Result::kOk) {
      return result;
    }
    if (node.tombstone) {
      return Result::kNotFound;
    }
    if (node.type != TreeNodeType::kSavedPage) {
      return Result::kInvalidArgument;
    }
    result =
        ValidateDestination(move.node_id, move.workspace_id, move.parent_id);
    if (result != Result::kOk) {
      return result;
    }
    if (node.workspace_id != move.workspace_id ||
        node.parent_id != move.parent_id || node.sort_key != move.sort_key) {
      changed_indices.push_back(index);
    }
    nodes.push_back(std::move(node));
  }
  if (changed_indices.empty()) {
    return Result::kOk;
  }

  std::vector<NodeSnapshot> snapshots;
  snapshots.reserve(changed_indices.size());
  for (size_t index : changed_indices) {
    snapshots.push_back({.node_id = nodes[index].id, .previous = nodes[index]});
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  const base::Uuid subject_node_id = nodes[changed_indices.front()].id;
  if (!InsertUndoOperation(UndoMutationKind::kMove, subject_node_id,
                           modified_at, snapshots)) {
    return Result::kDatabaseError;
  }

  sql::Statement update(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET workspace_id=?,parent_id=?,sort_key=?,"
      "modified_at=? WHERE id=?"));
  for (size_t index : changed_indices) {
    const SavedPageMove& move = moves[index];
    update.Reset(/*clear_bound_vars=*/true);
    update.BindString(0, move.workspace_id.AsLowercaseString());
    if (move.parent_id.has_value()) {
      update.BindString(1, move.parent_id->AsLowercaseString());
    } else {
      update.BindNull(1);
    }
    update.BindString(2, move.sort_key);
    update.BindTime(3, modified_at);
    update.BindString(4, move.node_id.AsLowercaseString());
    if (!update.Run() || db_.GetLastChangeCount() != 1) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }

  std::vector<base::Uuid> changed_ids;
  changed_ids.reserve(changed_indices.size());
  for (size_t index : changed_indices) {
    changed_ids.push_back(nodes[index].id);
  }
  Notify(MutationKind::kMoved, subject_node_id, std::move(changed_ids));
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::DeleteNode(const base::Uuid& node_id,
                                              base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || modified_at.is_null()) {
    return Result::kInvalidArgument;
  }

  TreeNode node;
  Result result = ReadNode(node_id, &node);
  if (result != Result::kOk) {
    return result;
  }
  if (node.tombstone) {
    return Result::kNotFound;
  }

  std::vector<TreeNode> subtree;
  result = ReadSubtree(node_id, &subtree);
  if (result != Result::kOk) {
    return result;
  }
  std::vector<NodeSnapshot> snapshots;
  snapshots.reserve(subtree.size());
  for (const TreeNode& descendant : subtree) {
    snapshots.push_back({.node_id = descendant.id, .previous = descendant});
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  if (!InsertUndoOperation(UndoMutationKind::kDelete, node.id, modified_at,
                           snapshots)) {
    return Result::kDatabaseError;
  }

  std::vector<base::Uuid> changed_ids;
  changed_ids.reserve(subtree.size());
  for (const TreeNode& descendant : subtree) {
    changed_ids.push_back(descendant.id);
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "WITH RECURSIVE subtree(id) AS (SELECT id FROM tree_nodes WHERE id=? "
      "UNION SELECT child.id FROM tree_nodes child JOIN subtree parent ON "
      "child.parent_id=parent.id) UPDATE tree_nodes SET tombstone=1,"
      "modified_at=? WHERE id IN (SELECT id FROM subtree)"));
  statement.BindString(0, node.id.AsLowercaseString());
  statement.BindTime(1, modified_at);
  if (!statement.Run() ||
      db_.GetLastChangeCount() != static_cast<int64_t>(subtree.size())) {
    return Result::kDatabaseError;
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }

  Notify(MutationKind::kDeleted, node.id, std::move(changed_ids));
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::DeleteNodesAtomically(
    const std::vector<base::Uuid>& node_ids,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (node_ids.empty() || modified_at.is_null()) {
    return Result::kInvalidArgument;
  }

  std::unordered_set<base::Uuid, base::UuidHash> requested_roots;
  std::unordered_set<base::Uuid, base::UuidHash> affected_ids;
  std::vector<std::vector<TreeNode>> subtrees;
  std::vector<NodeSnapshot> snapshots;
  subtrees.reserve(node_ids.size());
  for (const base::Uuid& node_id : node_ids) {
    if (!node_id.is_valid() || !requested_roots.insert(node_id).second) {
      return Result::kInvalidArgument;
    }
    TreeNode root;
    Result result = ReadNode(node_id, &root);
    if (result != Result::kOk) {
      return result;
    }
    if (root.tombstone) {
      return Result::kNotFound;
    }
    std::vector<TreeNode> subtree;
    result = ReadSubtree(node_id, &subtree);
    if (result != Result::kOk) {
      return result;
    }
    for (const TreeNode& descendant : subtree) {
      // Overlapping roots would make the affected-row count and undo payload
      // ambiguous. Callers must provide the highest disjoint roots instead.
      if (!affected_ids.insert(descendant.id).second) {
        return Result::kInvalidArgument;
      }
      snapshots.push_back({.node_id = descendant.id, .previous = descendant});
    }
    subtrees.push_back(std::move(subtree));
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  if (!InsertUndoOperation(UndoMutationKind::kDelete, node_ids.front(),
                           modified_at, snapshots)) {
    return Result::kDatabaseError;
  }

  sql::Statement statement(db_.GetUniqueStatement(
      "WITH RECURSIVE subtree(id) AS (SELECT id FROM tree_nodes WHERE id=? "
      "UNION SELECT child.id FROM tree_nodes child JOIN subtree parent ON "
      "child.parent_id=parent.id) UPDATE tree_nodes SET tombstone=1,"
      "modified_at=? WHERE id IN (SELECT id FROM subtree)"));
  for (size_t index = 0; index < node_ids.size(); ++index) {
    statement.Reset(/*clear_bound_vars=*/true);
    statement.BindString(0, node_ids[index].AsLowercaseString());
    statement.BindTime(1, modified_at);
    if (!statement.Run() || db_.GetLastChangeCount() !=
                                static_cast<int64_t>(subtrees[index].size())) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }

  std::vector<base::Uuid> changed_ids;
  changed_ids.reserve(snapshots.size());
  for (const NodeSnapshot& snapshot : snapshots) {
    changed_ids.push_back(snapshot.node_id);
  }
  Notify(MutationKind::kDeleted, node_ids.front(), std::move(changed_ids));
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::UndoLastMutation() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }

  int64_t operation_id = 0;
  base::Uuid subject_node_id;
  {
    sql::Statement operation(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT operation_id,subject_node_id FROM undo_operations ORDER BY "
        "operation_id DESC LIMIT 1"));
    if (!operation.Step()) {
      return operation.Succeeded() ? Result::kNothingToUndo
                                   : Result::kDatabaseError;
    }
    operation_id = operation.ColumnInt64(0);
    subject_node_id = base::Uuid::ParseLowercase(operation.ColumnString(1));
    if (!subject_node_id.is_valid()) {
      return Result::kDatabaseError;
    }
  }

  std::vector<NodeSnapshot> snapshots;
  if (!ReadUndoSnapshots(operation_id, &snapshots) || snapshots.empty() ||
      std::ranges::none_of(snapshots, [&subject_node_id](const auto& snapshot) {
        return snapshot.node_id == subject_node_id;
      })) {
    return Result::kDatabaseError;
  }
  for (const NodeSnapshot& snapshot : snapshots) {
    if (!RestoreSnapshot(snapshot)) {
      return Result::kDatabaseError;
    }
  }
  if (!RemoveUndoOperation(operation_id) || !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  std::vector<base::Uuid> changed_ids;
  changed_ids.reserve(snapshots.size());
  for (const NodeSnapshot& snapshot : snapshots) {
    changed_ids.push_back(snapshot.node_id);
  }
  Notify(MutationKind::kUndone, subject_node_id, std::move(changed_ids));
  return Result::kOk;
}

}  // namespace ahoi::tab_tree
