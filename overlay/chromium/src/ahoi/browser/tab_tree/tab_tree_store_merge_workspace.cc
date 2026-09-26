// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "base/check.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

namespace {

// The sync wire limit for a sort key (workspace_structure_sync.cc).
constexpr size_t kMaxSortKeyLength = 1024;

}  // namespace

TabTreeStore::Result TabTreeStore::MergeWorkspace(
    const WorkspaceMerge& merge,
    std::optional<base::Uuid>* folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  const base::Uuid& source_workspace_id = merge.source_workspace_id;
  const base::Uuid& target_workspace_id = merge.target_workspace_id;
  const std::string& sort_key = merge.sort_key;
  const base::Time modified_at = merge.modified_at;
  if (!source_workspace_id.is_valid() || !target_workspace_id.is_valid() ||
      source_workspace_id == target_workspace_id || sort_key.empty() ||
      modified_at.is_null() || !folder_id) {
    return Result::kInvalidArgument;
  }
  *folder_id = std::nullopt;

  Workspace source;
  Workspace target;
  for (Result lookup : {ReadWorkspace(source_workspace_id, &source),
                        ReadWorkspace(target_workspace_id, &target)}) {
    if (lookup != Result::kOk) {
      return lookup;
    }
  }
  if (source.tombstone || target.tombstone) {
    return Result::kNotFound;
  }

  // Every root of the source keeps its subtree, tombstoned rows included (as
  // in MoveNode), so an archived or deleted descendant never stays behind in
  // a deleted Workspace.
  std::vector<TreeNode> roots;
  std::vector<base::Uuid> closing;
  {
    sql::Statement statement(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id FROM tree_nodes WHERE workspace_id=? AND parent_id IS NULL "
        "AND tombstone=0 ORDER BY sort_key,id"));
    statement.BindString(0, source_workspace_id.AsLowercaseString());
    while (statement.Step()) {
      TreeNode root;
      const base::Uuid id =
          base::Uuid::ParseLowercase(statement.ColumnString(0));
      if (!id.is_valid() || ReadNode(id, &root) != Result::kOk) {
        return Result::kDatabaseError;
      }
      if (root.is_temporary && merge.closing_temporary_ids.contains(root.id)) {
        closing.push_back(root.id);
        continue;
      }
      roots.push_back(std::move(root));
    }
    if (!statement.Succeeded()) {
      return Result::kDatabaseError;
    }
  }

  std::vector<NodeSnapshot> snapshots;
  std::vector<base::Uuid> changed_ids;
  const auto in_folder = [&merge](const TreeNode& root) {
    return merge.into_folder && !root.is_temporary;
  };
  for (const TreeNode& root : roots) {
    if (!in_folder(root) &&
        sort_key.size() + root.sort_key.size() > kMaxSortKeyLength) {
      return Result::kInvalidArgument;
    }
    std::vector<TreeNode> subtree;
    const Result read = ReadSubtree(root.id, &subtree);
    if (read != Result::kOk) {
      return read;
    }
    for (TreeNode& node : subtree) {
      changed_ids.push_back(node.id);
      snapshots.push_back({.node_id = node.id, .previous = std::move(node)});
    }
  }

  std::optional<TreeNode> folder;
  if (std::ranges::any_of(roots, in_folder)) {
    folder = TreeNode{
        .id = base::Uuid::GenerateRandomV4(),
        .workspace_id = target_workspace_id,
        .type = TreeNodeType::kFolder,
        .title = source.name,
        .icon = source.icon,
        .accent_argb = source.accent_argb,
        .sort_key = sort_key,
        .created_at = modified_at,
        .modified_at = modified_at,
    };
    if (!ValidateNode(*folder)) {
      return Result::kInvalidArgument;
    }
    // Undo restores every node while the folder still exists, then deletes
    // the empty folder without violating the parent key.
    snapshots.push_back({.node_id = folder->id, .previous = std::nullopt});
    changed_ids.push_back(folder->id);
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  // An empty source leaves nothing to undo, like DeleteWorkspace.
  if (merge.record_undo && !roots.empty() &&
      !InsertUndoOperation(UndoMutationKind::kMove, roots.front().id,
                           modified_at, snapshots)) {
    return Result::kDatabaseError;
  }
  if (folder) {
    sql::Statement insert_folder(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
        "node_type,title,icon,accent_argb,url,sort_key,created_at,modified_at,"
        "tombstone,is_temporary,target_kind,local_scheme,home_url,home_target_"
        "kind,home_local_scheme) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    internal::BindNodeForInsert(insert_folder, *folder);
    if (!insert_folder.Run()) {
      return Result::kDatabaseError;
    }
  }

  sql::Statement move_descendants(db_.GetUniqueStatement(
      "WITH RECURSIVE subtree(id) AS (SELECT id FROM tree_nodes WHERE id=? "
      "UNION SELECT child.id FROM tree_nodes child JOIN subtree parent ON "
      "child.parent_id=parent.id) UPDATE tree_nodes SET workspace_id=?,"
      "modified_at=? WHERE id IN (SELECT id FROM subtree) AND id<>?"));
  sql::Statement move_root(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET workspace_id=?,parent_id=?,sort_key=?,"
      "modified_at=? WHERE id=? AND tombstone=0"));
  for (const TreeNode& root : roots) {
    move_descendants.Reset(/*clear_bound_vars=*/true);
    move_descendants.BindString(0, root.id.AsLowercaseString());
    move_descendants.BindString(1, target_workspace_id.AsLowercaseString());
    move_descendants.BindTime(2, modified_at);
    move_descendants.BindString(3, root.id.AsLowercaseString());
    if (!move_descendants.Run()) {
      return Result::kDatabaseError;
    }
    move_root.Reset(/*clear_bound_vars=*/true);
    move_root.BindString(0, target_workspace_id.AsLowercaseString());
    if (in_folder(root)) {
      move_root.BindString(1, folder->id.AsLowercaseString());
      move_root.BindString(2, root.sort_key);
    } else {
      move_root.BindNull(1);
      move_root.BindString(2, sort_key + root.sort_key);
    }
    move_root.BindTime(3, modified_at);
    move_root.BindString(4, root.id.AsLowercaseString());
    if (!move_root.Run() || db_.GetLastChangeCount() != 1) {
      return Result::kDatabaseError;
    }
  }

  sql::Statement close_page(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET tombstone=1,modified_at=? WHERE id=? AND "
      "tombstone=0"));
  for (const base::Uuid& id : closing) {
    close_page.Reset(/*clear_bound_vars=*/true);
    close_page.BindTime(0, modified_at);
    close_page.BindString(1, id.AsLowercaseString());
    if (!close_page.Run() || db_.GetLastChangeCount() != 1) {
      return Result::kDatabaseError;
    }
  }

  sql::Statement workspace_row(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE workspaces SET tombstone=1,merged_into=?,modified_at=? "
      "WHERE id=? AND tombstone=0"));
  workspace_row.BindString(0, target_workspace_id.AsLowercaseString());
  workspace_row.BindTime(1, modified_at);
  workspace_row.BindString(2, source_workspace_id.AsLowercaseString());
  if (!workspace_row.Run() || db_.GetLastChangeCount() != 1 ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  if (folder) {
    *folder_id = folder->id;
  }
  if (!roots.empty()) {
    Notify(MutationKind::kMoved, roots.front().id, std::move(changed_ids));
  }
  if (!closing.empty()) {
    Notify(MutationKind::kDeleted, closing.front(), std::move(closing));
  }
  return Result::kOk;
}

}  // namespace ahoi::tab_tree
