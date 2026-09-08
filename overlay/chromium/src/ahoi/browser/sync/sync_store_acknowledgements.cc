// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_store.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::sync {

SyncStore::Result SyncStore::AcknowledgeOutbox(
    const std::vector<std::string>& mutation_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (mutation_ids.empty()) {
    return Result::kInvalidArgument;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  for (const auto& mutation : mutation_ids) {
    if (mutation.empty()) {
      return Result::kInvalidArgument;
    }
    // Capture the exact acknowledged version before removing its outbox row.
    // A late ACK cannot cover a newer update, nor downgrade a newer receipt.
    sql::Statement receipt(db_.GetUniqueStatement(
        "INSERT INTO sync_acknowledged_records(entity_type,entity_id,"
        "version_model,version_physical,version_logical,version_device) "
        "SELECT entity_type,entity_id,version_model,version_physical,"
        "version_logical,version_device FROM sync_outbox WHERE mutation_id=? "
        "ON CONFLICT(entity_type,entity_id) DO UPDATE SET "
        "version_model=excluded.version_model,version_physical=excluded."
        "version_physical,"
        "version_logical=excluded.version_logical,version_device=excluded."
        "version_device "
        "WHERE "
        "(excluded.version_physical,excluded.version_logical,excluded.version_"
        "device) >= "
        "(sync_acknowledged_records.version_physical,sync_acknowledged_records."
        "version_logical,"
        "sync_acknowledged_records.version_device)"));
    receipt.BindString(0, mutation);
    if (!receipt.Run()) {
      return Result::kDatabaseError;
    }
    sql::Statement remove(
        db_.GetUniqueStatement("DELETE FROM sync_outbox WHERE mutation_id=?"));
    remove.BindString(0, mutation);
    if (!remove.Run()) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

bool SyncStore::IsRecordAcknowledged(const SyncRecord& record) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady() || !HasCompleteFieldVersions(record)) {
    return false;
  }
  sql::Statement query(db_.GetUniqueStatement(
      "SELECT version_model,version_physical,version_logical,version_device "
      "FROM sync_acknowledged_records WHERE entity_type=? AND entity_id=?"));
  query.BindInt(0, static_cast<int>(GetEntityType(record)));
  query.BindString(1, GetEntityId(record).AsLowercaseString());
  if (!query.Step() || query.ColumnInt(0) != kCurrentModelVersion) {
    return false;
  }
  const int64_t logical = query.ColumnInt64(2);
  if (logical < 0 || logical > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  const HlcStamp clock{.physical_time_us = query.ColumnInt64(1),
                       .logical = static_cast<uint32_t>(logical),
                       .device_tiebreak = query.ColumnString(3)};
  return IsValidSyncClock(clock) && clock >= GetVersion(record).stamp;
}

bool SyncStore::HasCompletedInitialFetch() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return false;
  }
  sql::Statement query(db_.GetUniqueStatement(
      "SELECT value FROM sync_metadata WHERE key='initial_fetch_complete'"));
  return query.Step() && query.ColumnString(0) == "1";
}

}  // namespace ahoi::sync
