// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_internal.h"
#include "base/json/json_reader.h"
#include "sql/statement.h"

namespace ahoi::tab_tree {
TabTreeStore::Result TabTreeStore::SetWorkspaceArchivePolicy(
    const base::Uuid& workspace_id,
    sync::SharedArchivePolicy policy,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady())
    return Result::kNotInitialized;
  if (!workspace_id.is_valid() || modified_at.is_null() ||
      static_cast<int>(policy) < 0 || static_cast<int>(policy) > 4)
    return Result::kInvalidArgument;
  Workspace workspace;
  const auto result = ReadWorkspace(workspace_id, &workspace);
  if (result != Result::kOk)
    return result;
  if (workspace.tombstone)
    return Result::kNotFound;
  if (workspace.archive_policy == policy)
    return Result::kOk;
  sql::Statement update(db_.GetUniqueStatement(
      "UPDATE workspaces SET archive_policy=?,modified_at=? WHERE id=? AND "
      "tombstone=0"));
  update.BindInt(0, static_cast<int>(policy));
  update.BindTime(1, modified_at);
  update.BindString(2, workspace_id.AsLowercaseString());
  if (!update.Run() || db_.GetLastChangeCount() != 1)
    return Result::kDatabaseError;
  Notify(MutationKind::kMoved, workspace_id, {workspace_id});
  return Result::kOk;
}

namespace internal {
bool DecodeWorkspaceStructureState(std::string_view state,
                                   std::set<base::Uuid>* hidden_nodes) {
  hidden_nodes->clear();
  if (state.empty()) {
    return true;
  }
  if (state.size() > 32 * 1024 * 1024) {
    return false;
  }
  const auto root = base::JSONReader::ReadDict(state, base::JSON_PARSE_RFC);
  if (!root || root->FindInt("schema") != 1 || !root->FindList("entries")) {
    return false;
  }
  const auto* ids = root->FindList("hidden_nodes");
  if (!ids) {
    return false;
  }
  for (const auto& value : *ids) {
    if (!value.is_string()) {
      return false;
    }
    const auto id = base::Uuid::ParseLowercase(value.GetString());
    if (!id.is_valid() || !hidden_nodes->insert(id).second) {
      return false;
    }
  }
  return true;
}
}  // namespace internal

std::optional<std::string> TabTreeStore::ReadWorkspaceStructureState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return std::nullopt;
  }
  sql::Statement query(db_.GetUniqueStatement(
      "SELECT value FROM meta WHERE key='ahoi.workspace_structure'"));
  std::string value;
  if (query.Step()) {
    if (query.GetColumnType(0) != sql::ColumnType::kText) {
      return std::nullopt;
    }
    value = query.ColumnString(0);
  }
  return query.Succeeded() ? std::make_optional(std::move(value))
                           : std::nullopt;
}

bool TabTreeStore::LoadWorkspaceStructureState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto state = ReadWorkspaceStructureState();
  return state &&
         internal::DecodeWorkspaceStructureState(*state, &archived_node_ids_);
}

TabTreeStore::Result TabTreeStore::SetWorkspaceStructureState(
    std::string state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  std::set<base::Uuid> hidden;
  if (!internal::DecodeWorkspaceStructureState(state, &hidden)) {
    return Result::kInvalidArgument;
  }
  sql::Statement write(
      db_.GetUniqueStatement("INSERT OR REPLACE INTO meta(key,value) "
                             "VALUES('ahoi.workspace_structure',?)"));
  write.BindString(0, state);
  if (!write.Run()) {
    return Result::kDatabaseError;
  }
  std::set<base::Uuid> changed = archived_node_ids_;
  changed.insert(hidden.begin(), hidden.end());
  archived_node_ids_ = std::move(hidden);
  if (!changed.empty()) {
    Notify(MutationKind::kMoved, *changed.begin(),
           {changed.begin(), changed.end()});
  }
  return Result::kOk;
}

bool TabTreeStore::IsNodeArchived(const base::Uuid& id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return archived_node_ids_.contains(id);
}

}  // namespace ahoi::tab_tree
