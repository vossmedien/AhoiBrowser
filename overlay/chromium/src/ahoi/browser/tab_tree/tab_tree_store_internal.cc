// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"

#include <limits>
#include <utility>

#include "sql/statement.h"

namespace ahoi::tab_tree::internal {

bool DecodeWorkspace(sql::Statement& statement, Workspace* workspace) {
  Workspace decoded;
  decoded.model_version = statement.ColumnInt(0);
  decoded.id = base::Uuid::ParseLowercase(statement.ColumnString(1));
  decoded.name = statement.ColumnString16(2);
  decoded.icon = statement.ColumnString16(3);
  decoded.sort_key = statement.ColumnString(4);
  if (statement.GetColumnType(5) != sql::ColumnType::kNull) {
    const int64_t accent = statement.ColumnInt64(5);
    if (accent < 0 ||
        accent > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    decoded.accent_argb = static_cast<uint32_t>(accent);
  }
  decoded.created_at = statement.ColumnTime(6);
  decoded.modified_at = statement.ColumnTime(7);
  decoded.tombstone = statement.ColumnBool(8);

  if (decoded.model_version != kCurrentModelVersion || !decoded.id.is_valid()) {
    return false;
  }
  *workspace = std::move(decoded);
  return true;
}

bool DecodeNode(sql::Statement& statement, TreeNode* node) {
  TreeNode decoded;
  decoded.model_version = statement.ColumnInt(0);
  decoded.id = base::Uuid::ParseLowercase(statement.ColumnString(1));
  decoded.workspace_id = base::Uuid::ParseLowercase(statement.ColumnString(2));
  if (statement.GetColumnType(3) != sql::ColumnType::kNull) {
    base::Uuid parent = base::Uuid::ParseLowercase(statement.ColumnString(3));
    if (!parent.is_valid()) {
      return false;
    }
    decoded.parent_id = std::move(parent);
  }

  switch (statement.ColumnInt(4)) {
    case static_cast<int>(TreeNodeType::kFolder):
      decoded.type = TreeNodeType::kFolder;
      break;
    case static_cast<int>(TreeNodeType::kSavedPage):
      decoded.type = TreeNodeType::kSavedPage;
      break;
    default:
      return false;
  }

  decoded.title = statement.ColumnString16(5);
  decoded.icon = statement.ColumnString16(6);
  if (statement.GetColumnType(7) != sql::ColumnType::kNull) {
    const int64_t accent = statement.ColumnInt64(7);
    if (accent < 0 ||
        accent > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    decoded.accent_argb = static_cast<uint32_t>(accent);
  }
  decoded.url = GURL(statement.ColumnString(8));
  decoded.sort_key = statement.ColumnString(9);
  decoded.created_at = statement.ColumnTime(10);
  decoded.modified_at = statement.ColumnTime(11);
  decoded.tombstone = statement.ColumnBool(12);

  if (decoded.model_version != kCurrentModelVersion || !decoded.id.is_valid() ||
      !decoded.workspace_id.is_valid()) {
    return false;
  }
  *node = std::move(decoded);
  return true;
}

void BindNodeForInsert(sql::Statement& statement, const TreeNode& node) {
  statement.BindInt(0, node.model_version);
  statement.BindString(1, node.id.AsLowercaseString());
  statement.BindString(2, node.workspace_id.AsLowercaseString());
  if (node.parent_id.has_value()) {
    statement.BindString(3, node.parent_id->AsLowercaseString());
  } else {
    statement.BindNull(3);
  }
  statement.BindInt(4, static_cast<int>(node.type));
  statement.BindString16(5, node.title);
  statement.BindString16(6, node.icon);
  if (node.accent_argb.has_value()) {
    statement.BindInt64(7, *node.accent_argb);
  } else {
    statement.BindNull(7);
  }
  statement.BindString(8, node.url.spec());
  statement.BindString(9, node.sort_key);
  statement.BindTime(10, node.created_at);
  statement.BindTime(11, node.modified_at);
  statement.BindBool(12, node.tombstone);
}

void BindWorkspaceForInsert(sql::Statement& statement,
                            const Workspace& workspace) {
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
}

}  // namespace ahoi::tab_tree::internal
