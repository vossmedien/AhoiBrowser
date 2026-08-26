// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <cstddef>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "base/check.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

TabTreeStore::Result TabTreeStore::CreateNode(const TreeNode& node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!ValidateNode(node) || node.tombstone) {
    return Result::kInvalidArgument;
  }

  TreeNode existing;
  const Result lookup = ReadNode(node.id, &existing);
  if (lookup == Result::kOk) {
    return Result::kAlreadyExists;
  }
  if (lookup != Result::kNotFound) {
    return lookup;
  }
  const Result destination =
      ValidateDestination(node.id, node.workspace_id, node.parent_id);
  if (destination != Result::kOk) {
    return destination;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }

  if (!InsertUndoOperation(UndoMutationKind::kCreate, node.id, node.modified_at,
                           {{.node_id = node.id, .previous = std::nullopt}})) {
    return Result::kDatabaseError;
  }

  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
      "node_type,title,icon,accent_argb,url,sort_key,created_at,modified_at,"
      "tombstone) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  internal::BindNodeForInsert(statement, node);
  if (!statement.Run() || !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  Notify(MutationKind::kCreated, node.id, {node.id});
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::CreateNodesAtomically(
    const std::vector<TreeNode>& nodes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (nodes.empty()) {
    return Result::kInvalidArgument;
  }

  std::unordered_map<base::Uuid, size_t, base::UuidHash> batch_indexes;
  std::unordered_set<base::Uuid, base::UuidHash> validated_workspaces;
  batch_indexes.reserve(nodes.size());
  for (size_t index = 0; index < nodes.size(); ++index) {
    const TreeNode& node = nodes[index];
    if (!ValidateNode(node) || node.tombstone ||
        !batch_indexes.emplace(node.id, index).second) {
      return Result::kInvalidArgument;
    }

    TreeNode existing;
    const Result lookup = ReadNode(node.id, &existing);
    if (lookup == Result::kOk) {
      return Result::kAlreadyExists;
    }
    if (lookup != Result::kNotFound) {
      return lookup;
    }

    if (validated_workspaces.insert(node.workspace_id).second) {
      Workspace workspace;
      const Result workspace_result =
          ReadWorkspace(node.workspace_id, &workspace);
      if (workspace_result != Result::kOk) {
        return workspace_result;
      }
      if (workspace.tombstone) {
        return Result::kNotFound;
      }
    }
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
    auto parent = batch_indexes.find(*node.parent_id);
    if (parent == batch_indexes.end()) {
      const Result destination =
          ValidateDestination(node.id, node.workspace_id, node.parent_id);
      if (destination != Result::kOk) {
        return destination;
      }
      continue;
    }
    const TreeNode& parent_node = nodes[parent->second];
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
  if (ready.empty()) {
    return Result::kCycle;
  }
  if (ready.size() != 1) {
    return Result::kInvalidArgument;
  }
  std::vector<size_t> insertion_order;
  insertion_order.reserve(nodes.size());
  while (!ready.empty()) {
    const size_t index = ready.front();
    ready.pop_front();
    insertion_order.push_back(index);
    auto children = children_by_parent.find(nodes[index].id);
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
    return Result::kCycle;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }

  std::vector<NodeSnapshot> snapshots;
  snapshots.reserve(nodes.size());
  // Undo must delete children before their parents while foreign keys are on.
  for (auto it = insertion_order.rbegin(); it != insertion_order.rend(); ++it) {
    snapshots.push_back({.node_id = nodes[*it].id, .previous = std::nullopt});
  }
  const TreeNode& subject = nodes[insertion_order.front()];
  if (!InsertUndoOperation(UndoMutationKind::kCreate, subject.id,
                           subject.modified_at, snapshots)) {
    return Result::kDatabaseError;
  }

  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
      "node_type,title,icon,accent_argb,url,sort_key,created_at,modified_at,"
      "tombstone) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  for (size_t index : insertion_order) {
    statement.Reset(/*clear_bound_vars=*/true);
    internal::BindNodeForInsert(statement, nodes[index]);
    if (!statement.Run()) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }

  std::vector<base::Uuid> changed_ids;
  changed_ids.reserve(nodes.size());
  for (size_t index : insertion_order) {
    changed_ids.push_back(nodes[index].id);
  }
  Notify(MutationKind::kCreated, subject.id, std::move(changed_ids));
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::CreateFolderAroundNode(
    const base::Uuid& source_node_id,
    std::u16string title,
    base::Time modified_at,
    base::Uuid* folder_id) {
  return CreateFolderAroundNodes({source_node_id}, std::move(title),
                                 modified_at, folder_id);
}

TabTreeStore::Result TabTreeStore::CreateFolderAroundNodes(
    const std::vector<base::Uuid>& source_node_ids,
    std::u16string title,
    base::Time modified_at,
    base::Uuid* folder_id) {
  return CreateStyledFolderAroundNodes(source_node_ids, std::move(title), u"",
                                       std::nullopt, modified_at, folder_id);
}

TabTreeStore::Result TabTreeStore::CreateStyledFolderAroundNodes(
    const std::vector<base::Uuid>& source_node_ids,
    std::u16string title,
    std::u16string icon,
    std::optional<uint32_t> accent_argb,
    base::Time modified_at,
    base::Uuid* folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (source_node_ids.empty() || title.empty() || modified_at.is_null() ||
      !folder_id) {
    return Result::kInvalidArgument;
  }

  std::unordered_set<base::Uuid, base::UuidHash> unique_ids;
  std::vector<TreeNode> sources;
  unique_ids.reserve(source_node_ids.size());
  sources.reserve(source_node_ids.size());
  for (const base::Uuid& source_node_id : source_node_ids) {
    if (!source_node_id.is_valid() ||
        !unique_ids.insert(source_node_id).second) {
      return Result::kInvalidArgument;
    }
    TreeNode source;
    const Result source_result = ReadNode(source_node_id, &source);
    if (source_result != Result::kOk) {
      return source_result;
    }
    if (source.tombstone) {
      return Result::kNotFound;
    }
    if (source.type != TreeNodeType::kSavedPage ||
        (!sources.empty() &&
         source.workspace_id != sources.front().workspace_id)) {
      return Result::kInvalidArgument;
    }
    sources.push_back(std::move(source));
  }
  const TreeNode& primary_source = sources.front();

  TreeNode folder{
      .id = base::Uuid::GenerateRandomV4(),
      .workspace_id = primary_source.workspace_id,
      .parent_id = primary_source.parent_id,
      .type = TreeNodeType::kFolder,
      .title = std::move(title),
      .icon = std::move(icon),
      .accent_argb = accent_argb,
      .sort_key = primary_source.sort_key,
      .created_at = modified_at,
      .modified_at = modified_at,
  };
  if (!ValidateNode(folder)) {
    return Result::kInvalidArgument;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }

  // Undo restores every source first while the folder still exists, then
  // safely deletes the now-empty folder without violating the parent key.
  std::vector<NodeSnapshot> snapshots;
  snapshots.reserve(sources.size() + 1);
  for (const TreeNode& source : sources) {
    snapshots.push_back({.node_id = source.id, .previous = source});
  }
  snapshots.push_back({.node_id = folder.id, .previous = std::nullopt});
  if (!InsertUndoOperation(UndoMutationKind::kMove, primary_source.id,
                           modified_at, snapshots)) {
    return Result::kDatabaseError;
  }

  sql::Statement insert_folder(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
      "node_type,title,icon,accent_argb,url,sort_key,created_at,modified_at,"
      "tombstone) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  internal::BindNodeForInsert(insert_folder, folder);
  if (!insert_folder.Run()) {
    return Result::kDatabaseError;
  }

  sql::Statement move_source(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET parent_id=?,sort_key=?,modified_at=? WHERE id=?"));
  for (size_t index = 0; index < sources.size(); ++index) {
    move_source.Reset(/*clear_bound_vars=*/true);
    move_source.BindString(0, folder.id.AsLowercaseString());
    move_source.BindString(1, std::string(index + 1, '@'));
    move_source.BindTime(2, modified_at);
    move_source.BindString(3, sources[index].id.AsLowercaseString());
    if (!move_source.Run() || db_.GetLastChangeCount() != 1) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }

  *folder_id = folder.id;
  std::vector<base::Uuid> changed_ids;
  changed_ids.reserve(sources.size() + 1);
  for (const TreeNode& source : sources) {
    changed_ids.push_back(source.id);
  }
  changed_ids.push_back(folder.id);
  Notify(MutationKind::kMoved, primary_source.id, std::move(changed_ids));
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::RenameNode(const base::Uuid& node_id,
                                              std::u16string title,
                                              base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || title.empty() || modified_at.is_null()) {
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
  if (node.title == title) {
    return Result::kOk;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  if (!InsertUndoOperation(UndoMutationKind::kRename, node.id, modified_at,
                           {{.node_id = node.id, .previous = node}})) {
    return Result::kDatabaseError;
  }

  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE, "UPDATE tree_nodes SET title=?,modified_at=? WHERE id=?"));
  statement.BindString16(0, title);
  statement.BindTime(1, modified_at);
  statement.BindString(2, node.id.AsLowercaseString());
  if (!statement.Run() || db_.GetLastChangeCount() != 1 ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  Notify(MutationKind::kRenamed, node.id, {node.id});
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::UpdateFolderPresentation(
    const base::Uuid& node_id,
    std::u16string title,
    std::u16string icon,
    std::optional<uint32_t> accent_argb,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || title.empty() || icon.size() > 32 ||
      modified_at.is_null()) {
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
  if (node.type != TreeNodeType::kFolder) {
    return Result::kInvalidArgument;
  }
  if (node.title == title && node.icon == icon &&
      node.accent_argb == accent_argb) {
    return Result::kOk;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin() ||
      !InsertUndoOperation(UndoMutationKind::kRename, node.id, modified_at,
                           {{.node_id = node.id, .previous = node}})) {
    return Result::kDatabaseError;
  }
  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET title=?,icon=?,accent_argb=?,modified_at=? "
      "WHERE id=?"));
  statement.BindString16(0, title);
  statement.BindString16(1, icon);
  if (accent_argb.has_value()) {
    statement.BindInt64(2, *accent_argb);
  } else {
    statement.BindNull(2);
  }
  statement.BindTime(3, modified_at);
  statement.BindString(4, node.id.AsLowercaseString());
  if (!statement.Run() || db_.GetLastChangeCount() != 1 ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }
  Notify(MutationKind::kRenamed, node.id, {node.id});
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::UpdateSavedPageMetadata(
    const base::Uuid& node_id,
    std::u16string title,
    const GURL& url,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || title.empty() || !url.is_valid() ||
      url.is_empty() || modified_at.is_null()) {
    return Result::kInvalidArgument;
  }

  TreeNode node;
  const Result result = ReadNode(node_id, &node);
  if (result != Result::kOk) {
    return result;
  }
  if (node.tombstone) {
    return Result::kNotFound;
  }
  if (node.type != TreeNodeType::kSavedPage) {
    return Result::kInvalidArgument;
  }
  if (node.title == title && node.url == url) {
    return Result::kOk;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE tree_nodes SET title=?,url=?,modified_at=? WHERE id=?"));
  statement.BindString16(0, title);
  statement.BindString(1, url.spec());
  statement.BindTime(2, modified_at);
  statement.BindString(3, node.id.AsLowercaseString());
  if (!statement.Run() || db_.GetLastChangeCount() != 1 ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }

  Notify(MutationKind::kRenamed, node.id, {node.id});
  return Result::kOk;
}

}  // namespace ahoi::tab_tree
