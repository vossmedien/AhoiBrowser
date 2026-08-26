// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <set>
#include <utility>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "sql/statement.h"

namespace ahoi::tab_tree {

namespace {

constexpr char kSelectWorkspaceSql[] =
    "SELECT model_version,id,name,icon,sort_key,accent_argb,created_at,"
    "modified_at,tombstone FROM workspaces WHERE id=?";

constexpr char kSelectNodeSql[] =
    "SELECT model_version,id,workspace_id,parent_id,node_type,title,icon,"
    "accent_argb,url,sort_key,created_at,modified_at,tombstone FROM "
    "tree_nodes WHERE id=?";

}  // namespace

bool TabTreeStore::ValidateWorkspace(const Workspace& workspace) const {
  return workspace.model_version == kCurrentModelVersion &&
         workspace.id.is_valid() && !workspace.name.empty() &&
         !workspace.sort_key.empty() && !workspace.created_at.is_null() &&
         !workspace.modified_at.is_null();
}

bool TabTreeStore::ValidateNode(const TreeNode& node) const {
  if (node.model_version != kCurrentModelVersion || !node.id.is_valid() ||
      !node.workspace_id.is_valid() || node.title.empty() ||
      node.sort_key.empty() || node.created_at.is_null() ||
      node.modified_at.is_null() ||
      (node.parent_id.has_value() && !node.parent_id->is_valid())) {
    return false;
  }
  if (node.type == TreeNodeType::kFolder) {
    return node.url.is_empty();
  }
  return node.type == TreeNodeType::kSavedPage && node.url.is_valid() &&
         !node.url.is_empty() && node.icon.empty() &&
         !node.accent_argb.has_value();
}

TabTreeStore::Result TabTreeStore::ReadWorkspace(const base::Uuid& workspace_id,
                                                 Workspace* workspace) {
  sql::Statement statement(
      db_.GetCachedStatement(SQL_FROM_HERE, kSelectWorkspaceSql));
  statement.BindString(0, workspace_id.AsLowercaseString());
  if (!statement.Step()) {
    return statement.Succeeded() ? Result::kNotFound : Result::kDatabaseError;
  }
  return internal::DecodeWorkspace(statement, workspace) &&
                 ValidateWorkspace(*workspace)
             ? Result::kOk
             : Result::kDatabaseError;
}

TabTreeStore::Result TabTreeStore::ReadNode(const base::Uuid& node_id,
                                            TreeNode* node) {
  sql::Statement statement(
      db_.GetCachedStatement(SQL_FROM_HERE, kSelectNodeSql));
  statement.BindString(0, node_id.AsLowercaseString());
  if (!statement.Step()) {
    return statement.Succeeded() ? Result::kNotFound : Result::kDatabaseError;
  }
  return internal::DecodeNode(statement, node) && ValidateNode(*node)
             ? Result::kOk
             : Result::kDatabaseError;
}

TabTreeStore::Result TabTreeStore::ReadSubtree(const base::Uuid& node_id,
                                               std::vector<TreeNode>* nodes) {
  sql::Statement statement(db_.GetUniqueStatement(
      "WITH RECURSIVE subtree(id) AS (SELECT id FROM tree_nodes WHERE id=? "
      "UNION SELECT child.id FROM tree_nodes child JOIN subtree parent ON "
      "child.parent_id=parent.id) SELECT node.model_version,node.id,"
      "node.workspace_id,node.parent_id,node.node_type,node.title,node.icon,"
      "node.accent_argb,node.url,node.sort_key,node.created_at,node.modified_"
      "at,"
      "node.tombstone FROM "
      "tree_nodes node JOIN subtree ON subtree.id=node.id ORDER BY node.id"));
  statement.BindString(0, node_id.AsLowercaseString());

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
  if (decoded.empty()) {
    return Result::kNotFound;
  }
  *nodes = std::move(decoded);
  return Result::kOk;
}

TabTreeStore::Result TabTreeStore::ValidateDestination(
    const base::Uuid& moving_node_id,
    const base::Uuid& workspace_id,
    const std::optional<base::Uuid>& parent_id) {
  Workspace workspace;
  Result result = ReadWorkspace(workspace_id, &workspace);
  if (result != Result::kOk) {
    return result;
  }
  if (workspace.tombstone) {
    return Result::kNotFound;
  }
  if (!parent_id.has_value()) {
    return Result::kOk;
  }
  if (*parent_id == moving_node_id) {
    return Result::kCycle;
  }

  std::set<std::string> visited;
  std::optional<base::Uuid> cursor = parent_id;
  while (cursor.has_value()) {
    if (*cursor == moving_node_id ||
        !visited.insert(cursor->AsLowercaseString()).second) {
      return Result::kCycle;
    }
    TreeNode ancestor;
    result = ReadNode(*cursor, &ancestor);
    if (result != Result::kOk) {
      return result;
    }
    if (ancestor.tombstone || ancestor.type != TreeNodeType::kFolder ||
        ancestor.workspace_id != workspace_id) {
      return Result::kInvalidArgument;
    }
    cursor = ancestor.parent_id;
  }
  return Result::kOk;
}

}  // namespace ahoi::tab_tree
