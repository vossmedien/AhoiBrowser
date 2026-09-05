// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/cstring_view.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using Result = SyncStore::Result;
using Rows = std::vector<std::vector<std::string>>;

base::Uuid Id(unsigned value) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("92000000-0000-4000-8000-%012x", value));
}

base::Time At(int64_t micros) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
}

SyncVersion Version(const char* device, int64_t physical) {
  return {.stamp = {.physical_time_us = physical, .device_tiebreak = device}};
}

BookmarkRecord Page(unsigned id = 1) {
  return {.id = Id(id),
          .kind = BookmarkKind::kUrl,
          .root_kind = BookmarkRoot::kOther,
          .sort_key = "a",
          .title = "Saved 海",
          .url = "https://example.test/bookmark",
          .created_at = At(10),
          .version = Version("local-device", 100)};
}

SyncChange ChangeFor(const SyncRecord& record, const char* mutation_id) {
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  return {
      .mutation_id = mutation_id,
      .entity_type = GetEntityType(record),
      .entity_id = GetEntityId(record),
      .kind = IsTombstone(record) ? ChangeKind::kDelete : ChangeKind::kUpsert,
      .version = GetVersion(record),
      .payload = std::move(payload)};
}

Rows ReadRows(sql::Database& database, base::cstring_view query) {
  sql::Statement statement(database.GetUniqueStatement(query));
  Rows rows;
  while (statement.Step()) {
    std::vector<std::string> row;
    for (int column = 0; column < statement.ColumnCount(); ++column) {
      row.push_back(statement.ColumnString(column));
    }
    rows.push_back(std::move(row));
  }
  EXPECT_TRUE(statement.Succeeded());
  return rows;
}

// Frozen v4 wire-v1 rows intentionally include whitespace and escaped content.
// A schema migration must preserve these bytes, not decode and reserialize
// them.
constexpr char kLegacyAppearanceId[] = "a2000000-0000-4000-8000-000000000001";
constexpr char kLegacyAppearancePayload[] = R"json( {
  "model_version":1, "id":"a2000000-0000-4000-8000-000000000001",
  "tombstone":false, "version_model":1, "version_physical":"100",
  "version_logical":0, "version_device":"legacy-device",
  "color_mode":"dark", "use_system_accent":false
} )json";
constexpr char kLegacyAssetId[] = "a2000000-0000-4000-8000-000000000002";
constexpr char kLegacyAssetPayload[] = R"json({
 "model_version":1,"id":"a2000000-0000-4000-8000-000000000002",
 "tombstone":true,"version_model":1,"version_physical":"120",
 "version_logical":2,"version_device":"legacy-device",
 "asset_kind":0,"name":"Legacy CSS","scope":"https://example.test/*",
 "source":"body { color: red; }\n/* \u00e4 */","enabled":true,"opted_in":true
})json";
constexpr char kWatermarkId[] = "a2000000-0000-4000-8000-000000000003";

struct LegacyRow {
  EntityType type;
  const char* id;
  const char* payload;
  const char* mutation_id;
  bool tombstone;
  int64_t physical;
  int logical;
  int64_t queued_at;
};

constexpr std::array<LegacyRow, 2> kLegacyRows = {{
    {EntityType::kAppearance, kLegacyAppearanceId, kLegacyAppearancePayload,
     "legacy-appearance", false, 100, 0, 501},
    {EntityType::kDeveloperAsset, kLegacyAssetId, kLegacyAssetPayload,
     "legacy-asset-delete", true, 120, 2, 502}}};

bool CreateV4Fixture(const base::FilePath& path) {
  sql::Database database(sql::test::kTestTag);
  sql::MetaTable meta;
  if (!database.Open(path) || !meta.Init(&database, 4, 4)) {
    return false;
  }
  // This is the old schema, including its restrictive entity CHECK. Never
  // derive it from the current schema or initialize it with SyncStore.
  if (!database.Execute(R"sql(
    CREATE TABLE sync_records(
      entity_type INTEGER NOT NULL CHECK(entity_type BETWEEN 0 AND 10),
      entity_id TEXT NOT NULL,payload TEXT NOT NULL,
      tombstone INTEGER NOT NULL CHECK(tombstone IN (0,1)),
      model_version INTEGER NOT NULL,version_physical INTEGER NOT NULL,
      version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,
      PRIMARY KEY(entity_type,entity_id));
    CREATE INDEX sync_records_type_order ON
      sync_records(entity_type,tombstone,version_physical,entity_id);
    CREATE TABLE sync_tombstones(
      entity_type INTEGER NOT NULL,entity_id TEXT NOT NULL,
      version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,
      version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,
      deleted_at INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id));
    CREATE TABLE sync_deletion_watermarks(
      entity_type INTEGER NOT NULL,entity_id TEXT NOT NULL,
      version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,
      version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,
      compacted_at INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id));
    CREATE TABLE sync_outbox(
      mutation_id TEXT PRIMARY KEY NOT NULL,entity_type INTEGER NOT NULL,
      entity_id TEXT NOT NULL,change_kind INTEGER NOT NULL,payload TEXT NOT NULL,
      version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,
      version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,
      created_at INTEGER NOT NULL);
    CREATE INDEX sync_outbox_order ON sync_outbox(created_at,mutation_id);
    CREATE TABLE sync_inbox(
      mutation_id TEXT PRIMARY KEY NOT NULL,entity_type INTEGER NOT NULL,
      entity_id TEXT NOT NULL,change_kind INTEGER NOT NULL,payload TEXT NOT NULL,
      version_model INTEGER NOT NULL,version_physical INTEGER NOT NULL,
      version_logical INTEGER NOT NULL,version_device TEXT NOT NULL,
      received_at INTEGER NOT NULL);
    CREATE TABLE sync_quarantine(
      quarantine_id INTEGER PRIMARY KEY AUTOINCREMENT,
      mutation_id TEXT NOT NULL,entity_type INTEGER NOT NULL,
      entity_id TEXT NOT NULL,reason TEXT NOT NULL,payload BLOB NOT NULL,
      received_at INTEGER NOT NULL);
    CREATE INDEX sync_quarantine_received ON
      sync_quarantine(received_at,quarantine_id);
    CREATE TABLE sync_metadata(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL);
    CREATE TABLE sync_retry_state(
      provider_key TEXT PRIMARY KEY NOT NULL,attempt INTEGER NOT NULL,
      last_attempt INTEGER NOT NULL,next_attempt INTEGER NOT NULL,
      last_error TEXT NOT NULL);
    CREATE TABLE sync_command_replay(
      command_id TEXT PRIMARY KEY NOT NULL,source_device_id TEXT NOT NULL,
      nonce TEXT NOT NULL,expires_at INTEGER NOT NULL,consumed_at INTEGER NOT NULL,
      UNIQUE(source_device_id,nonce));
    CREATE INDEX sync_command_replay_expiry ON sync_command_replay(expires_at);
    INSERT INTO sync_metadata VALUES('change_token','v4-token');
    INSERT INTO sync_retry_state VALUES('default',3,111,222,'offline');
    INSERT INTO sync_tombstones VALUES(
      10,'a2000000-0000-4000-8000-000000000002',1,120,2,'legacy-device',777);
    INSERT INTO sync_deletion_watermarks VALUES(
      4,'a2000000-0000-4000-8000-000000000003',2,90,3,'deleted-device',888);
  )sql")) {
    return false;
  }
  for (const auto& row : kLegacyRows) {
    sql::Statement record(database.GetUniqueStatement(
        "INSERT INTO sync_records VALUES(?,?,?,?,?,?,?,?)"));
    record.BindInt(0, static_cast<int>(row.type));
    record.BindString(1, row.id);
    record.BindString(2, row.payload);
    record.BindInt(3, row.tombstone ? 1 : 0);
    record.BindInt(4, 1);
    record.BindInt64(5, row.physical);
    record.BindInt(6, row.logical);
    record.BindString(7, "legacy-device");
    if (!record.Run()) {
      return false;
    }
    sql::Statement outbox(database.GetUniqueStatement(
        "INSERT INTO sync_outbox VALUES(?,?,?,?,?,?,?,?,?,?)"));
    outbox.BindString(0, row.mutation_id);
    outbox.BindInt(1, static_cast<int>(row.type));
    outbox.BindString(2, row.id);
    outbox.BindInt(3, row.tombstone ? 1 : 0);
    outbox.BindString(4, row.payload);
    outbox.BindInt(5, 1);
    outbox.BindInt64(6, row.physical);
    outbox.BindInt(7, row.logical);
    outbox.BindString(8, "legacy-device");
    outbox.BindInt64(9, row.queued_at);
    if (!outbox.Run()) {
      return false;
    }
  }
  return true;
}

TEST(BookmarkSyncStoreTest, RecordAndOutboxSurviveRestartWithoutDuplication) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("sync.sqlite");
  const BookmarkRecord original = Page();
  SyncRecord expected = original;
  ASSERT_TRUE(NormalizeFieldVersions(&expected));
  std::string queued_payload;
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    ASSERT_EQ(Result::kOk, store.PutLocalRecord(original, "local-bookmark"));
    EXPECT_EQ(Result::kAlreadyApplied,
              store.PutLocalRecord(original, "local-bookmark"));
    std::vector<SyncChange> outbox;
    ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &outbox));
    ASSERT_EQ(1u, outbox.size());
    EXPECT_EQ(EntityType::kBookmark, outbox[0].entity_type);
    EXPECT_EQ(original.id, outbox[0].entity_id);
    EXPECT_EQ(ChangeKind::kUpsert, outbox[0].kind);
    queued_payload = outbox[0].payload;
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    SyncRecord actual;
    ASSERT_EQ(Result::kOk,
              store.GetRecord(EntityType::kBookmark, original.id, &actual));
    EXPECT_EQ(expected, actual);
    EXPECT_TRUE(HasCompleteFieldVersions(actual));
    std::vector<SyncChange> outbox;
    ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &outbox));
    ASSERT_EQ(1u, outbox.size());
    EXPECT_EQ(queued_payload, outbox[0].payload);
    EXPECT_EQ("local-bookmark", outbox[0].mutation_id);
    EXPECT_EQ(Result::kAlreadyApplied,
              store.PutLocalRecord(original, "local-bookmark"));
    ASSERT_EQ(Result::kOk, store.AcknowledgeOutbox({"local-bookmark"}));
    EXPECT_EQ(0, store.PendingOutboxCount());
  }
}

TEST(BookmarkSyncStoreTest, ChildBeforeParentAndRepeatedProviderPageAreSafe) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  BookmarkRecord child = Page();
  child.root_kind.reset();
  child.parent_id = Id(2);
  BookmarkRecord parent = Page(2);
  parent.kind = BookmarkKind::kFolder;
  parent.url.clear();
  const SyncChange child_change = ChangeFor(child, "remote-child");
  ASSERT_EQ(Result::kOk,
            store.ApplyRemoteBatch({.changes = {child_change},
                                    .next_change_token = "child-page"}));
  SyncRecord actual;
  ASSERT_EQ(Result::kOk,
            store.GetRecord(EntityType::kBookmark, child.id, &actual));
  EXPECT_EQ(child.parent_id, std::get<BookmarkRecord>(actual).parent_id);
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kBookmark, parent.id, &actual));
  ASSERT_EQ(Result::kOk,
            store.ApplyRemoteBatch({.changes = {child_change},
                                    .next_change_token = "replayed-page"}));
  EXPECT_EQ(1, store.InboxCount());
  EXPECT_EQ(0, store.PendingOutboxCount());
  ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                             {.changes = {ChangeFor(parent, "remote-parent")},
                              .next_change_token = "parent-page"}));
  std::vector<SyncRecord> records;
  ASSERT_EQ(Result::kOk, store.GetRecords(EntityType::kBookmark, &records));
  ASSERT_EQ(2u, records.size());
  EXPECT_TRUE(ValidateBookmarkGraph({std::get<BookmarkRecord>(records[0]),
                                     std::get<BookmarkRecord>(records[1])}));
  EXPECT_EQ(2, store.InboxCount());
  EXPECT_EQ(0, store.QuarantineCount());
  EXPECT_EQ("parent-page", store.GetChangeToken());
}

TEST(BookmarkSyncStoreTest,
     RejectsLocalKindChangeWithoutTouchingRecordOrOutbox) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const BookmarkRecord original = Page();
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(original, "original"));
  ASSERT_EQ(Result::kOk, store.AcknowledgeOutbox({"original"}));
  SyncRecord before;
  ASSERT_EQ(Result::kOk,
            store.GetRecord(EntityType::kBookmark, original.id, &before));
  BookmarkRecord invalid = original;
  invalid.kind = BookmarkKind::kFolder;
  invalid.url.clear();
  invalid.version = Version("local-device", 200);
  EXPECT_NE(Result::kOk, store.PutLocalRecord(invalid, "changed-kind"));
  SyncRecord after;
  ASSERT_EQ(Result::kOk,
            store.GetRecord(EntityType::kBookmark, original.id, &after));
  EXPECT_EQ(before, after);
  EXPECT_EQ(0, store.PendingOutboxCount());
}

TEST(BookmarkSyncStoreTest,
     DeletedBookmarkAndWatermarkRejectStaleReplayOnRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("sync.sqlite");
  const BookmarkRecord original = Page();
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    ASSERT_EQ(Result::kOk, store.PutLocalRecord(original, "create"));
    ASSERT_EQ(Result::kOk, store.AcknowledgeOutbox({"create"}));
    BookmarkRecord removed = original;
    removed.tombstone = true;
    removed.version = Version("local-device", 300);
    ASSERT_EQ(Result::kOk, store.PutLocalRecord(removed, "delete"));
    ASSERT_EQ(Result::kOk,
              store.CompactExpiredTombstones(base::Time::Now() + base::Days(31),
                                             base::Days(30)));
    SyncRecord actual;
    ASSERT_EQ(Result::kOk,
              store.GetRecord(EntityType::kBookmark, original.id, &actual));
    EXPECT_TRUE(IsTombstone(actual));
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    std::vector<SyncChange> outbox;
    ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &outbox));
    ASSERT_EQ(1u, outbox.size());
    EXPECT_EQ(ChangeKind::kDelete, outbox[0].kind);
    EXPECT_EQ(EntityType::kBookmark, outbox[0].entity_type);
    ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                               {.changes = {ChangeFor(original, "stale-live")},
                                .next_change_token = "after-stale"}));
    SyncRecord actual;
    ASSERT_EQ(Result::kOk,
              store.GetRecord(EntityType::kBookmark, original.id, &actual));
    EXPECT_TRUE(IsTombstone(actual));
    EXPECT_EQ(0, store.QuarantineCount());
    EXPECT_EQ(1, store.PendingOutboxCount());
    ASSERT_EQ(Result::kOk, store.AcknowledgeOutbox({"delete"}));
    ASSERT_EQ(Result::kOk,
              store.CompactExpiredTombstones(base::Time::Now() + base::Days(31),
                                             base::Days(30)));
    EXPECT_EQ(Result::kNotFound,
              store.GetRecord(EntityType::kBookmark, original.id, &actual));
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    ASSERT_EQ(Result::kOk,
              store.ApplyRemoteBatch(
                  {.changes = {ChangeFor(original, "stale-after-compaction")},
                   .next_change_token = "after-compaction"}));
    SyncRecord actual;
    EXPECT_EQ(Result::kNotFound,
              store.GetRecord(EntityType::kBookmark, original.id, &actual));
    EXPECT_EQ(1, store.QuarantineCount());
    EXPECT_EQ(0, store.PendingOutboxCount());
    EXPECT_EQ("after-compaction", store.GetChangeToken());
    BookmarkRecord resurrection = original;
    resurrection.version = Version("local-device", 400);
    EXPECT_EQ(Result::kConflict,
              store.PutLocalRecord(resurrection, "resurrection"));
  }
}

TEST(BookmarkSyncStoreTest, MigratesRealV4SchemaWithoutRewritingLegacyData) {
  static_assert(SyncStore::kCurrentSchemaVersion == kCurrentSchemaVersion);
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("v4.sqlite");
  ASSERT_TRUE(CreateV4Fixture(path));
  constexpr char kRecordsQuery[] =
      "SELECT * FROM sync_records WHERE entity_type<>11 ORDER BY "
      "entity_type,entity_id";
  constexpr char kOutboxQuery[] =
      "SELECT * FROM sync_outbox WHERE entity_type<>11 ORDER BY mutation_id";
  constexpr char kTombstonesQuery[] =
      "SELECT * FROM sync_tombstones ORDER BY entity_type,entity_id";
  constexpr char kWatermarksQuery[] =
      "SELECT * FROM sync_deletion_watermarks ORDER BY entity_type,entity_id";
  Rows records_before;
  Rows outbox_before;
  Rows tombstones_before;
  Rows watermarks_before;
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    records_before = ReadRows(database, kRecordsQuery);
    outbox_before = ReadRows(database, kOutboxQuery);
    tombstones_before = ReadRows(database, kTombstonesQuery);
    watermarks_before = ReadRows(database, kWatermarksQuery);
    ASSERT_EQ(2u, records_before.size());
    ASSERT_EQ(2u, outbox_before.size());
    ASSERT_EQ(1u, tombstones_before.size());
    ASSERT_EQ(1u, watermarks_before.size());
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    EXPECT_EQ("v4-token", store.GetChangeToken());
    EXPECT_EQ(3, store.GetRetryState().attempt);
    EXPECT_EQ("offline", store.GetRetryState().last_error);
    EXPECT_EQ(0, store.QuarantineCount());
    for (const auto& row : kLegacyRows) {
      SyncRecord actual;
      ASSERT_EQ(Result::kOk,
                store.GetRecord(row.type, base::Uuid::ParseLowercase(row.id),
                                &actual));
      EXPECT_EQ(row.type, GetEntityType(actual));
      EXPECT_EQ(1, GetVersion(actual).model_version);
      EXPECT_EQ(row.tombstone, IsTombstone(actual));
    }
    std::vector<SyncChange> outbox;
    ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &outbox));
    ASSERT_EQ(2u, outbox.size());
    for (size_t i = 0; i < outbox.size(); ++i) {
      EXPECT_EQ(kLegacyRows[i].mutation_id, outbox[i].mutation_id);
      EXPECT_EQ(kLegacyRows[i].payload, outbox[i].payload);
    }
    // A real entity 11 write exercises the migrated CHECK constraint.
    ASSERT_EQ(Result::kOk, store.PutLocalRecord(Page(), "new-bookmark"));
  }
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&database, 5, 5));
    EXPECT_EQ(5, meta.GetVersionNumber());
    EXPECT_EQ(records_before, ReadRows(database, kRecordsQuery));
    EXPECT_EQ(outbox_before, ReadRows(database, kOutboxQuery));
    EXPECT_EQ(tombstones_before, ReadRows(database, kTombstonesQuery));
    EXPECT_EQ(watermarks_before, ReadRows(database, kWatermarksQuery));
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    SyncRecord actual;
    EXPECT_EQ(Result::kOk,
              store.GetRecord(EntityType::kBookmark, Page().id, &actual));
    // The migrated watermark must still block an old non-bookmark replay.
    const RemoteTabRecord stale{.id = base::Uuid::ParseLowercase(kWatermarkId),
                                .device_id = Id(20),
                                .session_id = Id(21),
                                .url = "https://example.test/old-tab",
                                .title = "Old tab",
                                .opened_at = At(10),
                                .last_active = At(50),
                                .version = Version("old-device", 50)};
    ASSERT_EQ(Result::kOk,
              store.ApplyRemoteBatch({.changes = {ChangeFor(stale, "old-tab")},
                                      .next_change_token = "after-watermark"}));
    EXPECT_EQ(Result::kNotFound,
              store.GetRecord(EntityType::kRemoteTab, stale.id, &actual));
    EXPECT_EQ(1, store.QuarantineCount());
  }
}

}  // namespace
}  // namespace ahoi::sync
