// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_store.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::sync {
namespace {

void BindVersion(sql::Statement& statement,
                 int offset,
                 const SyncVersion& version) {
  statement.BindInt(offset, version.model_version);
  statement.BindInt64(offset + 1, version.stamp.physical_time_us);
  statement.BindInt(offset + 2, static_cast<int>(version.stamp.logical));
  statement.BindString(offset + 3, version.stamp.device_tiebreak);
}

SyncVersion ReadVersion(sql::Statement& statement, int offset) {
  return {.model_version = statement.ColumnInt(offset),
          .stamp = {
              .physical_time_us = statement.ColumnInt64(offset + 1),
              .logical = static_cast<uint32_t>(statement.ColumnInt(offset + 2)),
              .device_tiebreak = statement.ColumnString(offset + 3)}};
}

int64_t ToMicros(base::Time value) {
  return value.ToDeltaSinceWindowsEpoch().InMicroseconds();
}

}  // namespace

bool SyncStore::WriteQuarantine(const SyncChange& change,
                                const std::string& reason) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT INTO sync_quarantine(mutation_id,entity_type,entity_id,reason,"
      "payload,received_at) VALUES(?,?,?,?,?,?)"));
  statement.BindString(0, change.mutation_id);
  statement.BindInt(1, static_cast<int>(change.entity_type));
  statement.BindString(2, change.entity_id.AsLowercaseString());
  statement.BindString(3, reason);
  // Quarantine never reaches logs or UI. It is profile-local diagnostic state
  // and has the same at-rest trust boundary as the canonical SQLite payload.
  statement.BindString(4, change.payload);
  statement.BindInt64(5, ToMicros(base::Time::Now()));
  return statement.Run();
}

SyncStore::Result SyncStore::ReadDeletionWatermark(EntityType type,
                                                   const base::Uuid& id,
                                                   SyncVersion* version) const {
  if (!version || !id.is_valid()) {
    return Result::kInvalidArgument;
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT version_model,version_physical,version_logical,version_device "
      "FROM sync_deletion_watermarks WHERE entity_type=? AND entity_id=?"));
  statement.BindInt(0, static_cast<int>(type));
  statement.BindString(1, id.AsLowercaseString());
  if (!statement.Step()) {
    return statement.Succeeded() ? Result::kNotFound : Result::kDatabaseError;
  }
  *version = ReadVersion(statement, 0);
  return Result::kOk;
}

int64_t SyncStore::QuarantineCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return -1;
  }
  sql::Statement statement(
      db_.GetUniqueStatement("SELECT COUNT(*) FROM sync_quarantine"));
  return statement.Step() ? statement.ColumnInt64(0) : -1;
}

SyncStore::Result SyncStore::CompactExpiredTombstones(
    base::Time now,
    base::TimeDelta retention) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (now.is_null() || retention < base::TimeDelta()) {
    return Result::kInvalidArgument;
  }

  struct Candidate {
    EntityType type;
    std::string id;
    SyncVersion version;
  };
  std::vector<Candidate> candidates;
  sql::Statement select(db_.GetUniqueStatement(
      "SELECT t.entity_type,t.entity_id,t.version_model,t.version_physical,"
      "t.version_logical,t.version_device FROM sync_tombstones t WHERE "
      "t.deleted_at<=? AND NOT EXISTS(SELECT 1 FROM sync_outbox o WHERE "
      "o.entity_type=t.entity_type AND o.entity_id=t.entity_id)"));
  select.BindInt64(0, ToMicros(now - retention));
  while (select.Step()) {
    candidates.push_back({.type = static_cast<EntityType>(select.ColumnInt(0)),
                          .id = select.ColumnString(1),
                          .version = ReadVersion(select, 2)});
  }
  if (!select.Succeeded()) {
    return Result::kDatabaseError;
  }
  if (candidates.empty()) {
    return Result::kOk;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  for (const Candidate& candidate : candidates) {
    sql::Statement watermark(db_.GetUniqueStatement(
        "INSERT OR REPLACE INTO sync_deletion_watermarks(entity_type,entity_id,"
        "version_model,version_physical,version_logical,version_device,"
        "compacted_at) VALUES(?,?,?,?,?,?,?)"));
    watermark.BindInt(0, static_cast<int>(candidate.type));
    watermark.BindString(1, candidate.id);
    BindVersion(watermark, 2, candidate.version);
    watermark.BindInt64(6, ToMicros(now));
    if (!watermark.Run()) {
      return Result::kDatabaseError;
    }

    sql::Statement remove_record(db_.GetUniqueStatement(
        "DELETE FROM sync_records WHERE entity_type=? AND entity_id=?"));
    remove_record.BindInt(0, static_cast<int>(candidate.type));
    remove_record.BindString(1, candidate.id);
    sql::Statement remove_tombstone(db_.GetUniqueStatement(
        "DELETE FROM sync_tombstones WHERE entity_type=? AND entity_id=?"));
    remove_tombstone.BindInt(0, static_cast<int>(candidate.type));
    remove_tombstone.BindString(1, candidate.id);
    if (!remove_record.Run() || !remove_tombstone.Run()) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

}  // namespace ahoi::sync
