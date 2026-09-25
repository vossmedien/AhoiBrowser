// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include "ahoi/browser/sync/cloudkit_sync_quarantine.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

base::Uuid ParseId(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

// Format 3 clocks and ordinary timestamps are at or after the Unix epoch and
// clocks carry canonical device UUIDs. Fixtures use offsets from that minimum.
constexpr char kDeviceA[] = "60000000-0000-4000-8000-00000000d00a";
constexpr char kDeviceB[] = "60000000-0000-4000-8000-00000000d00b";

base::Time TimeAt(int64_t offset) {
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(kMinimumSyncClockPhysicalUs + offset));
}

SyncVersion MakeVersion(const char* device, int64_t offset) {
  return {.stamp = {.physical_time_us = kMinimumSyncClockPhysicalUs + offset,
                    .device_tiebreak = device}};
}

WorkspaceRecord MakeWorkspace() {
  return {.id = ParseId("60000000-0000-4000-8000-000000000001"),
          .name = "Initial",
          .icon = "compass",
          .sort_key = "a",
          .created_at = TimeAt(1),
          .modified_at = TimeAt(100),
          .version = MakeVersion(kDeviceA, 100)};
}

SyncChange ChangeFor(const WorkspaceRecord& record, const char* mutation) {
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  return {.mutation_id = mutation,
          .entity_type = EntityType::kWorkspace,
          .entity_id = record.id,
          .kind = record.tombstone ? ChangeKind::kDelete : ChangeKind::kUpsert,
          .version = record.version,
          .payload = std::move(payload)};
}

TEST(SyncWireV3Test, CarriesCompleteCanonicalFieldClocks) {
  const WorkspaceRecord workspace = MakeWorkspace();
  std::string payload;
  ASSERT_TRUE(SerializeRecord(workspace, &payload));
  EXPECT_NE(payload.find("\"model_version\":3"), std::string::npos);
  EXPECT_NE(payload.find("\"field_versions\":{"), std::string::npos);
  EXPECT_NE(payload.find("\"accent_argb\":{"), std::string::npos);
  EXPECT_NE(payload.find("\"archive_policy\":{"), std::string::npos);
  EXPECT_NE(payload.find("\"tombstone\":{"), std::string::npos);

  SyncRecord decoded;
  ASSERT_TRUE(DeserializeRecord(EntityType::kWorkspace, payload, &decoded));
  EXPECT_TRUE(HasCompleteFieldVersions(decoded));
  const WorkspaceRecord& round_trip = std::get<WorkspaceRecord>(decoded);
  EXPECT_EQ(round_trip.field_versions.size(), 8u);
  EXPECT_EQ(round_trip.field_versions.at("name"), workspace.version.stamp);
}

std::string SharedGoldenPayload(const char* name) {
  base::FilePath root;
  EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string bytes;
  EXPECT_TRUE(base::ReadFileToString(
      root.AppendASCII("ahoi/browser/sync/testdata/sync_wire_v3.json"),
      &bytes));
  const auto document = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  if (!document || !document->FindList("records")) {
    ADD_FAILURE() << "unreadable sync_wire_v3.json";
    return {};
  }
  for (const auto& item : *document->FindList("records")) {
    const std::string* found = item.GetDict().FindString("name");
    const std::string* payload = item.GetDict().FindString("payload");
    if (found && payload && *found == name) {
      return *payload;
    }
  }
  ADD_FAILURE() << "missing golden " << name;
  return {};
}

TEST(SyncWireV3Test, MatchesCompanionRemoteTabGoldenBytes) {
  const std::string golden = SharedGoldenPayload("presence_saved_web");
  ASSERT_FALSE(golden.empty());
  SyncRecord decoded;
  ASSERT_TRUE(DeserializeRecord(EntityType::kRemoteTab, golden, &decoded));
  const auto& tab = std::get<RemoteTabRecord>(decoded);
  EXPECT_EQ(3, tab.model_version);
  EXPECT_TRUE(tab.tree_node_id.has_value());
  std::string payload;
  ASSERT_TRUE(SerializeRecord(decoded, &payload));
  EXPECT_EQ(golden, payload);
}

// Format 2 is removed (ADR 0009): the same companion bytes relabelled as
// model 2 fail closed instead of being upgraded.
TEST(SyncWireV3Test, RejectsCompanionRemoteTabBytesFromRemovedFormat) {
  std::string legacy = SharedGoldenPayload("presence_saved_web");
  ASSERT_NE(legacy.find("\"model_version\":3"), std::string::npos);
  ASSERT_NE(legacy.find("\"version_model\":3"), std::string::npos);
  base::ReplaceSubstringsAfterOffset(&legacy, 0, "\"model_version\":3",
                                     "\"model_version\":2");
  base::ReplaceSubstringsAfterOffset(&legacy, 0, "\"version_model\":3",
                                     "\"version_model\":2");
  SyncRecord decoded;
  EXPECT_FALSE(DeserializeRecord(EntityType::kRemoteTab, legacy, &decoded));
}

TEST(SyncStoreV3Test, MergesDisjointFieldsAndRequeuesConvergedUnion) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  WorkspaceRecord initial = MakeWorkspace();
  ASSERT_EQ(store.PutLocalRecord(initial, "initial"), SyncStore::Result::kOk);
  ASSERT_EQ(store.AcknowledgeOutbox({"initial"}), SyncStore::Result::kOk);

  SyncRecord value;
  ASSERT_EQ(store.GetRecord(EntityType::kWorkspace, initial.id, &value),
            SyncStore::Result::kOk);
  WorkspaceRecord local = std::get<WorkspaceRecord>(value);
  local.sort_key = "z";
  local.version = MakeVersion(kDeviceA, 120);
  ASSERT_EQ(store.PutLocalRecord(local, "local-order"), SyncStore::Result::kOk);
  ASSERT_EQ(store.AcknowledgeOutbox({"local-order"}), SyncStore::Result::kOk);

  WorkspaceRecord remote = std::get<WorkspaceRecord>(value);
  remote.name = "Remote name";
  remote.version = MakeVersion(kDeviceB, 110);
  remote.field_versions.insert_or_assign("name", remote.version.stamp);
  ASSERT_EQ(
      store.ApplyRemoteBatch({.changes = {ChangeFor(remote, "remote-name")},
                              .next_change_token = "v2-page",
                              .has_more = false}),
      SyncStore::Result::kOk);

  ASSERT_EQ(store.GetRecord(EntityType::kWorkspace, initial.id, &value),
            SyncStore::Result::kOk);
  const WorkspaceRecord& merged = std::get<WorkspaceRecord>(value);
  EXPECT_EQ(merged.name, "Remote name");
  EXPECT_EQ(merged.sort_key, "z");
  EXPECT_EQ(merged.field_versions.at("name"), remote.version.stamp);
  EXPECT_EQ(merged.field_versions.at("sort_key"), local.version.stamp);
  EXPECT_GT(merged.version.stamp, local.version.stamp);
  EXPECT_GT(merged.version.stamp, remote.version.stamp);
  EXPECT_EQ(store.PendingOutboxCount(), 1);
  EXPECT_EQ(store.GetChangeToken(), "v2-page");
}

TEST(SyncStoreV3Test, QuarantinesOneConflictAndAdvancesProviderToken) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  WorkspaceRecord initial = MakeWorkspace();
  ASSERT_EQ(store.PutLocalRecord(initial, "initial"), SyncStore::Result::kOk);
  ASSERT_EQ(store.AcknowledgeOutbox({"initial"}), SyncStore::Result::kOk);

  SyncRecord value;
  ASSERT_EQ(store.GetRecord(EntityType::kWorkspace, initial.id, &value),
            SyncStore::Result::kOk);
  WorkspaceRecord conflicting = std::get<WorkspaceRecord>(value);
  conflicting.name = "same-clock divergence";
  ASSERT_EQ(
      store.ApplyRemoteBatch({.changes = {ChangeFor(conflicting, "bad-field")},
                              .next_change_token = "after-bad",
                              .has_more = false}),
      SyncStore::Result::kOk);
  EXPECT_EQ(store.QuarantineCount(), 1);
  EXPECT_EQ(store.GetChangeToken(), "after-bad");

  ASSERT_EQ(store.GetRecord(EntityType::kWorkspace, initial.id, &value),
            SyncStore::Result::kOk);
  EXPECT_EQ(std::get<WorkspaceRecord>(value).name, initial.name);
}

TEST(SyncStoreV3Test, QuarantinesMutationIdCollisionWithoutPinningToken) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  WorkspaceRecord first = MakeWorkspace();
  ASSERT_EQ(store.ApplyRemoteBatch(
                {.changes = {ChangeFor(first, "provider-mutation")},
                 .next_change_token = "first-token"}),
            SyncStore::Result::kOk);
  WorkspaceRecord collision = first;
  collision.name = "Different payload";
  collision.version = MakeVersion(kDeviceB, 200);
  ASSERT_EQ(store.ApplyRemoteBatch(
                {.changes = {ChangeFor(collision, "provider-mutation")},
                 .next_change_token = "second-token"}),
            SyncStore::Result::kOk);
  EXPECT_EQ(store.QuarantineCount(), 1);
  EXPECT_EQ(store.GetChangeToken(), "second-token");
  SyncRecord value;
  ASSERT_EQ(store.GetRecord(EntityType::kWorkspace, first.id, &value),
            SyncStore::Result::kOk);
  EXPECT_EQ(std::get<WorkspaceRecord>(value).name, first.name);
}

TEST(SyncStoreV3Test, QuarantinesMalformedCloudKitEnvelopeWithoutPayload) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const SyncChange marker = MakeCloudKitQuarantineMarker(EntityType::kTreeNode);
  EXPECT_TRUE(IsCloudKitQuarantineMarker(marker));
  EXPECT_EQ(marker.payload, "{}");
  ASSERT_EQ(store.ApplyRemoteBatch(
                {.changes = {marker}, .next_change_token = "after-malformed"}),
            SyncStore::Result::kOk);
  EXPECT_EQ(store.QuarantineCount(), 1);
  EXPECT_EQ(store.GetChangeToken(), "after-malformed");
}

TEST(SyncStoreV3Test, CompactionWatermarkRejectsDelayedResurrection) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  WorkspaceRecord deleted = MakeWorkspace();
  deleted.tombstone = true;
  deleted.version = MakeVersion(kDeviceA, 200);
  ASSERT_EQ(store.PutLocalRecord(deleted, "delete"), SyncStore::Result::kOk);
  ASSERT_EQ(store.AcknowledgeOutbox({"delete"}), SyncStore::Result::kOk);
  ASSERT_EQ(store.CompactExpiredTombstones(base::Time::Now() + base::Days(31),
                                           base::Days(30)),
            SyncStore::Result::kOk);

  SyncRecord value;
  EXPECT_EQ(store.GetRecord(EntityType::kWorkspace, deleted.id, &value),
            SyncStore::Result::kNotFound);

  // A delayed peer write carries the complete field-clock map of the deleted
  // record with only its tombstone register advanced.
  SyncRecord complete_deleted = deleted;
  ASSERT_TRUE(NormalizeFieldVersions(&complete_deleted));
  WorkspaceRecord resurrected = std::get<WorkspaceRecord>(complete_deleted);
  resurrected.tombstone = false;
  resurrected.version = MakeVersion(kDeviceB, 300);
  resurrected.field_versions.insert_or_assign("tombstone",
                                              resurrected.version.stamp);
  ASSERT_EQ(store.ApplyRemoteBatch(
                {.changes = {ChangeFor(resurrected, "resurrection")},
                 .next_change_token = "after-resurrection",
                 .has_more = false}),
            SyncStore::Result::kOk);
  EXPECT_EQ(store.QuarantineCount(), 1);
  EXPECT_EQ(store.GetChangeToken(), "after-resurrection");
  EXPECT_EQ(store.GetRecord(EntityType::kWorkspace, deleted.id, &value),
            SyncStore::Result::kNotFound);
}

// Pre-launch format reset (ADR 0009): an old v2 store is refused, never
// migrated, and its bytes stay untouched.
TEST(SyncStoreV3Test, RefusesV2DatabaseWithoutMigration) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("sync.sqlite");
  constexpr char kLegacyId[] = "80000000-0000-4000-8000-000000000001";
  constexpr char kLegacyPayload[] =
      R"json({"model_version":1,"id":"80000000-0000-4000-8000-000000000001",)json"
      R"json("version_model":1,"version_physical":"100","version_logical":0,)json"
      R"json("version_device":"legacy-device","url":"https://example.test/"})json";
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&database, 2, 2));
    ASSERT_TRUE(database.Execute(
        "CREATE TABLE sync_records("
        "entity_type INTEGER NOT NULL,entity_id TEXT NOT NULL,payload TEXT "
        "NOT NULL,tombstone INTEGER NOT NULL,model_version INTEGER NOT NULL,"
        "version_physical INTEGER NOT NULL,version_logical INTEGER NOT NULL,"
        "version_device TEXT NOT NULL,PRIMARY KEY(entity_type,entity_id))"));
    sql::Statement insert(database.GetUniqueStatement(
        "INSERT INTO sync_records VALUES(?,?,?,?,?,?,?,?)"));
    insert.BindInt(0, static_cast<int>(EntityType::kRemoteTab));
    insert.BindString(1, kLegacyId);
    insert.BindString(2, kLegacyPayload);
    insert.BindInt(3, 0);
    insert.BindInt(4, 1);
    insert.BindInt64(5, 100);
    insert.BindInt(6, 0);
    insert.BindString(7, "legacy-device");
    ASSERT_TRUE(insert.Run());
  }

  {
    SyncStore store;
    EXPECT_FALSE(store.Initialize(path));
  }

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::MetaTable meta;
  ASSERT_TRUE(meta.Init(&database, 2, 2));
  EXPECT_EQ(2, meta.GetVersionNumber());
  EXPECT_FALSE(database.DoesTableExist("sync_outbox"));
  sql::Statement query(database.GetUniqueStatement(
      "SELECT entity_id,payload,model_version FROM sync_records"));
  ASSERT_TRUE(query.Step());
  EXPECT_EQ(kLegacyId, query.ColumnString(0));
  EXPECT_EQ(kLegacyPayload, query.ColumnString(1));
  EXPECT_EQ(1, query.ColumnInt(2));
  EXPECT_FALSE(query.Step());
}

}  // namespace
}  // namespace ahoi::sync
