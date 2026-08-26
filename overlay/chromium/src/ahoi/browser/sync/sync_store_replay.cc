// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_store.h"

#include <utility>

#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::sync {

SyncStore::Result SyncStore::ConsumeRemoteCommand(
    const base::Uuid& command_id,
    const base::Uuid& source_device_id,
    std::string nonce_base64,
    base::Time expires_at,
    base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) return Result::kNotInitialized;
  if (!command_id.is_valid() || !source_device_id.is_valid() ||
      nonce_base64.empty() || expires_at <= now) {
    return Result::kInvalidArgument;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) return Result::kDatabaseError;
  sql::Statement prune(db_.GetUniqueStatement(
      "DELETE FROM sync_command_replay WHERE expires_at<=?"));
  prune.BindTime(0, now);
  if (!prune.Run()) return Result::kDatabaseError;
  sql::Statement insert(db_.GetUniqueStatement(
      "INSERT OR IGNORE INTO sync_command_replay(command_id,source_device_id,"
      "nonce,expires_at,consumed_at) VALUES(?,?,?,?,?)"));
  insert.BindString(0, command_id.AsLowercaseString());
  insert.BindString(1, source_device_id.AsLowercaseString());
  insert.BindString(2, std::move(nonce_base64));
  insert.BindTime(3, expires_at);
  insert.BindTime(4, now);
  if (!insert.Run()) return Result::kDatabaseError;
  const bool inserted = db_.GetLastChangeCount() == 1;
  if (!transaction.Commit()) return Result::kDatabaseError;
  return inserted ? Result::kOk : Result::kAlreadyApplied;
}

}  // namespace ahoi::sync
