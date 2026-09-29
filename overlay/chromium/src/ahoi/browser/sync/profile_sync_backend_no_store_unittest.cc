// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Regression for the 29 Sep 2026 crash (build 52, real Arc import):
// ProfileSyncBackend::AddHistoryVisit -> Put -> SyncStore::PutLocalRecord on
// a null store_. History, tab and settings observers keep reporting while
// sync is off, after a failed Initialize() or before the store is open; every
// entry point must then refuse without touching the database.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

constexpr char kDevice[] = "a8000000-0000-4000-8000-000000000001";
constexpr char kSession[] = "a8000000-0000-4000-8000-000000000002";
constexpr char kRecord[] = "a8000000-0000-4000-8000-000000000003";
constexpr char kUrl[] = "https://example.com/visited";

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

class ProfileSyncBackendNoStoreTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  base::FilePath DatabasePath() const {
    return directory_.GetPath()
        .AppendASCII("Ahoi Sync")
        .AppendASCII("sync-format3.sqlite");
  }

  std::unique_ptr<ProfileSyncBackend> MakeBackend(bool profile_active) {
    // Transport stays off, so no provider or key setup ever starts.
    return std::make_unique<ProfileSyncBackend>(
        DatabasePath(), Id(kDevice), Id(kSession), "No store test",
        /*transport_enabled=*/false, /*history_retention_days=*/90,
        /*bookmark_sync_enabled=*/false,
        /*profile_authorization=*/
        base::BindRepeating([](bool active) { return active; },
                            profile_active));
  }

  // Every observer/service entry point the profile service can reach while
  // the backend holds no store: none may crash, all must refuse.
  void ExpectEntryPointsRefuse(ProfileSyncBackend& backend) {
    const base::Time now = base::Time::Now();
    EXPECT_FALSE(backend.AddHistoryVisit(kUrl, "Title", now, "link", 7));
    EXPECT_FALSE(backend.AddHistoryVisit(kUrl, "Title", now, "typed", 0));
    EXPECT_FALSE(backend.TombstoneHistory({kUrl}, base::Time(), now, false));
    EXPECT_FALSE(backend.TombstoneHistory({}, base::Time(), now, true));
    EXPECT_FALSE(backend.UpsertAppearance(
        AppearanceRecord{.id = Id(kRecord), .color_mode = "dark"}));
    EXPECT_FALSE(
        backend.UpsertDeveloperAsset(DeveloperAssetRecord{.id = Id(kRecord)}));
    EXPECT_FALSE(
        backend.ReplaceLocalExtensionInventory({ExtensionInventoryRecord{
            .id = Id(kRecord), .device_id = Id(kDevice)}}));
    EXPECT_FALSE(backend.SetHistoryRetentionDays(30));
    EXPECT_FALSE(backend.ApplyRemote(ProviderBatch{}));
    EXPECT_FALSE(backend.Refresh());
    EXPECT_FALSE(backend.SetSharedTabNativeSupport(SharedTabNativeSupport{}));
    EXPECT_FALSE(backend.ReplaceLocalTabs({}));
    EXPECT_TRUE(
        backend.ClaimRemoteCommands(RemoteCommandPolicy{}, now).empty());
    EXPECT_FALSE(backend.CompleteRemoteCommand(Id(kRecord), true, "ok"));
    EXPECT_FALSE(backend.ConfirmAccountTransition(true));
    EXPECT_FALSE(backend.ConfirmZoneRecovery());
    EXPECT_FALSE(backend.RetrySyncKeySetup());
    EXPECT_FALSE(backend.SetTransportEnabled(true));
    EXPECT_FALSE(backend.SetTransportEnabled(false));
    EXPECT_FALSE(backend.ReadBrowserSettings());
    EXPECT_FALSE(backend.ReadWorkspaceStructure());
    EXPECT_FALSE(backend.ReadBookmarkProjection());
    EXPECT_FALSE(backend.ReadSharedTabProjection());
    bool called = false;
    backend.SyncNow(base::BindLambdaForTesting(
        [&called](std::optional<SyncStateSnapshot> state) {
          called = true;
          EXPECT_FALSE(state);
        }));
    EXPECT_TRUE(called);
  }

  // Reopens the database after the backend released it.
  std::vector<SyncRecord> StoredHistory() {
    SyncStore store;
    EXPECT_TRUE(store.Initialize(DatabasePath()));
    std::vector<SyncRecord> records;
    EXPECT_EQ(store.GetRecords(EntityType::kHistoryEntry, &records),
              SyncStore::Result::kOk);
    return records;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir directory_;
};

}  // namespace

TEST_F(ProfileSyncBackendNoStoreTest, NeverInitializedWritesNothing) {
  auto backend = MakeBackend(/*profile_active=*/true);
  ExpectEntryPointsRefuse(*backend);
  backend.reset();
  EXPECT_FALSE(base::PathExists(DatabasePath()));
}

TEST_F(ProfileSyncBackendNoStoreTest, FailedInitializeWritesNothing) {
  // A revoked profile lease makes Initialize() fail before opening a store;
  // the service keeps the backend and its observers keep reporting.
  auto backend = MakeBackend(/*profile_active=*/false);
  EXPECT_FALSE(backend->Initialize());
  ExpectEntryPointsRefuse(*backend);
  backend.reset();
  EXPECT_FALSE(base::PathExists(DatabasePath()));
}

TEST_F(ProfileSyncBackendNoStoreTest, SuspendedSyncWritesNoHistory) {
  auto backend = MakeBackend(/*profile_active=*/true);
  ASSERT_TRUE(backend->Initialize());
  // Sync switched off: the store is released but the backend lives on until
  // its SequenceBound owner is reset.
  backend->SuspendWithoutPersisting();
  ExpectEntryPointsRefuse(*backend);
  backend.reset();
  EXPECT_TRUE(StoredHistory().empty());
}

TEST_F(ProfileSyncBackendNoStoreTest, OpenStoreStillRecordsHistoryVisit) {
  auto backend = MakeBackend(/*profile_active=*/true);
  ASSERT_TRUE(backend->Initialize());
  const auto state =
      backend->AddHistoryVisit(kUrl, "Title", base::Time::Now(), "link", 7);
  ASSERT_TRUE(state);
  ASSERT_EQ(state->history.size(), 1u);
  EXPECT_EQ(state->history.front().url, kUrl);
  EXPECT_EQ(state->history.front().device_id, Id(kDevice));
  backend.reset();
  const auto stored = StoredHistory();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_EQ(std::get<HistoryRecord>(stored.front()).url, kUrl);
}

}  // namespace ahoi::sync
