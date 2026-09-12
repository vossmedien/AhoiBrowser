// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/tab_tree/tab_tree_store.h"

#include <initializer_list>
#include <string>

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
             "tombstone IN (0,1)),archive_policy INTEGER NOT NULL DEFAULT 0 "
             "CHECK(archive_policy IN (0,1,2,3,4)))") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS tree_nodes("
             "model_version INTEGER NOT NULL,id TEXT PRIMARY KEY NOT NULL,"
             "workspace_id TEXT NOT NULL REFERENCES workspaces(id) ON DELETE "
             "RESTRICT,parent_id TEXT REFERENCES tree_nodes(id) ON DELETE "
             "RESTRICT,node_type INTEGER NOT NULL CHECK(node_type IN (0,1)),"
             "title TEXT NOT NULL,icon TEXT NOT NULL DEFAULT '',"
             "accent_argb INTEGER,url TEXT NOT NULL,sort_key TEXT NOT NULL,"
             "created_at INTEGER NOT NULL,modified_at INTEGER NOT NULL,"
             "tombstone INTEGER NOT NULL CHECK(tombstone IN (0,1)),"
             "is_temporary INTEGER NOT NULL DEFAULT 0 CHECK("
             "is_temporary IN (0,1)),target_kind INTEGER CHECK("
             "target_kind IN (0,1,2)),local_scheme TEXT,"
             "home_url TEXT NOT NULL DEFAULT '',home_target_kind INTEGER "
             "CHECK(home_target_kind IN (0,2)),home_local_scheme TEXT)") &&
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
             "tombstone INTEGER,is_temporary INTEGER DEFAULT 0 CHECK("
             "is_temporary IN (0,1)),target_kind INTEGER CHECK("
             "target_kind IN (0,1,2)),local_scheme TEXT,"
             "home_url TEXT,home_target_kind INTEGER CHECK("
             "home_target_kind IN (0,2)),home_local_scheme TEXT,"
             "PRIMARY KEY(operation_id,node_id),"
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
  if (meta_table->GetVersionNumber() == 2) {
    if (!MigrateNodesToSchema3() || !meta_table->SetVersionNumber(3)) {
      return false;
    }
  }
  if (meta_table->GetVersionNumber() == 3) {
    // A small additive LOCAL tree upgrade, not a sync-format/data migration.
    // The pinned SQLite cannot ALTER ADD CHECK (pragma_quick_check); matching
    // read/write validation keeps these typed columns constrained. A schema2
    // upgrade may already have created the current node/undo columns above.
    for (const char* table : {"tree_nodes", "undo_node_snapshots"}) {
      for (const char* column :
           {"home_url", "home_target_kind", "home_local_scheme"}) {
        if (db_.DoesColumnExist(table, column)) {
          continue;
        }
        const std::string type =
            std::string(column) == "home_target_kind" ? " INTEGER" : " TEXT";
        const std::string defaults =
            std::string(column) == "home_url" ? " DEFAULT ''" : "";
        if (!db_.Execute("ALTER TABLE " + std::string(table) + " ADD COLUMN " +
                         column + type + defaults)) {
          return false;
        }
      }
      // Only pre-existing local saved content establishes its initial Home.
      // Navigation updates never rewrite it; temporary pages keep no default.
      if (!db_.Execute("UPDATE " + std::string(table) +
                       " SET home_url=url,home_target_kind=NULL,"
                       "home_local_scheme=NULL WHERE node_type=1 "
                       "AND is_temporary=0 AND (target_kind IS NULL OR "
                       "target_kind!=1)")) {
        return false;
      }
    }
    if ((!db_.DoesColumnExist("workspaces", "archive_policy") &&
         !db_.Execute(
             "ALTER TABLE workspaces ADD COLUMN archive_policy INTEGER "
             "NOT NULL DEFAULT 0")) ||
        !meta_table->SetVersionNumber(4) ||
        !meta_table->SetCompatibleVersionNumber(4)) {
      return false;
    }
  }
  return meta_table->GetVersionNumber() == kCurrentSchemaVersion;
}

bool TabTreeStore::MigrateNodesToSchema3() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // InitializeSchema owns the transaction and keeps foreign_keys enabled.
  // ADD COLUMN ... CHECK invokes pragma_quick_check internally, which the
  // pinned Chromium SQLite does not expose. Recreate from the same constrained
  // schema as new stores; INSERT/UPDATE enforce every constraint normally.
  if (!db_.Execute("ALTER TABLE tree_nodes RENAME TO tree_nodes_schema2") ||
      !db_.Execute("ALTER TABLE undo_node_snapshots "
                   "RENAME TO undo_node_snapshots_schema2") ||
      !CreateSchema() ||
      !db_.Execute(
          "INSERT INTO tree_nodes(model_version,id,workspace_id,parent_id,"
          "node_type,title,icon,accent_argb,url,sort_key,"
          "created_at,modified_at,tombstone,is_temporary,target_kind,"
          "local_scheme) "
          "SELECT model_version,id,workspace_id,NULL,node_type,title,icon,"
          "accent_argb,url,sort_key,created_at,modified_at,tombstone,"
          "0,NULL,NULL FROM tree_nodes_schema2") ||
      // All parents exist before edges are restored, independent of row order.
      !db_.Execute("UPDATE tree_nodes SET parent_id=(SELECT old.parent_id "
                   "FROM tree_nodes_schema2 AS old WHERE old.id=tree_nodes.id) "
                   "WHERE id IN (SELECT id FROM tree_nodes_schema2 "
                   "WHERE parent_id IS NOT NULL)") ||
      !db_.Execute(
          "INSERT INTO undo_node_snapshots(operation_id,ordinal,existed,"
          "node_id,model_version,workspace_id,parent_id,node_type,title,icon,"
          "accent_argb,url,sort_key,created_at,modified_at,tombstone,"
          "is_temporary,target_kind,local_scheme) "
          "SELECT operation_id,ordinal,existed,node_id,model_version,"
          "workspace_id,parent_id,node_type,title,icon,accent_argb,url,"
          "sort_key,created_at,modified_at,tombstone,0,NULL,NULL "
          "FROM undo_node_snapshots_schema2") ||
      // Old self-referencing ON DELETE RESTRICT edges must be detached before
      // DROP's implicit delete. The new tree already has the exact old edges;
      // any later failure rolls this and both renames/copies back together.
      !db_.Execute("UPDATE tree_nodes_schema2 SET parent_id=NULL "
                   "WHERE parent_id IS NOT NULL") ||
      !db_.Execute("DROP TABLE undo_node_snapshots_schema2") ||
      !db_.Execute("DROP TABLE tree_nodes_schema2")) {
    return false;
  }
  // The old table owned the named indexes. InitializeSchema calls CreateSchema
  // again after this migration to recreate them on the new table before commit.
  // Workspaces, undo_operations (including its sequence) and meta stay intact.
  return true;
}

bool TabTreeStore::IsReady() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return db_.is_open();
}

}  // namespace ahoi::tab_tree
