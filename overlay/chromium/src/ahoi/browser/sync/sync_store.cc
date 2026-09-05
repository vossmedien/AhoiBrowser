// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/sync_store.h"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/check.h"
#include "base/uuid.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::sync {
namespace {

int ToInt(EntityType type) {
  return static_cast<int>(type);
}

int ToInt(ChangeKind kind) {
  return static_cast<int>(kind);
}

bool IsValidEntityType(int value) {
  return value >= static_cast<int>(EntityType::kDevice) &&
         value <= static_cast<int>(EntityType::kBookmark);
}

bool IsValidChangeKind(int value) {
  return value == static_cast<int>(ChangeKind::kUpsert) ||
         value == static_cast<int>(ChangeKind::kDelete);
}

void BindVersion(sql::Statement& statement,
                 int offset,
                 const SyncVersion& version) {
  statement.BindInt(offset, version.model_version);
  statement.BindInt64(offset + 1, version.stamp.physical_time_us);
  statement.BindInt(offset + 2, static_cast<int>(version.stamp.logical));
  statement.BindString(offset + 3, version.stamp.device_tiebreak);
}

SyncVersion ReadVersion(sql::Statement& statement, int offset) {
  return SyncVersion{
      .model_version = statement.ColumnInt(offset),
      .stamp = HlcStamp{
          .physical_time_us = statement.ColumnInt64(offset + 1),
          .logical = static_cast<uint32_t>(statement.ColumnInt(offset + 2)),
          .device_tiebreak = statement.ColumnString(offset + 3)}};
}

base::Time ReadTime(sql::Statement& statement, int column) {
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(statement.ColumnInt64(column)));
}

void BindTime(sql::Statement& statement, int column, base::Time time) {
  statement.BindInt64(column, time.ToDeltaSinceWindowsEpoch().InMicroseconds());
}

}  // namespace

SyncStore::SyncStore()
    // AhoiSync will get a dedicated DatabaseTag histogram variant when the
    // provider-backed profile service is wired into chrome/browser. Reuse the
    // existing Ahoi tag until that integration lands; storage itself remains
    // profile-local and independently versioned.
    : db_(sql::DatabaseOptions().set_flush_to_media(true), "AhoiTabTree") {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

SyncStore::~SyncStore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool SyncStore::Initialize(const base::FilePath& path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (path.empty() || db_.is_open() || !db_.Open(path)) {
    return false;
  }
  if (!InitializeSchema()) {
    db_.Close();
    return false;
  }
  return true;
}

bool SyncStore::InitializeInMemory() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (db_.is_open() || !db_.OpenInMemory()) {
    return false;
  }
  if (!InitializeSchema()) {
    db_.Close();
    return false;
  }
  return true;
}

void SyncStore::AddObserver(SyncStoreObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void SyncStore::RemoveObserver(SyncStoreObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool SyncStore::IsReady() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return db_.is_open();
}

SyncStore::Result SyncStore::ReadStoredRecord(EntityType type,
                                              const base::Uuid& id,
                                              StoredRecord* stored) const {
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!id.is_valid() || !stored) {
    return Result::kInvalidArgument;
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT payload FROM sync_records WHERE entity_type=? AND entity_id=?"));
  statement.BindInt(0, ToInt(type));
  statement.BindString(1, id.AsLowercaseString());
  if (!statement.Step()) {
    return statement.Succeeded() ? Result::kNotFound : Result::kDatabaseError;
  }
  stored->payload = statement.ColumnString(0);
  if (!DeserializeRecord(type, stored->payload, &stored->record) ||
      !ValidateRecord(stored->record, nullptr)) {
    return Result::kDatabaseError;
  }
  return Result::kOk;
}

bool SyncStore::ReadStoredRecords(EntityType type,
                                  std::vector<StoredRecord>* records) const {
  if (!records) {
    return false;
  }
  records->clear();
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT payload FROM sync_records WHERE entity_type=? "
      "ORDER BY entity_id"));
  statement.BindInt(0, ToInt(type));
  while (statement.Step()) {
    StoredRecord stored;
    stored.payload = statement.ColumnString(0);
    if (!DeserializeRecord(type, stored.payload, &stored.record) ||
        !ValidateRecord(stored.record, nullptr)) {
      return false;
    }
    records->push_back(std::move(stored));
  }
  return statement.Succeeded();
}

bool SyncStore::UpsertRecord(const SyncRecord& record,
                             const std::string& payload) {
  const EntityType type = GetEntityType(record);
  const SyncVersion& version = GetVersion(record);
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO sync_records(entity_type,entity_id,payload,"
      "tombstone,model_version,version_physical,version_logical,version_device)"
      "VALUES(?,?,?,?,?,?,?,?)"));
  statement.BindInt(0, ToInt(type));
  statement.BindString(1, GetEntityId(record).AsLowercaseString());
  statement.BindString(2, payload);
  statement.BindInt(3, IsTombstone(record) ? 1 : 0);
  BindVersion(statement, 4, version);
  return statement.Run();
}

bool SyncStore::WriteTombstone(const SyncRecord& record) {
  const EntityType type = GetEntityType(record);
  const SyncVersion& version = GetVersion(record);
  if (IsTombstone(record)) {
    sql::Statement statement(db_.GetUniqueStatement(
        "INSERT OR REPLACE INTO sync_tombstones(entity_type,entity_id,"
        "version_model,version_physical,version_logical,version_device,"
        "deleted_at) VALUES(?,?,?,?,?,?,?)"));
    statement.BindInt(0, ToInt(type));
    statement.BindString(1, GetEntityId(record).AsLowercaseString());
    BindVersion(statement, 2, version);
    BindTime(statement, 6, base::Time::Now());
    return statement.Run();
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "DELETE FROM sync_tombstones WHERE entity_type=? AND entity_id=?"));
  statement.BindInt(0, ToInt(type));
  statement.BindString(1, GetEntityId(record).AsLowercaseString());
  return statement.Run();
}

bool SyncStore::WriteOutbox(const SyncChange& change) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR IGNORE INTO sync_outbox(mutation_id,entity_type,entity_id,"
      "change_kind,payload,version_model,version_physical,version_logical,"
      "version_device,created_at) VALUES(?,?,?,?,?,?,?,?,?,?)"));
  statement.BindString(0, change.mutation_id);
  statement.BindInt(1, ToInt(change.entity_type));
  statement.BindString(2, change.entity_id.AsLowercaseString());
  statement.BindInt(3, ToInt(change.kind));
  statement.BindString(4, change.payload);
  BindVersion(statement, 5, change.version);
  BindTime(statement, 9, base::Time::Now());
  return statement.Run();
}

bool SyncStore::OutboxMatches(const SyncChange& change, bool* present) {
  if (!present) {
    return false;
  }
  *present = false;
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT entity_type,entity_id,change_kind,payload,version_model,"
      "version_physical,version_logical,version_device FROM sync_outbox "
      "WHERE mutation_id=?"));
  statement.BindString(0, change.mutation_id);
  if (!statement.Step()) {
    return statement.Succeeded();
  }
  *present = true;
  const SyncVersion version = ReadVersion(statement, 4);
  return statement.ColumnInt(0) == ToInt(change.entity_type) &&
         statement.ColumnString(1) == change.entity_id.AsLowercaseString() &&
         statement.ColumnInt(2) == ToInt(change.kind) &&
         statement.ColumnString(3) == change.payload &&
         version == change.version;
}

bool SyncStore::WriteInbox(const SyncChange& change) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT INTO sync_inbox(mutation_id,entity_type,entity_id,change_kind,"
      "payload,version_model,version_physical,version_logical,version_device,"
      "received_at) VALUES(?,?,?,?,?,?,?,?,?,?)"));
  statement.BindString(0, change.mutation_id);
  statement.BindInt(1, ToInt(change.entity_type));
  statement.BindString(2, change.entity_id.AsLowercaseString());
  statement.BindInt(3, ToInt(change.kind));
  statement.BindString(4, change.payload);
  BindVersion(statement, 5, change.version);
  BindTime(statement, 9, base::Time::Now());
  return statement.Run();
}

bool SyncStore::InboxMatches(const SyncChange& change, bool* present) {
  if (!present) {
    return false;
  }
  *present = false;
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT entity_type,entity_id,change_kind,payload,version_model,"
      "version_physical,version_logical,version_device FROM sync_inbox "
      "WHERE mutation_id=?"));
  statement.BindString(0, change.mutation_id);
  if (!statement.Step()) {
    return statement.Succeeded();
  }
  *present = true;
  const SyncVersion version = ReadVersion(statement, 4);
  return statement.ColumnInt(0) == ToInt(change.entity_type) &&
         statement.ColumnString(1) == change.entity_id.AsLowercaseString() &&
         statement.ColumnInt(2) == ToInt(change.kind) &&
         statement.ColumnString(3) == change.payload &&
         version == change.version;
}

bool SyncStore::SetMetadata(const std::string& key, const std::string& value) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO sync_metadata(key,value) VALUES(?,?)"));
  statement.BindString(0, key);
  statement.BindString(1, value);
  return statement.Run();
}

SyncStore::Result SyncStore::PutLocalRecord(const SyncRecord& record,
                                            std::string mutation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!ValidateRecord(record, nullptr) || !GetEntityId(record).is_valid()) {
    return Result::kInvalidArgument;
  }
  if (mutation_id.empty()) {
    mutation_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  if (mutation_id.empty()) {
    return Result::kInvalidArgument;
  }
  SyncVersion deletion_watermark;
  const Result watermark = ReadDeletionWatermark(
      GetEntityType(record), GetEntityId(record), &deletion_watermark);
  if (watermark == Result::kOk) {
    return Result::kConflict;
  }
  if (watermark != Result::kNotFound) {
    return watermark;
  }
  StoredRecord existing;
  const Result read_result =
      ReadStoredRecord(GetEntityType(record), GetEntityId(record), &existing);
  SyncRecord local = record;
  if (!StampLocalMutation(
          read_result == Result::kOk ? &existing.record : nullptr, &local,
          nullptr) ||
      !ValidateRecord(local, nullptr)) {
    return Result::kInvalidArgument;
  }
  std::string payload;
  if (!SerializeRecord(local, &payload)) {
    return Result::kInvalidArgument;
  }
  if (read_result == Result::kOk) {
    const MergeDecision decision =
        DecideMerge(GetVersion(existing.record), existing.payload,
                    GetVersion(local), payload);
    if (decision == MergeDecision::kKeepExisting) {
      return Result::kStale;
    }
    if (decision == MergeDecision::kDuplicate) {
      return Result::kAlreadyApplied;
    }
    if (decision == MergeDecision::kInvalid) {
      return Result::kConflict;
    }
  } else if (read_result != Result::kNotFound) {
    return read_result;
  }

  const SyncChange change{
      .mutation_id = mutation_id,
      .entity_type = GetEntityType(local),
      .entity_id = GetEntityId(local),
      .kind = IsTombstone(local) ? ChangeKind::kDelete : ChangeKind::kUpsert,
      .version = GetVersion(local),
      .payload = payload};
  bool outbox_present = false;
  if (!OutboxMatches(change, &outbox_present)) {
    return outbox_present ? Result::kConflict : Result::kDatabaseError;
  }
  if (outbox_present) {
    return Result::kAlreadyApplied;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  if (!UpsertRecord(local, payload) || !WriteTombstone(local) ||
      !WriteOutbox(change) || !transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

SyncStore::Result SyncStore::ApplyRemoteBatch(const ProviderBatch& batch) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }

  // Provider pages are transport batches, not complete tree snapshots. Parent
  // and child records may arrive on different pages, and an offline move can
  // race a delete. Persist individually valid records atomically here; the
  // UI-side adapter deterministically repairs orphans/cycles before the regular
  // TabTreeStore sees them. Rejecting a partial graph at this layer would pin
  // the change token forever and make eventual convergence impossible.

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  bool changed = false;
  for (const SyncChange& change : batch.changes) {
    SyncRecord incoming;
    if (!ValidateChangeEnvelope(change, &incoming) ||
        !ValidateRecord(incoming, nullptr)) {
      if (!WriteQuarantine(change, "invalid_envelope")) {
        return Result::kDatabaseError;
      }
      changed = true;
      continue;
    }

    bool present = false;
    if (!InboxMatches(change, &present)) {
      if (!present) {
        return Result::kDatabaseError;
      }
      if (!WriteQuarantine(change, "mutation_id_conflict")) {
        return Result::kDatabaseError;
      }
      changed = true;
      continue;
    }
    if (present) {
      continue;
    }

    SyncVersion deletion_watermark;
    const Result watermark = ReadDeletionWatermark(
        change.entity_type, change.entity_id, &deletion_watermark);
    if (watermark == Result::kOk) {
      if (IsTombstone(incoming) && change.version <= deletion_watermark) {
        if (!WriteInbox(change)) {
          return Result::kDatabaseError;
        }
      } else if (!WriteQuarantine(change, "resurrection_after_compaction")) {
        return Result::kDatabaseError;
      }
      changed = true;
      continue;
    }
    if (watermark != Result::kNotFound) {
      return Result::kDatabaseError;
    }

    StoredRecord existing;
    const Result read_result =
        ReadStoredRecord(change.entity_type, change.entity_id, &existing);
    SyncRecord materialized = incoming;
    std::string materialized_payload = change.payload;
    bool requeue_merged_record = false;
    if (read_result == Result::kOk) {
      const MergeDecision decision =
          MergeRecordFields(existing.record, incoming, &materialized, nullptr);
      if (decision == MergeDecision::kInvalid) {
        if (!WriteQuarantine(change, "field_conflict")) {
          return Result::kDatabaseError;
        }
        changed = true;
        continue;
      }
      if (decision == MergeDecision::kKeepExisting ||
          decision == MergeDecision::kDuplicate) {
        if (!WriteInbox(change)) {
          return Result::kDatabaseError;
        }
        continue;
      }
      requeue_merged_record = decision == MergeDecision::kMergeFields;
      if (requeue_merged_record &&
          !SerializeRecord(materialized, &materialized_payload)) {
        return Result::kDatabaseError;
      }
    } else if (read_result != Result::kNotFound) {
      return Result::kDatabaseError;
    }
    if (!WriteInbox(change) ||
        !UpsertRecord(materialized, materialized_payload) ||
        !WriteTombstone(materialized)) {
      return Result::kDatabaseError;
    }
    if (requeue_merged_record) {
      const SyncChange convergence{
          .mutation_id = base::Uuid::GenerateRandomV4().AsLowercaseString(),
          .entity_type = GetEntityType(materialized),
          .entity_id = GetEntityId(materialized),
          .kind = IsTombstone(materialized) ? ChangeKind::kDelete
                                            : ChangeKind::kUpsert,
          .version = GetVersion(materialized),
          .payload = materialized_payload};
      if (!WriteOutbox(convergence)) {
        return Result::kDatabaseError;
      }
    }
    changed = true;
  }
  if (!SetMetadata("change_token", batch.next_change_token)) {
    return Result::kDatabaseError;
  }
  if (!db_.Execute(
          "UPDATE sync_retry_state SET attempt=0,last_attempt=0,next_attempt=0,"
          "last_error='' WHERE provider_key='default'")) {
    return Result::kDatabaseError;
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  if (changed || !batch.changes.empty() || !batch.next_change_token.empty()) {
    NotifyChanged();
  }
  return Result::kOk;
}

SyncStore::Result SyncStore::GetRecord(EntityType type,
                                       const base::Uuid& id,
                                       SyncRecord* record) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!record) {
    return Result::kInvalidArgument;
  }
  StoredRecord stored;
  const Result result = ReadStoredRecord(type, id, &stored);
  if (result == Result::kOk) {
    *record = std::move(stored.record);
  }
  return result;
}

SyncStore::Result SyncStore::GetRecords(
    EntityType type,
    std::vector<SyncRecord>* records) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!records) {
    return Result::kInvalidArgument;
  }
  std::vector<StoredRecord> stored;
  if (!ReadStoredRecords(type, &stored)) {
    return Result::kDatabaseError;
  }
  records->clear();
  records->reserve(stored.size());
  for (StoredRecord& item : stored) {
    records->push_back(std::move(item.record));
  }
  return Result::kOk;
}

SyncStore::Result SyncStore::GetRemoteTabs(
    std::vector<RemoteTabRecord>* records) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!records) {
    return Result::kInvalidArgument;
  }
  std::vector<SyncRecord> all;
  const Result result = GetRecords(EntityType::kRemoteTab, &all);
  if (result != Result::kOk) {
    return result;
  }
  records->clear();
  for (SyncRecord& item : all) {
    if (RemoteTabRecord* tab = std::get_if<RemoteTabRecord>(&item)) {
      records->push_back(std::move(*tab));
    }
  }
  return Result::kOk;
}

SyncStore::Result SyncStore::ReadOutbox(size_t limit,
                                        std::vector<SyncChange>* changes,
                                        bool include_bookmarks) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!changes || limit == 0) {
    return Result::kInvalidArgument;
  }
  changes->clear();
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT mutation_id,entity_type,entity_id,change_kind,payload,"
      "version_model,version_physical,version_logical,version_device "
      "FROM sync_outbox WHERE (? OR entity_type<>?) "
      "ORDER BY created_at,mutation_id LIMIT ?"));
  // Filter before LIMIT so retained, unapproved bookmarks cannot starve later
  // records from categories that are allowed to make progress.
  statement.BindBool(0, include_bookmarks);
  statement.BindInt(1, static_cast<int>(EntityType::kBookmark));
  statement.BindInt64(2, static_cast<int64_t>(limit));
  while (statement.Step()) {
    const int type = statement.ColumnInt(1);
    const int kind = statement.ColumnInt(3);
    if (!IsValidEntityType(type) || !IsValidChangeKind(kind)) {
      return Result::kDatabaseError;
    }
    SyncChange change{
        .mutation_id = statement.ColumnString(0),
        .entity_type = static_cast<EntityType>(type),
        .entity_id = base::Uuid::ParseLowercase(statement.ColumnString(2)),
        .kind = static_cast<ChangeKind>(kind),
        .version = ReadVersion(statement, 5),
        .payload = statement.ColumnString(4)};
    if (!change.entity_id.is_valid()) {
      return Result::kDatabaseError;
    }
    SyncRecord decoded;
    if (!ValidateChangeEnvelope(change, &decoded)) {
      return Result::kDatabaseError;
    }
    changes->push_back(std::move(change));
  }
  return statement.Succeeded() ? Result::kOk : Result::kDatabaseError;
}

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
  for (const std::string& mutation_id : mutation_ids) {
    if (mutation_id.empty()) {
      return Result::kInvalidArgument;
    }
    sql::Statement statement(
        db_.GetUniqueStatement("DELETE FROM sync_outbox WHERE mutation_id=?"));
    statement.BindString(0, mutation_id);
    if (!statement.Run()) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

SyncStore::Result SyncStore::PrepareOutboxForCloudRecovery(
    bool requeue_local_records) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin() || !db_.Execute("DELETE FROM sync_outbox")) {
    return Result::kDatabaseError;
  }
  if (requeue_local_records) {
    sql::Statement statement(db_.GetUniqueStatement(
        "INSERT INTO sync_outbox(mutation_id,entity_type,entity_id,change_kind,"
        "payload,version_model,version_physical,version_logical,version_device,"
        "created_at) SELECT lower(hex(randomblob(16))),entity_type,entity_id,"
        "tombstone,payload,model_version,version_physical,version_logical,"
        "version_device,? FROM sync_records"));
    BindTime(statement, 0, base::Time::Now());
    if (!statement.Run()) {
      return Result::kDatabaseError;
    }
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

int64_t SyncStore::PendingOutboxCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return -1;
  }
  sql::Statement statement(
      db_.GetUniqueStatement("SELECT COUNT(*) FROM sync_outbox"));
  return statement.Step() ? statement.ColumnInt64(0) : -1;
}

int64_t SyncStore::InboxCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return -1;
  }
  sql::Statement statement(
      db_.GetUniqueStatement("SELECT COUNT(*) FROM sync_inbox"));
  return statement.Step() ? statement.ColumnInt64(0) : -1;
}

std::string SyncStore::GetChangeToken() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return {};
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT value FROM sync_metadata WHERE key='change_token'"));
  return statement.Step() ? statement.ColumnString(0) : std::string();
}

RetryState SyncStore::GetRetryState() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RetryState state;
  if (!IsReady()) {
    return state;
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT attempt,last_attempt,next_attempt,last_error FROM "
      "sync_retry_state WHERE provider_key='default'"));
  if (statement.Step()) {
    state.attempt = statement.ColumnInt(0);
    state.last_attempt = ReadTime(statement, 1);
    state.next_attempt = ReadTime(statement, 2);
    state.last_error = statement.ColumnString(3);
  }
  return state;
}

SyncStore::Result SyncStore::MarkRetry(base::Time next_attempt,
                                       std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  RetryState current = GetRetryState();
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO sync_retry_state(provider_key,attempt,"
      "last_attempt,next_attempt,last_error) VALUES('default',?,?,?,?)"));
  statement.BindInt(0, current.attempt + 1);
  BindTime(statement, 1, base::Time::Now());
  BindTime(statement, 2, next_attempt);
  statement.BindString(3, error);
  if (!statement.Run() || !transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

SyncStore::Result SyncStore::ClearRetry() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin() ||
      !db_.Execute(
          "UPDATE sync_retry_state SET attempt=0,last_attempt=0,next_attempt=0,"
          "last_error='' WHERE provider_key='default'") ||
      !transaction.Commit()) {
    return Result::kDatabaseError;
  }
  NotifyChanged();
  return Result::kOk;
}

void SyncStore::NotifyChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (SyncStoreObserver& observer : observers_) {
    observer.OnSyncStoreChanged();
  }
}

}  // namespace ahoi::sync
