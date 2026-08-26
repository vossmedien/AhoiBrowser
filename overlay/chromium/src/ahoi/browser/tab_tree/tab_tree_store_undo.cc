// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <algorithm>
#include <limits>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/check.h"
#include "sql/statement.h"

namespace ahoi::tab_tree {

bool TabTreeStore::InsertUndoOperation(
    UndoMutationKind kind,
    const base::Uuid& subject_node_id,
    base::Time created_at,
    const std::vector<NodeSnapshot>& snapshots) {
  sql::Statement operation(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO undo_operations(mutation_kind,subject_node_id,created_at) "
      "VALUES(?,?,?)"));
  operation.BindInt(0, static_cast<int>(kind));
  operation.BindString(1, subject_node_id.AsLowercaseString());
  operation.BindTime(2, created_at);
  if (!operation.Run()) {
    return false;
  }
  const int64_t operation_id = db_.GetLastInsertRowId();

  sql::Statement snapshot_statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO undo_node_snapshots(operation_id,ordinal,existed,node_id,"
      "model_version,workspace_id,parent_id,node_type,title,icon,accent_argb,"
      "url,sort_key,created_at,modified_at,tombstone) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  for (size_t i = 0; i < snapshots.size(); ++i) {
    const NodeSnapshot& snapshot = snapshots[i];
    snapshot_statement.Reset(/*clear_bound_vars=*/true);
    snapshot_statement.BindInt64(0, operation_id);
    snapshot_statement.BindInt64(1, static_cast<int64_t>(i));
    snapshot_statement.BindBool(2, snapshot.previous.has_value());
    snapshot_statement.BindString(3, snapshot.node_id.AsLowercaseString());
    if (!snapshot.previous.has_value()) {
      for (int column = 4; column <= 15; ++column) {
        snapshot_statement.BindNull(column);
      }
    } else {
      const TreeNode& node = *snapshot.previous;
      snapshot_statement.BindInt(4, node.model_version);
      snapshot_statement.BindString(5, node.workspace_id.AsLowercaseString());
      if (node.parent_id.has_value()) {
        snapshot_statement.BindString(6, node.parent_id->AsLowercaseString());
      } else {
        snapshot_statement.BindNull(6);
      }
      snapshot_statement.BindInt(7, static_cast<int>(node.type));
      snapshot_statement.BindString16(8, node.title);
      snapshot_statement.BindString16(9, node.icon);
      if (node.accent_argb.has_value()) {
        snapshot_statement.BindInt64(10, *node.accent_argb);
      } else {
        snapshot_statement.BindNull(10);
      }
      snapshot_statement.BindString(11, node.url.spec());
      snapshot_statement.BindString(12, node.sort_key);
      snapshot_statement.BindTime(13, node.created_at);
      snapshot_statement.BindTime(14, node.modified_at);
      snapshot_statement.BindBool(15, node.tombstone);
    }
    if (!snapshot_statement.Run()) {
      return false;
    }
  }
  return true;
}

bool TabTreeStore::RestoreSnapshot(const NodeSnapshot& snapshot) {
  if (!snapshot.previous.has_value()) {
    sql::Statement statement(db_.GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM tree_nodes WHERE id=?"));
    statement.BindString(0, snapshot.node_id.AsLowercaseString());
    return statement.Run() && db_.GetLastChangeCount() == 1;
  }

  const TreeNode& node = *snapshot.previous;
  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET model_version=?,workspace_id=?,parent_id=?,"
      "node_type=?,title=?,icon=?,accent_argb=?,url=?,sort_key=?,created_at=?,"
      "modified_at=?,tombstone=? WHERE id=?"));
  statement.BindInt(0, node.model_version);
  statement.BindString(1, node.workspace_id.AsLowercaseString());
  if (node.parent_id.has_value()) {
    statement.BindString(2, node.parent_id->AsLowercaseString());
  } else {
    statement.BindNull(2);
  }
  statement.BindInt(3, static_cast<int>(node.type));
  statement.BindString16(4, node.title);
  statement.BindString16(5, node.icon);
  if (node.accent_argb.has_value()) {
    statement.BindInt64(6, *node.accent_argb);
  } else {
    statement.BindNull(6);
  }
  statement.BindString(7, node.url.spec());
  statement.BindString(8, node.sort_key);
  statement.BindTime(9, node.created_at);
  statement.BindTime(10, node.modified_at);
  statement.BindBool(11, node.tombstone);
  statement.BindString(12, node.id.AsLowercaseString());
  return statement.Run() && db_.GetLastChangeCount() == 1;
}

bool TabTreeStore::ReadUndoSnapshots(int64_t operation_id,
                                     std::vector<NodeSnapshot>* snapshots) {
  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT existed,node_id,model_version,workspace_id,parent_id,node_type,"
      "title,icon,accent_argb,url,sort_key,created_at,modified_at,tombstone "
      "FROM "
      "undo_node_snapshots WHERE operation_id=? ORDER BY ordinal"));
  statement.BindInt64(0, operation_id);

  std::vector<NodeSnapshot> decoded;
  while (statement.Step()) {
    NodeSnapshot snapshot;
    snapshot.node_id = base::Uuid::ParseLowercase(statement.ColumnString(1));
    if (!snapshot.node_id.is_valid()) {
      return false;
    }
    if (statement.ColumnBool(0)) {
      TreeNode node;
      node.model_version = statement.ColumnInt(2);
      node.id = snapshot.node_id;
      node.workspace_id = base::Uuid::ParseLowercase(statement.ColumnString(3));
      if (statement.GetColumnType(4) != sql::ColumnType::kNull) {
        base::Uuid parent =
            base::Uuid::ParseLowercase(statement.ColumnString(4));
        if (!parent.is_valid()) {
          return false;
        }
        node.parent_id = std::move(parent);
      }
      const int node_type = statement.ColumnInt(5);
      if (node_type == static_cast<int>(TreeNodeType::kFolder)) {
        node.type = TreeNodeType::kFolder;
      } else if (node_type == static_cast<int>(TreeNodeType::kSavedPage)) {
        node.type = TreeNodeType::kSavedPage;
      } else {
        return false;
      }
      node.title = statement.ColumnString16(6);
      node.icon = statement.ColumnString16(7);
      if (statement.GetColumnType(8) != sql::ColumnType::kNull) {
        const int64_t accent = statement.ColumnInt64(8);
        if (accent < 0 || accent > static_cast<int64_t>(
                                       std::numeric_limits<uint32_t>::max())) {
          return false;
        }
        node.accent_argb = static_cast<uint32_t>(accent);
      }
      node.url = GURL(statement.ColumnString(9));
      node.sort_key = statement.ColumnString(10);
      node.created_at = statement.ColumnTime(11);
      node.modified_at = statement.ColumnTime(12);
      node.tombstone = statement.ColumnBool(13);
      if (!ValidateNode(node)) {
        return false;
      }
      snapshot.previous = std::move(node);
    }
    decoded.push_back(std::move(snapshot));
  }
  if (!statement.Succeeded()) {
    return false;
  }
  *snapshots = std::move(decoded);
  return true;
}

bool TabTreeStore::RemoveUndoOperation(int64_t operation_id) {
  sql::Statement delete_snapshots(db_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM undo_node_snapshots WHERE operation_id=?"));
  delete_snapshots.BindInt64(0, operation_id);
  if (!delete_snapshots.Run()) {
    return false;
  }
  sql::Statement delete_operation(db_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM undo_operations WHERE operation_id=?"));
  delete_operation.BindInt64(0, operation_id);
  return delete_operation.Run() && db_.GetLastChangeCount() == 1;
}

void TabTreeStore::Notify(MutationKind kind,
                          const base::Uuid& subject_node_id,
                          std::vector<base::Uuid> node_ids) {
  DCHECK(subject_node_id.is_valid());
  DCHECK(std::ranges::find(node_ids, subject_node_id) != node_ids.end());
  TabTreeChange change{.kind = kind,
                       .subject_node_id = subject_node_id,
                       .node_ids = std::move(node_ids)};
  for (TabTreeObserver& observer : observers_) {
    observer.OnTabTreeChanged(change);
  }
}

}  // namespace ahoi::tab_tree
