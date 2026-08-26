// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/check.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

TabTreeStore::Result TabTreeStore::CreateWorkspace(const Workspace& workspace) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!ValidateWorkspace(workspace) || workspace.tombstone) {
    return Result::kInvalidArgument;
  }

  Workspace existing;
  const Result lookup = ReadWorkspace(workspace.id, &existing);
  if (lookup == Result::kOk) {
    return Result::kAlreadyExists;
  }
  if (lookup != Result::kNotFound) {
    return lookup;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO workspaces(model_version,id,name,icon,sort_key,accent_argb,"
      "created_at,modified_at,tombstone) VALUES(?,?,?,?,?,?,?,?,?)"));
  statement.BindInt(0, workspace.model_version);
  statement.BindString(1, workspace.id.AsLowercaseString());
  statement.BindString16(2, workspace.name);
  statement.BindString16(3, workspace.icon);
  statement.BindString(4, workspace.sort_key);
  if (workspace.accent_argb.has_value()) {
    statement.BindInt64(5, *workspace.accent_argb);
  } else {
    statement.BindNull(5);
  }
  statement.BindTime(6, workspace.created_at);
  statement.BindTime(7, workspace.modified_at);
  statement.BindBool(8, workspace.tombstone);
  if (!statement.Run() || !transaction.Commit()) {
    return Result::kDatabaseError;
  }
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::UpdateWorkspacePresentation(
    const base::Uuid& workspace_id,
    std::u16string name,
    std::u16string icon,
    std::optional<uint32_t> accent_argb,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!workspace_id.is_valid() || name.empty() || modified_at.is_null()) {
    return Result::kInvalidArgument;
  }

  Workspace workspace;
  const Result lookup = ReadWorkspace(workspace_id, &workspace);
  if (lookup != Result::kOk) {
    return lookup;
  }
  if (workspace.tombstone) {
    return Result::kNotFound;
  }

  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE workspaces SET name=?,icon=?,accent_argb=?,modified_at=? "
      "WHERE id=? AND tombstone=0"));
  statement.BindString16(0, name);
  statement.BindString16(1, icon);
  if (accent_argb.has_value()) {
    statement.BindInt64(2, *accent_argb);
  } else {
    statement.BindNull(2);
  }
  statement.BindTime(3, modified_at);
  statement.BindString(4, workspace_id.AsLowercaseString());
  return statement.Run() && db_.GetLastChangeCount() == 1
             ? Result::kOk
             : Result::kDatabaseError;
}

TabTreeStore::Result TabTreeStore::DeleteWorkspace(
    const base::Uuid& workspace_id,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!workspace_id.is_valid() || modified_at.is_null()) {
    return Result::kInvalidArgument;
  }

  Workspace workspace;
  const Result lookup = ReadWorkspace(workspace_id, &workspace);
  if (lookup != Result::kOk) {
    return lookup;
  }
  if (workspace.tombstone) {
    return Result::kNotFound;
  }

  sql::Statement visible_count(db_.GetCachedStatement(
      SQL_FROM_HERE, "SELECT COUNT(*) FROM workspaces WHERE tombstone=0"));
  if (!visible_count.Step()) {
    return Result::kDatabaseError;
  }
  if (visible_count.ColumnInt64(0) <= 1) {
    return Result::kInvalidArgument;
  }

  std::vector<base::Uuid> deleted_node_ids;
  sql::Statement node_ids(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id FROM tree_nodes WHERE workspace_id=? AND tombstone=0"));
  node_ids.BindString(0, workspace_id.AsLowercaseString());
  while (node_ids.Step()) {
    const base::Uuid node_id =
        base::Uuid::ParseLowercase(node_ids.ColumnString(0));
    if (!node_id.is_valid()) {
      return Result::kDatabaseError;
    }
    deleted_node_ids.push_back(node_id);
  }
  if (!node_ids.Succeeded()) {
    return Result::kDatabaseError;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  sql::Statement nodes(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET tombstone=1,modified_at=? WHERE workspace_id=? "
      "AND tombstone=0"));
  nodes.BindTime(0, modified_at);
  nodes.BindString(1, workspace_id.AsLowercaseString());
  if (!nodes.Run()) {
    return Result::kDatabaseError;
  }
  sql::Statement workspace_row(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE workspaces SET tombstone=1,modified_at=? WHERE id=? AND "
      "tombstone=0"));
  workspace_row.BindTime(0, modified_at);
  workspace_row.BindString(1, workspace_id.AsLowercaseString());
  if (!workspace_row.Run() || db_.GetLastChangeCount() != 1 ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  if (!deleted_node_ids.empty()) {
    Notify(MutationKind::kDeleted, deleted_node_ids.front(),
           std::move(deleted_node_ids));
  }
  return Result::kOk;
}

}  // namespace ahoi::tab_tree
