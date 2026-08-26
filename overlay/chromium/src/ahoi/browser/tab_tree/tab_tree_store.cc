// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/tab_tree/tab_tree_store.h"

#include "base/check.h"
#include "sql/meta_table.h"
#include "sql/transaction.h"

namespace ahoi::tab_tree {

TabTreeStore::TabTreeStore()
    : db_(sql::DatabaseOptions().set_flush_to_media(true),
          /*tag=*/"AhoiTabTree") {
  // The profile service may construct this store on the UI sequence before
  // transferring it to its dedicated database sequence.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

TabTreeStore::~TabTreeStore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool TabTreeStore::Initialize(const base::FilePath& path) {
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

bool TabTreeStore::InitializeInMemory() {
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

bool TabTreeStore::InitializeSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  bool initialized = false;
  if (db_.Execute("PRAGMA foreign_keys=ON")) {
    sql::Transaction transaction(&db_);
    if (transaction.Begin()) {
      sql::MetaTable meta_table;
      if (meta_table.Init(&db_, kCurrentSchemaVersion, kCurrentSchemaVersion) &&
          meta_table.GetCompatibleVersionNumber() <= kCurrentSchemaVersion &&
          meta_table.GetVersionNumber() >= kLowestSupportedSchemaVersion &&
          meta_table.GetVersionNumber() <= kCurrentSchemaVersion &&
          MigrateSchema(&meta_table)) {
        initialized = CreateSchema() && transaction.Commit();
      }
    }
  }

  return initialized;
}

void TabTreeStore::AddObserver(TabTreeObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void TabTreeStore::RemoveObserver(TabTreeObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool TabTreeStore::CreateSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return db_.Execute(
             "CREATE TABLE IF NOT EXISTS workspaces("
             "model_version INTEGER NOT NULL,id TEXT PRIMARY KEY NOT NULL,"
             "name TEXT NOT NULL,icon TEXT NOT NULL,sort_key TEXT NOT NULL,"
             "accent_argb INTEGER,created_at INTEGER NOT NULL,"
             "modified_at INTEGER NOT NULL,tombstone INTEGER NOT NULL CHECK("
             "tombstone IN (0,1)))") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS tree_nodes("
             "model_version INTEGER NOT NULL,id TEXT PRIMARY KEY NOT NULL,"
             "workspace_id TEXT NOT NULL REFERENCES workspaces(id) ON DELETE "
             "RESTRICT,parent_id TEXT REFERENCES tree_nodes(id) ON DELETE "
             "RESTRICT,node_type INTEGER NOT NULL CHECK(node_type IN (0,1)),"
             "title TEXT NOT NULL,icon TEXT NOT NULL DEFAULT '',"
             "accent_argb INTEGER,url TEXT NOT NULL,sort_key TEXT NOT NULL,"
             "created_at INTEGER NOT NULL,modified_at INTEGER NOT NULL,"
             "tombstone INTEGER NOT NULL CHECK(tombstone IN (0,1)))") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS tree_nodes_parent_order ON "
             "tree_nodes(workspace_id,parent_id,tombstone,sort_key,id)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS tree_nodes_url_lookup ON "
             "tree_nodes(workspace_id,node_type,tombstone,url,id)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS undo_operations("
             "operation_id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "mutation_kind INTEGER NOT NULL CHECK(mutation_kind IN (0,1,2,3)),"
             "subject_node_id TEXT NOT NULL,created_at INTEGER NOT NULL)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS undo_node_snapshots("
             "operation_id INTEGER NOT NULL REFERENCES undo_operations("
             "operation_id) ON DELETE CASCADE,ordinal INTEGER NOT NULL,"
             "existed INTEGER NOT NULL CHECK(existed IN (0,1)),"
             "node_id TEXT NOT NULL,model_version INTEGER,workspace_id TEXT,"
             "parent_id TEXT,node_type INTEGER,title TEXT,icon TEXT,"
             "accent_argb INTEGER,url TEXT,"
             "sort_key TEXT,created_at INTEGER,modified_at INTEGER,"
             "tombstone INTEGER,PRIMARY KEY(operation_id,node_id),"
             "UNIQUE(operation_id,ordinal))");
}

bool TabTreeStore::MigrateSchema(sql::MetaTable* meta_table) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(meta_table);
  if (meta_table->GetVersionNumber() == 1) {
    if (!db_.Execute("ALTER TABLE tree_nodes ADD COLUMN icon TEXT NOT NULL "
                     "DEFAULT ''") ||
        !db_.Execute("ALTER TABLE tree_nodes ADD COLUMN accent_argb INTEGER") ||
        !db_.Execute("ALTER TABLE undo_node_snapshots ADD COLUMN icon TEXT") ||
        !db_.Execute(
            "ALTER TABLE undo_node_snapshots ADD COLUMN accent_argb INTEGER") ||
        !meta_table->SetVersionNumber(2)) {
      return false;
    }
  }
  return meta_table->GetVersionNumber() == kCurrentSchemaVersion;
}

bool TabTreeStore::IsReady() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return db_.is_open();
}

}  // namespace ahoi::tab_tree
