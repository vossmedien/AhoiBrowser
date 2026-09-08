// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_store.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::sync {

bool SyncStore::InitializeSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_.Execute("PRAGMA foreign_keys=ON")) {
    return false;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }
  // Pre-launch format reset: do not stamp an existing unrelated/old database
  // as a fresh store merely because it lacks our metadata table.
  if (!sql::MetaTable::DoesTableExist(&db_)) {
    sql::Statement tables(db_.GetUniqueStatement(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%'"));
    if (!tables.Step() || tables.ColumnInt(0) != 0) {
      return false;
    }
  }
  sql::MetaTable meta_table;
  if (!meta_table.Init(&db_, kCurrentSchemaVersion, kCurrentSchemaVersion) ||
      meta_table.GetCompatibleVersionNumber() != kCurrentSchemaVersion ||
      meta_table.GetVersionNumber() != kCurrentSchemaVersion ||
      !CreateSchema()) {
    return false;
  }
  return transaction.Commit();
}

bool SyncStore::CreateSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_native_tree_receipts("
             "receipt_id TEXT PRIMARY KEY NOT NULL,payload TEXT NOT NULL)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_native_tree_observations("
             "receipt_id TEXT PRIMARY KEY NOT NULL,payload TEXT NOT NULL)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_acknowledged_records("
             "entity_type INTEGER NOT NULL,entity_id TEXT NOT NULL,"
             "version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,"
             "version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,"
             "PRIMARY KEY(entity_type,entity_id))") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_bookmark_bindings("
             "native_key TEXT PRIMARY KEY NOT NULL,entity_id TEXT NOT NULL,"
             "baseline TEXT NOT NULL,native_index INTEGER NOT NULL "
             "CHECK(native_index>=0),materialized INTEGER NOT NULL "
             "CHECK(materialized IN (0,1)),last_observed TEXT NOT NULL DEFAULT "
             "'',"
             "observed_receipt TEXT NOT NULL DEFAULT '',"
             "observation_session TEXT NOT NULL DEFAULT '')") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_bookmark_apply_receipts("
             "receipt_id TEXT PRIMARY KEY NOT NULL,entity_id TEXT NOT NULL,"
             "payload TEXT NOT NULL)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS sync_bookmark_receipts_entity "
             "ON sync_bookmark_apply_receipts(entity_id)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_records("
             "entity_type INTEGER NOT NULL CHECK(entity_type BETWEEN 0 AND 12),"
             "entity_id TEXT NOT NULL,payload TEXT NOT NULL,tombstone INTEGER "
             "NOT NULL CHECK(tombstone IN (0,1)),model_version INTEGER NOT "
             "NULL,"
             "version_physical INTEGER NOT NULL,version_logical INTEGER NOT "
             "NULL,"
             "version_device TEXT NOT NULL,PRIMARY "
             "KEY(entity_type,entity_id))") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS sync_records_type_order ON "
             "sync_records(entity_type,tombstone,version_physical,entity_"
             "id)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_tombstones("
             "entity_type INTEGER NOT NULL,entity_id TEXT NOT NULL,"
             "version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,"
             "version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,"
             "deleted_at INTEGER NOT NULL,PRIMARY "
             "KEY(entity_type,entity_id))") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_deletion_watermarks("
             "entity_type INTEGER NOT NULL,entity_id TEXT NOT NULL,"
             "version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,"
             "version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,"
             "compacted_at INTEGER NOT NULL,PRIMARY "
             "KEY(entity_type,entity_id))") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_outbox("
             "mutation_id TEXT PRIMARY KEY NOT NULL,entity_type INTEGER NOT "
             "NULL,"
             "entity_id TEXT NOT NULL,change_kind INTEGER NOT NULL,payload "
             "TEXT "
             "NOT NULL,version_model INTEGER NOT NULL,version_physical INTEGER "
             "NOT NULL,version_logical INTEGER NOT NULL,version_device TEXT "
             "NOT "
             "NULL,created_at INTEGER NOT NULL)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS sync_outbox_order ON "
             "sync_outbox(created_at,mutation_id)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_inbox("
             "mutation_id TEXT PRIMARY KEY NOT NULL,entity_type INTEGER NOT "
             "NULL,"
             "entity_id TEXT NOT NULL,change_kind INTEGER NOT NULL,payload "
             "TEXT "
             "NOT NULL,version_model INTEGER NOT NULL,version_physical INTEGER "
             "NOT NULL,version_logical INTEGER NOT NULL,version_device TEXT "
             "NOT "
             "NULL,received_at INTEGER NOT NULL)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_quarantine("
             "quarantine_id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "mutation_id TEXT NOT NULL,entity_type INTEGER NOT NULL,"
             "entity_id TEXT NOT NULL,reason TEXT NOT NULL,payload BLOB NOT "
             "NULL,"
             "received_at INTEGER NOT NULL)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS sync_quarantine_received ON "
             "sync_quarantine(received_at,quarantine_id)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_metadata("
             "key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_retry_state("
             "provider_key TEXT PRIMARY KEY NOT NULL,attempt INTEGER NOT NULL,"
             "last_attempt INTEGER NOT NULL,next_attempt INTEGER NOT NULL,"
             "last_error TEXT NOT NULL)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS sync_command_replay("
             "command_id TEXT PRIMARY KEY NOT NULL,"
             "source_device_id TEXT NOT NULL,nonce TEXT NOT NULL,"
             "expires_at INTEGER NOT NULL,consumed_at INTEGER NOT NULL,"
             "UNIQUE(source_device_id,nonce))") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS sync_command_replay_expiry ON "
             "sync_command_replay(expires_at)") &&
         db_.Execute(
             "INSERT OR IGNORE INTO sync_metadata(key,value) VALUES"
             "('change_token','')") &&
         db_.Execute(
             "INSERT OR IGNORE INTO sync_retry_state(provider_key,attempt,"
             "last_attempt,next_attempt,last_error) "
             "VALUES('default',0,0,0,'')");
}

}  // namespace ahoi::sync
