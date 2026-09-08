// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/scoped_error_expecter.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::tab_tree {
namespace {

class AhoiTabTreePersistenceTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    path_ = directory_.GetPath().AppendASCII("Tree.sqlite");
    store_ = std::make_unique<TabTreeStore>();
    ASSERT_TRUE(store_->Initialize(path_));
    Workspace workspace;
    workspace.id = base::Uuid::GenerateRandomV4();
    workspace.name = u"Local workspace";
    workspace.sort_key = "a";
    workspace.created_at = base::Time::Now();
    workspace.modified_at = workspace.created_at;
    ASSERT_EQ(TabTreeStore::Result::kOk, store_->CreateWorkspace(workspace));
    ASSERT_EQ(TabTreeStore::Result::kOk,
              store_->ExportPersistenceSnapshot(&initial_));
    ASSERT_TRUE(initial_.sync_baseline_receipt.empty());
  }

  base::ScopedTempDir directory_;
  base::FilePath path_;
  std::unique_ptr<TabTreeStore> store_;
  TabTreeStore::PersistenceSnapshot initial_;
};

TEST_F(AhoiTabTreePersistenceTest, ReceiptSurvivesLocalEditsAndDiskReload) {
  auto applied = initial_;
  applied.sync_baseline_receipt = "opaque-baseline";
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->ReplacePersistenceSnapshot(applied));
  auto local = applied.tree;
  local.workspaces.front().name = u"Local import";
  ASSERT_EQ(TabTreeStore::Result::kOk, store_->ReplaceWithSnapshot(local));
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->UpdateWorkspacePresentation(
                local.workspaces.front().id, u"Local edit", u"code",
                std::nullopt, base::Time::Now()));

  TabTreeStore::PersistenceSnapshot edited;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->ExportPersistenceSnapshot(&edited));
  EXPECT_EQ(applied.sync_baseline_receipt, edited.sync_baseline_receipt);
  EXPECT_EQ(u"Local edit", edited.tree.workspaces.front().name);
  store_.reset();
  TabTreeStore reloaded;
  ASSERT_TRUE(reloaded.Initialize(path_));
  TabTreeStore::PersistenceSnapshot persisted;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            reloaded.ExportPersistenceSnapshot(&persisted));
  EXPECT_EQ(edited, persisted);
}

TEST_F(AhoiTabTreePersistenceTest, ReceiptWriteFailureRollsBackTheWholeTree) {
  initial_.sync_baseline_receipt = "previous-baseline";
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->ReplacePersistenceSnapshot(initial_));
  store_.reset();
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path_));
    ASSERT_TRUE(
        database.Execute("CREATE TRIGGER reject_receipt BEFORE INSERT ON meta "
                         "WHEN NEW.key='sync_baseline_receipt' "
                         "BEGIN SELECT RAISE(ABORT, 'receipt rejected'); END"));
  }
  store_ = std::make_unique<TabTreeStore>();
  ASSERT_TRUE(store_->Initialize(path_));
  auto rejected = initial_;
  rejected.tree.workspaces.front().name = u"Must not persist";
  rejected.sync_baseline_receipt = "rejected-baseline";
  {
    sql::test::ScopedErrorExpecter errors;
    errors.ExpectError(SQLITE_CONSTRAINT_TRIGGER);
    EXPECT_EQ(TabTreeStore::Result::kDatabaseError,
              store_->ReplacePersistenceSnapshot(rejected));
    EXPECT_TRUE(errors.SawExpectedErrors());
  }
  store_.reset();
  TabTreeStore reloaded;
  ASSERT_TRUE(reloaded.Initialize(path_));
  TabTreeStore::PersistenceSnapshot persisted;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            reloaded.ExportPersistenceSnapshot(&persisted));
  EXPECT_EQ(initial_, persisted);
}

TEST_F(AhoiTabTreePersistenceTest, ReadingOldStoreDoesNotWriteAnEmptyReceipt) {
  store_.reset();
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path_));
  sql::Statement receipt(database.GetUniqueStatement(
      "SELECT value FROM meta WHERE key='sync_baseline_receipt'"));
  EXPECT_FALSE(receipt.Step());
  EXPECT_TRUE(receipt.Succeeded());
}

TEST_F(AhoiTabTreePersistenceTest, RevocationBeforeCommitKeepsTreeAndBaseline) {
  auto rejected = initial_;
  rejected.tree.workspaces.front().name = u"Must not persist";
  rejected.sync_baseline_receipt = "revoked-baseline";
  int checks = 0;
  EXPECT_EQ(TabTreeStore::Result::kCancelled,
            store_->ReplacePersistenceSnapshot(
                rejected, base::BindLambdaForTesting(
                              [&checks] { return ++checks == 1; })));
  EXPECT_EQ(2, checks);
  TabTreeStore::PersistenceSnapshot after;
  ASSERT_EQ(TabTreeStore::Result::kOk,
            store_->ExportPersistenceSnapshot(&after));
  EXPECT_EQ(initial_, after);
}

}  // namespace
}  // namespace ahoi::tab_tree
