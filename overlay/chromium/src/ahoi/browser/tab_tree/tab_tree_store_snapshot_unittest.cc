// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

// TabTreeStore snapshot round trips and schema upgrades: tree, tombstones
// and undo history survive, a failed replacement keeps the old tree, and a
// too-new schema is refused without razing data (split from
// tab_tree_store_unittest.cc, source line budget).

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/tab_tree/tab_tree_store_unittest_support.h"
#include "base/files/file_path.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/test/scoped_error_expecter.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::tab_tree {

namespace {

using test_support::AhoiTabTreeStoreTest;

TEST_F(AhoiTabTreeStoreTest, SnapshotRoundTripsTreeTombstonesAndUndoHistory) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode folder = NewFolder(workspace, std::nullopt, u"Project", "folder-a");
  TreeNode page = NewSavedPage(workspace, folder.id, u"Before",
                               GURL("https://example.test/"), "page-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(folder));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->RenameNode(page.id, u"After", base::Time::Now()));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->DeleteNode(folder.id, base::Time::Now()));

  TabTreeSnapshot snapshot;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->ExportSnapshot(&snapshot));
  ASSERT_EQ(1u, snapshot.workspaces.size());
  ASSERT_EQ(2u, snapshot.nodes.size());
  ASSERT_EQ(4u, snapshot.undo_operations.size());

  TabTreeStore restored;
  ASSERT_TRUE(restored.InitializeInMemory());
  ASSERT_EQ(TabTreeStore::Result::kOk, restored.ReplaceWithSnapshot(snapshot));
  TabTreeSnapshot restored_snapshot;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            restored.ExportSnapshot(&restored_snapshot));
  EXPECT_EQ(snapshot, restored_snapshot);

  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->UpdateWorkspacePresentation(
                workspace.id, u"Renamed development", u"code", std::nullopt,
                base::Time::Now()));
  Workspace imported = NewWorkspace(u"Imported", "workspace-b");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(imported));
  TreeNode imported_folder =
      NewFolder(imported, std::nullopt, u"Imported project", "folder-a");
  TreeNode imported_page =
      NewSavedPage(imported, imported_folder.id, u"Imported page",
                   GURL("https://example.test/imported"), "page-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(imported_folder));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(imported_page));
  TabTreeSnapshot updated_snapshot;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->ExportSnapshot(&updated_snapshot));
  ASSERT_EQ(2u, updated_snapshot.workspaces.size());
  ASSERT_EQ(4u, updated_snapshot.nodes.size());

  // Repeat against an occupied store, including replacing its workspace set.
  // The old bulk DELETE failed on parent links even for an identical rewrite.
  for (const TabTreeSnapshot *replacement :
       {&snapshot, &updated_snapshot, &snapshot}) {
    ASSERT_EQ(TabTreeStore::Result::kOk,
              restored.ReplaceWithSnapshot(*replacement));
    TabTreeSnapshot rewritten;
    ASSERT_EQ(TabTreeStore::Result::kOk, restored.ExportSnapshot(&rewritten));
    EXPECT_EQ(*replacement, rewritten);
  }

  ASSERT_EQ(TabTreeStore::Result::kOk, restored.UndoLastMutation());
  TreeNode restored_page;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            restored.GetNode(page.id, &restored_page));
  EXPECT_FALSE(restored_page.tombstone);
  EXPECT_EQ(u"After", restored_page.title);
  ASSERT_EQ(TabTreeStore::Result::kOk, restored.UndoLastMutation());
  ASSERT_EQ(TabTreeStore::Result::kOk,
            restored.GetNode(page.id, &restored_page));
  EXPECT_EQ(u"Before", restored_page.title);
}

TEST_F(AhoiTabTreeStoreTest, SnapshotReplacementFailurePreservesExistingTree) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode folder = NewFolder(workspace, std::nullopt, u"Project", "folder-a");
  TreeNode page = NewSavedPage(workspace, folder.id, u"Saved page",
                               GURL("https://example.test/"), "page-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(folder));
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  TabTreeSnapshot before;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->ExportSnapshot(&before));
  ASSERT_FALSE(before.undo_operations.empty());
  ASSERT_FALSE(before.undo_operations.front().nodes.empty());

  TabTreeSnapshot invalid = before;
  invalid.workspaces.front().name = u"Must not persist";
  // This duplicate reaches the SQL uniqueness check after tree replacement.
  // Rollback must restore the old parent links as well as the undo history.
  auto &undo_nodes = invalid.undo_operations.front().nodes;
  undo_nodes.push_back(undo_nodes.front());
  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CONSTRAINT_PRIMARYKEY);
    EXPECT_EQ(TabTreeStore::Result::kDatabaseError,
              store_->ReplaceWithSnapshot(invalid));
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }

  store_.reset();
  ASSERT_TRUE(ReopenStore());
  TabTreeSnapshot after;
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->ExportSnapshot(&after));
  EXPECT_EQ(before, after);
}

TEST_F(AhoiTabTreeStoreTest, RejectsSavedPageAsParent) {
  Workspace workspace = NewWorkspace(u"Development", "workspace-a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
  TreeNode page = NewSavedPage(workspace, std::nullopt, u"Page",
                               GURL("https://example.test/"), "a");
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateNode(page));
  TreeNode invalid_child = NewFolder(workspace, page.id, u"Invalid child", "a");
  EXPECT_EQ(TabTreeStore::Result::kInvalidArgument,
            store_->CreateNode(invalid_child));
}

TEST_F(AhoiTabTreeStoreTest, RefusesTooNewSchemaWithoutRazingData) {
  store_.reset();
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(database_path_));
    sql::MetaTable meta_table;
    ASSERT_TRUE(meta_table.Init(&database, TabTreeStore::kCurrentSchemaVersion,
                                TabTreeStore::kCurrentSchemaVersion));
    ASSERT_TRUE(
        meta_table.SetVersionNumber(TabTreeStore::kCurrentSchemaVersion + 1));
    ASSERT_TRUE(meta_table.SetCompatibleVersionNumber(
        TabTreeStore::kCurrentSchemaVersion + 1));
    ASSERT_TRUE(database.Execute(
        "CREATE TABLE schema_sentinel(value INTEGER NOT NULL)"));
  }

  store_ = std::make_unique<TabTreeStore>();
  EXPECT_FALSE(store_->Initialize(database_path_));
  store_.reset();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(database_path_));
  EXPECT_TRUE(database.DoesTableExist("schema_sentinel"));
}

TEST_F(AhoiTabTreeStoreTest, Schema2UpgradeKeepsNestedRowsUndoAndConstraints) {
  const base::FilePath legacy_path =
      temp_dir_.GetPath().AppendASCII("Schema2.sqlite");
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(legacy_path));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&database, 2, 2));
    ASSERT_TRUE(database.Execute(R"sql(
      PRAGMA foreign_keys=ON;
      CREATE TABLE workspaces(model_version INTEGER NOT NULL,
        id TEXT PRIMARY KEY NOT NULL,name TEXT NOT NULL,icon TEXT NOT NULL,
        sort_key TEXT NOT NULL,accent_argb INTEGER,created_at INTEGER NOT NULL,
        modified_at INTEGER NOT NULL,tombstone INTEGER NOT NULL
        CHECK(tombstone IN (0,1)));
      CREATE TABLE tree_nodes(model_version INTEGER NOT NULL,
        id TEXT PRIMARY KEY NOT NULL,workspace_id TEXT NOT NULL
        REFERENCES workspaces(id) ON DELETE RESTRICT,
        parent_id TEXT REFERENCES tree_nodes(id) ON DELETE RESTRICT,
        node_type INTEGER NOT NULL CHECK(node_type IN (0,1)),title TEXT NOT NULL,
        icon TEXT NOT NULL DEFAULT '',accent_argb INTEGER,url TEXT NOT NULL,
        sort_key TEXT NOT NULL,created_at INTEGER NOT NULL,
        modified_at INTEGER NOT NULL,tombstone INTEGER NOT NULL
        CHECK(tombstone IN (0,1)));
      CREATE INDEX tree_nodes_parent_order ON
        tree_nodes(workspace_id,parent_id,tombstone,sort_key,id);
      CREATE INDEX tree_nodes_url_lookup ON
        tree_nodes(workspace_id,node_type,tombstone,url,id);
      CREATE TABLE undo_operations(operation_id INTEGER PRIMARY KEY
        AUTOINCREMENT,mutation_kind INTEGER NOT NULL
        CHECK(mutation_kind IN (0,1,2,3)),subject_node_id TEXT NOT NULL,
        created_at INTEGER NOT NULL);
      CREATE TABLE undo_node_snapshots(operation_id INTEGER NOT NULL
        REFERENCES undo_operations(operation_id) ON DELETE CASCADE,
        ordinal INTEGER NOT NULL,existed INTEGER NOT NULL CHECK(existed IN (0,1)),
        node_id TEXT NOT NULL,model_version INTEGER,workspace_id TEXT,
        parent_id TEXT,node_type INTEGER,title TEXT,icon TEXT,accent_argb INTEGER,
        url TEXT,sort_key TEXT,created_at INTEGER,modified_at INTEGER,
        tombstone INTEGER,PRIMARY KEY(operation_id,node_id),
        UNIQUE(operation_id,ordinal));
      INSERT INTO workspaces VALUES(1,'10000000-0000-4000-8000-000000000001',
        'Workspace','folder','a',NULL,1,1,0);
      INSERT INTO tree_nodes VALUES(1,'20000000-0000-4000-8000-000000000001',
        '10000000-0000-4000-8000-000000000001',NULL,0,'Folder','code',42,'','a',1,2,1);
      INSERT INTO tree_nodes VALUES(1,'30000000-0000-4000-8000-000000000001',
        '10000000-0000-4000-8000-000000000001',
        '20000000-0000-4000-8000-000000000001',1,'Page','',NULL,
        'https://example.test/','b',1,2,1);
      INSERT INTO undo_operations VALUES(7,3,
        '20000000-0000-4000-8000-000000000001',2);
      INSERT INTO undo_node_snapshots
        SELECT 7,0,1,id,model_version,workspace_id,parent_id,node_type,title,
          icon,accent_argb,url,sort_key,created_at,1,0 FROM tree_nodes
        WHERE node_type=0;
      INSERT INTO undo_node_snapshots
        SELECT 7,1,1,id,model_version,workspace_id,parent_id,node_type,title,
          icon,accent_argb,url,sort_key,created_at,1,0 FROM tree_nodes
        WHERE node_type=1;
      INSERT INTO meta VALUES('sync_baseline_receipt','retained-baseline');
    )sql"));
  }

  {
    TabTreeStore migrated;
    ASSERT_TRUE(migrated.Initialize(legacy_path));
    TabTreeStore::PersistenceSnapshot snapshot;
    ASSERT_EQ(TabTreeStore::Result::kOk,
              migrated.ExportPersistenceSnapshot(&snapshot));
    EXPECT_EQ("retained-baseline", snapshot.sync_baseline_receipt);
    ASSERT_EQ(1u, snapshot.tree.workspaces.size());
    ASSERT_EQ(2u, snapshot.tree.nodes.size());
    ASSERT_EQ(1u, snapshot.tree.undo_operations.size());
    EXPECT_EQ(7, snapshot.tree.undo_operations.front().operation_id);
    EXPECT_EQ(snapshot.tree.nodes.front().id,
              snapshot.tree.nodes.back().parent_id);
    EXPECT_EQ(u"code", snapshot.tree.nodes.front().icon);
    EXPECT_EQ(42u, snapshot.tree.nodes.front().accent_argb);
    EXPECT_TRUE(snapshot.tree.nodes.back().tombstone);
    EXPECT_FALSE(snapshot.tree.nodes.back().is_temporary);
    EXPECT_FALSE(snapshot.tree.nodes.back().target_kind);
    EXPECT_FALSE(snapshot.tree.nodes.back().local_scheme);
    ASSERT_EQ(TabTreeStore::Result::kOk, migrated.UndoLastMutation());
    TreeNode page;
    ASSERT_EQ(TabTreeStore::Result::kOk,
              migrated.GetNode(snapshot.tree.nodes.back().id, &page));
    EXPECT_FALSE(page.tombstone);
    EXPECT_EQ(GURL("https://example.test/"), page.url);
    EXPECT_EQ(snapshot.tree.nodes.front().id, page.parent_id);
    const TreeNode next =
        NewSavedPage(snapshot.tree.workspaces.front(), page.parent_id, u"Next",
                     GURL("https://example.test/next"), "c");
    ASSERT_EQ(TabTreeStore::Result::kOk, migrated.CreateNode(next));
    ASSERT_EQ(TabTreeStore::Result::kOk,
              migrated.ExportPersistenceSnapshot(&snapshot));
    EXPECT_EQ(8, snapshot.tree.undo_operations.back().operation_id);
  }
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(legacy_path));
  ASSERT_TRUE(database.Execute("PRAGMA foreign_keys=ON"));
  EXPECT_TRUE(database.DoesIndexExist("tree_nodes_parent_order"));
  EXPECT_TRUE(database.DoesIndexExist("tree_nodes_url_lookup"));
  EXPECT_FALSE(database.DoesTableExist("tree_nodes_schema2"));
  EXPECT_FALSE(database.DoesTableExist("undo_node_snapshots_schema2"));
  {
    sql::test::ScopedErrorExpecter errors;
    errors.ExpectError(SQLITE_CONSTRAINT_CHECK);
    EXPECT_FALSE(database.Execute("UPDATE tree_nodes SET is_temporary=2"));
    EXPECT_TRUE(errors.SawExpectedErrors());
  }
  {
    sql::test::ScopedErrorExpecter errors;
    errors.ExpectError(SQLITE_CONSTRAINT_FOREIGNKEY);
    EXPECT_FALSE(database.Execute("UPDATE tree_nodes SET parent_id='missing'"));
    EXPECT_TRUE(errors.SawExpectedErrors());
  }
}

}  // namespace

}  // namespace ahoi::tab_tree
