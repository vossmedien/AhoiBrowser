// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <algorithm>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "base/check.h"
#include "sql/statement.h"

namespace ahoi::tab_tree {

TabTreeStore::Result TabTreeStore::GetWorkspaces(
    std::vector<Workspace>* workspaces) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!workspaces) {
    return Result::kInvalidArgument;
  }

  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT model_version,id,name,icon,sort_key,accent_argb,created_at,"
      "modified_at,tombstone FROM workspaces WHERE tombstone=0 ORDER BY "
      "sort_key,id"));
  std::vector<Workspace> decoded;
  while (statement.Step()) {
    Workspace workspace;
    if (!internal::DecodeWorkspace(statement, &workspace) ||
        !ValidateWorkspace(workspace)) {
      return Result::kDatabaseError;
    }
    decoded.push_back(std::move(workspace));
  }
  if (!statement.Succeeded()) {
    return Result::kDatabaseError;
  }
  *workspaces = std::move(decoded);
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::GetWorkspace(const base::Uuid& workspace_id,
                                                Workspace* workspace) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!workspace_id.is_valid() || !workspace) {
    return Result::kInvalidArgument;
  }
  return ReadWorkspace(workspace_id, workspace);
}

TabTreeStore::Result TabTreeStore::GetNode(const base::Uuid& node_id,
                                           TreeNode* node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || !node) {
    return Result::kInvalidArgument;
  }
  return ReadNode(node_id, node);
}

TabTreeStore::Result TabTreeStore::GetSubtree(const base::Uuid& node_id,
                                              std::vector<TreeNode>* nodes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!node_id.is_valid() || !nodes) {
    return Result::kInvalidArgument;
  }
  std::vector<TreeNode> decoded;
  const Result result = ReadSubtree(node_id, &decoded);
  if (result != Result::kOk) {
    return result;
  }
  if (std::ranges::any_of(decoded, &TreeNode::tombstone)) {
    return Result::kNotFound;
  }
  *nodes = std::move(decoded);
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::GetChildren(
    const base::Uuid& workspace_id,
    std::optional<base::Uuid> parent_id,
    std::vector<TreeNode>* children) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!workspace_id.is_valid() || !children ||
      (parent_id.has_value() && !parent_id->is_valid())) {
    return Result::kInvalidArgument;
  }

  Workspace workspace;
  const Result workspace_result = ReadWorkspace(workspace_id, &workspace);
  if (workspace_result != Result::kOk) {
    return workspace_result;
  }
  if (workspace.tombstone) {
    return Result::kNotFound;
  }

  const std::string query =
      parent_id.has_value() ? "SELECT model_version,id,workspace_id,parent_id,"
                              "node_type,title,icon,accent_argb,url,sort_key,"
                              "created_at,modified_at,"
                              "tombstone FROM tree_nodes WHERE workspace_id=? "
                              "AND parent_id=? AND tombstone=0 ORDER BY "
                              "sort_key,id"
                            : "SELECT model_version,id,workspace_id,parent_id,"
                              "node_type,title,icon,accent_argb,url,sort_key,"
                              "created_at,modified_at,"
                              "tombstone FROM tree_nodes WHERE workspace_id=? "
                              "AND parent_id IS NULL AND tombstone=0 ORDER BY "
                              "sort_key,id";
  sql::Statement statement(db_.GetUniqueStatement(query));
  statement.BindString(0, workspace_id.AsLowercaseString());
  if (parent_id.has_value()) {
    statement.BindString(1, parent_id->AsLowercaseString());
  }

  std::vector<TreeNode> decoded;
  while (statement.Step()) {
    TreeNode node;
    if (!internal::DecodeNode(statement, &node) || !ValidateNode(node)) {
      return Result::kDatabaseError;
    }
    decoded.push_back(std::move(node));
  }
  if (!statement.Succeeded()) {
    return Result::kDatabaseError;
  }
  *children = std::move(decoded);
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::FindSavedPagesByUrl(
    const base::Uuid& workspace_id,
    const GURL& url,
    std::vector<TreeNode>* saved_pages) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!workspace_id.is_valid() || !url.is_valid() || url.is_empty() ||
      !saved_pages) {
    return Result::kInvalidArgument;
  }

  Workspace workspace;
  const Result workspace_result = ReadWorkspace(workspace_id, &workspace);
  if (workspace_result != Result::kOk) {
    return workspace_result;
  }
  if (workspace.tombstone) {
    return Result::kNotFound;
  }

  sql::Statement statement(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT model_version,id,workspace_id,parent_id,node_type,title,icon,"
      "accent_argb,url,sort_key,created_at,modified_at,tombstone FROM "
      "tree_nodes WHERE "
      "workspace_id=? AND node_type=? AND url=? AND tombstone=0 ORDER BY "
      "parent_id,sort_key,id"));
  statement.BindString(0, workspace_id.AsLowercaseString());
  statement.BindInt(1, static_cast<int>(TreeNodeType::kSavedPage));
  statement.BindString(2, url.spec());

  std::vector<TreeNode> decoded;
  while (statement.Step()) {
    TreeNode node;
    if (!internal::DecodeNode(statement, &node) || !ValidateNode(node)) {
      return Result::kDatabaseError;
    }
    decoded.push_back(std::move(node));
  }
  if (!statement.Succeeded()) {
    return Result::kDatabaseError;
  }
  *saved_pages = std::move(decoded);
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::ExportSnapshot(TabTreeSnapshot* snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!snapshot) {
    return Result::kInvalidArgument;
  }

  TabTreeSnapshot exported;
  sql::Statement workspaces(db_.GetUniqueStatement(
      "SELECT model_version,id,name,icon,sort_key,accent_argb,created_at,"
      "modified_at,tombstone FROM workspaces ORDER BY sort_key,id"));
  while (workspaces.Step()) {
    Workspace workspace;
    if (!internal::DecodeWorkspace(workspaces, &workspace) ||
        !ValidateWorkspace(workspace)) {
      return Result::kDatabaseError;
    }
    exported.workspaces.push_back(std::move(workspace));
  }
  if (!workspaces.Succeeded()) {
    return Result::kDatabaseError;
  }

  sql::Statement nodes(db_.GetUniqueStatement(
      "SELECT model_version,id,workspace_id,parent_id,node_type,title,icon,"
      "accent_argb,url,sort_key,created_at,modified_at,tombstone FROM "
      "tree_nodes ORDER BY id"));
  while (nodes.Step()) {
    TreeNode node;
    if (!internal::DecodeNode(nodes, &node) || !ValidateNode(node)) {
      return Result::kDatabaseError;
    }
    exported.nodes.push_back(std::move(node));
  }
  if (!nodes.Succeeded()) {
    return Result::kDatabaseError;
  }

  sql::Statement operations(db_.GetUniqueStatement(
      "SELECT operation_id,mutation_kind,subject_node_id,created_at FROM "
      "undo_operations ORDER BY operation_id"));
  while (operations.Step()) {
    UndoOperationSnapshot operation;
    operation.operation_id = operations.ColumnInt64(0);
    switch (operations.ColumnInt(1)) {
      case static_cast<int>(UndoMutationKind::kCreate):
        operation.kind = UndoMutationKind::kCreate;
        break;
      case static_cast<int>(UndoMutationKind::kRename):
        operation.kind = UndoMutationKind::kRename;
        break;
      case static_cast<int>(UndoMutationKind::kMove):
        operation.kind = UndoMutationKind::kMove;
        break;
      case static_cast<int>(UndoMutationKind::kDelete):
        operation.kind = UndoMutationKind::kDelete;
        break;
      default:
        return Result::kDatabaseError;
    }
    operation.subject_node_id =
        base::Uuid::ParseLowercase(operations.ColumnString(2));
    operation.created_at = operations.ColumnTime(3);
    if (operation.operation_id <= 0 || !operation.subject_node_id.is_valid() ||
        operation.created_at.is_null()) {
      return Result::kDatabaseError;
    }
    std::vector<NodeSnapshot> stored_nodes;
    if (!ReadUndoSnapshots(operation.operation_id, &stored_nodes) ||
        stored_nodes.empty()) {
      return Result::kDatabaseError;
    }
    operation.nodes.reserve(stored_nodes.size());
    for (NodeSnapshot& stored_node : stored_nodes) {
      operation.nodes.push_back({.node_id = stored_node.node_id,
                                 .previous = std::move(stored_node.previous)});
    }
    exported.undo_operations.push_back(std::move(operation));
  }
  if (!operations.Succeeded()) {
    return Result::kDatabaseError;
  }

  *snapshot = std::move(exported);
  return Result::kOk;
}

}  // namespace ahoi::tab_tree
