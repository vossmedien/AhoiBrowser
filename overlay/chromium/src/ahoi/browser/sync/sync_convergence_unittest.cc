// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include "ahoi/browser/sync/cloudkit_sync_quarantine.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/scoped_temp_dir.h"
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

base::Time TimeAt(int64_t micros) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
}

SyncVersion MakeVersion(const char* device, int64_t physical) {
  return {.stamp = {.physical_time_us = physical, .device_tiebreak = device}};
}

WorkspaceRecord MakeWorkspace() {
  return {.id = ParseId("60000000-0000-4000-8000-000000000001"),
          .name = "Initial",
          .icon = "compass",
          .sort_key = "a",
          .created_at = TimeAt(1),
          .modified_at = TimeAt(100),
          .version = MakeVersion("device-a", 100)};
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

TEST(SyncWireV2Test, CarriesCompleteCanonicalFieldClocks) {
  const WorkspaceRecord workspace = MakeWorkspace();
  std::string payload;
  ASSERT_TRUE(SerializeRecord(workspace, &payload));
  EXPECT_NE(payload.find("\"model_version\":2"), std::string::npos);
  EXPECT_NE(payload.find("\"field_versions\":{"), std::string::npos);
  EXPECT_NE(payload.find("\"accent_argb\":{"), std::string::npos);
  EXPECT_NE(payload.find("\"tombstone\":{"), std::string::npos);

  SyncRecord decoded;
  ASSERT_TRUE(DeserializeRecord(EntityType::kWorkspace, payload, &decoded));
  EXPECT_TRUE(HasCompleteFieldVersions(decoded));
  const WorkspaceRecord& round_trip = std::get<WorkspaceRecord>(decoded);
  EXPECT_EQ(round_trip.field_versions.size(), 7u);
  EXPECT_EQ(round_trip.field_versions.at("name"), workspace.version.stamp);
}

TEST(SyncWireV2Test, MatchesCompanionRemoteTabGoldenBytes) {
  constexpr int64_t kPhysical = 11644473601000000LL;
  const RemoteTabRecord tab{
      .id = ParseId("10000000-0000-4000-8000-000000000001"),
      .device_id = ParseId("10000000-0000-4000-8000-000000000002"),
      .session_id = ParseId("10000000-0000-4000-8000-000000000003"),
      .url = "https://example.test/path",
      .title = "Ahoi",
      .opened_at = TimeAt(kPhysical),
      .last_active = TimeAt(kPhysical),
      .version = {.stamp = {.physical_time_us = kPhysical,
                            .logical = 2,
                            .device_tiebreak =
                                "10000000-0000-4000-8000-000000000002"}}};
  std::string payload;
  ASSERT_TRUE(SerializeRecord(tab, &payload));
  EXPECT_EQ(
      payload,
      R"json({"device_id":"10000000-0000-4000-8000-000000000002","field_versions":{"device_id":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"is_incognito":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"last_active":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"opened_at":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"pinned":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"session_id":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"title":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"tombstone":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"url":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"},"workspace_id":{"device":"10000000-0000-4000-8000-000000000002","logical":2,"physical":"11644473601000000"}},"id":"10000000-0000-4000-8000-000000000001","is_incognito":false,"last_active":"11644473601000000","model_version":2,"opened_at":"11644473601000000","pinned":false,"session_id":"10000000-0000-4000-8000-000000000003","title":"Ahoi","tombstone":false,"url":"https://example.test/path","version_device":"10000000-0000-4000-8000-000000000002","version_logical":2,"version_model":2,"version_physical":"11644473601000000"})json");
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
  local.version = MakeVersion("device-a", 120);
  ASSERT_EQ(store.PutLocalRecord(local, "local-order"), SyncStore::Result::kOk);
  ASSERT_EQ(store.AcknowledgeOutbox({"local-order"}), SyncStore::Result::kOk);

  WorkspaceRecord remote = std::get<WorkspaceRecord>(value);
  remote.name = "Remote name";
  remote.version = MakeVersion("device-b", 110);
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
  collision.version = MakeVersion("device-b", 200);
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
  deleted.version = MakeVersion("device-a", 200);
  ASSERT_EQ(store.PutLocalRecord(deleted, "delete"), SyncStore::Result::kOk);
  ASSERT_EQ(store.AcknowledgeOutbox({"delete"}), SyncStore::Result::kOk);
  ASSERT_EQ(store.CompactExpiredTombstones(base::Time::Now() + base::Days(31),
                                           base::Days(30)),
            SyncStore::Result::kOk);

  SyncRecord value;
  EXPECT_EQ(store.GetRecord(EntityType::kWorkspace, deleted.id, &value),
            SyncStore::Result::kNotFound);

  WorkspaceRecord resurrected = deleted;
  resurrected.tombstone = false;
  resurrected.version = MakeVersion("device-b", 300);
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

TEST(SyncStoreV3Test, MigratesV2DatabaseAndLazilyUpgradesWireV1Row) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("sync.sqlite");
  const RemoteTabRecord legacy{
      .model_version = 1,
      .id = ParseId("80000000-0000-4000-8000-000000000001"),
      .device_id = ParseId("80000000-0000-4000-8000-000000000002"),
      .session_id = ParseId("80000000-0000-4000-8000-000000000003"),
      .url = "https://example.test/legacy",
      .title = "Legacy",
      .opened_at = TimeAt(100),
      .last_active = TimeAt(100),
      .version = {.model_version = 1,
                  .stamp = {.physical_time_us = 100,
                            .device_tiebreak = "legacy-device"}}};
  std::string legacy_payload;
  ASSERT_TRUE(SerializeRecord(legacy, &legacy_payload));
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
    insert.BindString(1, legacy.id.AsLowercaseString());
    insert.BindString(2, legacy_payload);
    insert.BindInt(3, 0);
    insert.BindInt(4, legacy.version.model_version);
    insert.BindInt64(5, legacy.version.stamp.physical_time_us);
    insert.BindInt(6, static_cast<int>(legacy.version.stamp.logical));
    insert.BindString(7, legacy.version.stamp.device_tiebreak);
    ASSERT_TRUE(insert.Run());
  }

  SyncStore store;
  ASSERT_TRUE(store.Initialize(path));
  EXPECT_EQ(store.QuarantineCount(), 0);
  SyncRecord value;
  ASSERT_EQ(store.GetRecord(EntityType::kRemoteTab, legacy.id, &value),
            SyncStore::Result::kOk);
  RemoteTabRecord upgraded = std::get<RemoteTabRecord>(value);
  EXPECT_EQ(upgraded.model_version, 1);
  upgraded.title = "Upgraded";
  upgraded.model_version = kCurrentModelVersion;
  upgraded.version = MakeVersion("current-device", 200);
  ASSERT_EQ(store.PutLocalRecord(upgraded, "upgrade-v1-row"),
            SyncStore::Result::kOk);
  std::vector<SyncChange> outbox;
  ASSERT_EQ(store.ReadOutbox(10, &outbox), SyncStore::Result::kOk);
  ASSERT_EQ(outbox.size(), 1u);
  EXPECT_NE(outbox[0].payload.find("\"model_version\":2"), std::string::npos);
  EXPECT_NE(outbox[0].payload.find("\"field_versions\":{"), std::string::npos);
}

}  // namespace
}  // namespace ahoi::sync
