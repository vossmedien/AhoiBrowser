// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <vector>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/test/scoped_error_expecter.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using Result = SyncStore::Result;

base::Uuid Id(unsigned value) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("b0000000-0000-4000-8000-%012x", value));
}

base::Time At() {
  return base::Time::UnixEpoch() + base::Seconds(1);
}

SyncVersion Version() {
  return {.stamp = {.physical_time_us = kMinimumSyncClockPhysicalUs + 1000000,
                    .device_tiebreak = Id(1).AsLowercaseString()}};
}

std::vector<SyncRecord> CaptureRows() {
  const DeviceRecord device{.id = Id(1),
                            .type = DeviceType::kMacDesktop,
                            .display_name = "Fixture Mac",
                            .created_at = At(),
                            .last_seen = At(),
                            .version = Version()};
  const WorkspaceRecord workspace{.id = Id(2),
                                  .name = "Fixture workspace",
                                  .sort_key = "0",
                                  .created_at = At(),
                                  .modified_at = At(),
                                  .version = Version()};
  const TreeNodeRecord page{.id = Id(3),
                            .workspace_id = Id(2),
                            .kind = TreeNodeKind::kPage,
                            .title = "Shared temporary page",
                            .url = "https://example.test/shared",
                            .sort_key = "0",
                            .created_at = At(),
                            .modified_at = At(),
                            .version = Version(),
                            .is_temporary = true,
                            .target_kind = SharedTabTargetKind::kWeb};
  const RemoteTabRecord presence{.id = Id(4),
                                 .device_id = Id(1),
                                 .session_id = Id(5),
                                 .workspace_id = Id(2),
                                 .url = page.url,
                                 .title = page.title,
                                 .opened_at = At(),
                                 .last_active = At(),
                                 .version = Version(),
                                 .tree_node_id = page.id,
                                 .target_kind = page.target_kind};
  // Presence first deliberately tests post-batch, rather than append-order,
  // reference validation. The caller does not need a second transaction.
  return {presence, page, workspace, device};
}

SyncAuthorization Approved() {
  return base::BindRepeating([] { return true; });
}

class StoreObserver : public SyncStoreObserver {
 public:
  void OnSyncStoreChanged() override { ++notifications; }
  int notifications = 0;
};

TEST(UnifiedSyncStoreTest, CompleteCaptureIsOneCommitAndOneNotification) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  StoreObserver observer;
  store.AddObserver(&observer);
  EXPECT_EQ(Result::kOk, store.PutLocalBatch(CaptureRows(), Approved()));
  EXPECT_EQ(1, observer.notifications);
  EXPECT_EQ(4, store.PendingOutboxCount());
  SyncRecord page;
  ASSERT_EQ(Result::kOk, store.GetRecord(EntityType::kTreeNode, Id(3), &page));
  EXPECT_TRUE(std::get<TreeNodeRecord>(page).is_temporary);
  EXPECT_TRUE(HasCompleteFieldVersions(page));
  store.RemoveObserver(&observer);
}

TEST(UnifiedSyncStoreTest, RevocationAtFinalBoundaryRollsBackEveryRow) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  auto checks = std::make_shared<int>(0);
  auto authorization = base::BindRepeating(
      [](std::shared_ptr<int> count) { return ++*count == 1; }, checks);
  EXPECT_EQ(Result::kNotAuthorized,
            store.PutLocalBatch(CaptureRows(), authorization));
  EXPECT_EQ(2, *checks);
  EXPECT_EQ(0, store.PendingOutboxCount());
  SyncRecord row;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kTreeNode, Id(3), &row));
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kRemoteTab, Id(4), &row));
  EXPECT_EQ(Result::kOk, store.PutLocalBatch(CaptureRows(), Approved()));
}

TEST(UnifiedSyncStoreTest, MissingPageNeverCommitsAnUnlinkedLocalPresence) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const auto rows = CaptureRows();
  EXPECT_EQ(Result::kInvalidArgument,
            store.PutLocalBatch({rows.front()}, Approved()));
  EXPECT_EQ(0, store.PendingOutboxCount());
  SyncRecord row;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kRemoteTab, Id(4), &row));
}

TEST(UnifiedSyncStoreTest, LateSqlFailureRollsBackEarlierRowsAndOutbox) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("format3.sqlite");
  SyncStore store;
  ASSERT_TRUE(store.Initialize(path));
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    ASSERT_TRUE(database.Execute(
        "CREATE TRIGGER fixture_fail_last BEFORE INSERT ON sync_records "
        "WHEN NEW.entity_type=0 BEGIN SELECT RAISE(ABORT,'fixture'); END"));
  }
  sql::test::ScopedErrorExpecter errors;
  errors.ExpectError(SQLITE_CONSTRAINT_TRIGGER);
  EXPECT_EQ(Result::kDatabaseError,
            store.PutLocalBatch(CaptureRows(), Approved()));
  EXPECT_TRUE(errors.SawExpectedErrors());
  EXPECT_EQ(0, store.PendingOutboxCount());
  SyncRecord row;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kTreeNode, Id(3), &row));
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kRemoteTab, Id(4), &row));
}

TEST(UnifiedSyncStoreTest, OldDevelopmentStoreIsRefusedWithoutMigration) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("old-development.sqlite");
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&database, 5, 5));
    ASSERT_TRUE(
        database.Execute("CREATE TABLE preserved(value TEXT NOT NULL)"));
    ASSERT_TRUE(database.Execute("INSERT INTO preserved VALUES('unchanged')"));
  }
  {
    SyncStore store;
    EXPECT_FALSE(store.Initialize(path));
  }
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&database, 5, 5));
    EXPECT_EQ(5, meta.GetVersionNumber());
    sql::Statement query(
        database.GetUniqueStatement("SELECT value FROM preserved"));
    ASSERT_TRUE(query.Step());
    EXPECT_EQ("unchanged", query.ColumnString(0));
  }
  SyncStore fresh;
  EXPECT_TRUE(fresh.Initialize(
      directory.GetPath().AppendASCII("fresh-format3.sqlite")));
}

TEST(UnifiedSyncStoreTest, TwoNodeCycleRollsBackTheWholeBatch) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  TreeNodeRecord first{.id = Id(10),
                       .workspace_id = Id(2),
                       .parent_id = Id(11),
                       .title = "First folder",
                       .sort_key = "A",
                       .created_at = At(),
                       .modified_at = At(),
                       .version = Version()};
  auto second = first;
  second.id = Id(11);
  second.parent_id = first.id;
  second.title = "Second folder";
  EXPECT_TRUE(ValidateRecord(first));
  EXPECT_TRUE(ValidateRecord(second));
  EXPECT_EQ(Result::kInvalidArgument,
            store.PutLocalBatch({first, second}, Approved()));
  EXPECT_EQ(0, store.PendingOutboxCount());
  SyncRecord row;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kTreeNode, first.id, &row));
}

TEST(UnifiedSyncStoreTest,
     IdempotentPresenceReplayDoesNotReauthorizeOldTarget) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const auto rows = CaptureRows();
  ASSERT_EQ(Result::kOk, store.PutLocalBatch(rows, Approved()));
  auto page = std::get<TreeNodeRecord>(rows[1]);
  page.url = "https://example.test/new-target";
  page.version.stamp.logical = 1;
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(page));
  const auto before = store.PendingOutboxCount();
  EXPECT_EQ(Result::kAlreadyApplied, store.PutLocalRecord(rows.front()));
  EXPECT_EQ(before, store.PendingOutboxCount());
  SyncRecord stored;
  ASSERT_EQ(Result::kOk,
            store.GetRecord(EntityType::kTreeNode, page.id, &stored));
  EXPECT_EQ(page.url, std::get<TreeNodeRecord>(stored).url);
}

}  // namespace
}  // namespace ahoi::sync
