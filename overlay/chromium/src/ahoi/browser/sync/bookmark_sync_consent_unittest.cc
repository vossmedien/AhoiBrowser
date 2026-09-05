// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using Result = SyncStore::Result;

base::Uuid Id(unsigned value) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("93000000-0000-4000-8000-%012x", value));
}

BookmarkRecord Bookmark(unsigned id) {
  return {.id = Id(id),
          .root_kind = BookmarkRoot::kBookmarkBar,
          .sort_key = "a",
          .title = "Private bookmark folder",
          .created_at =
              base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(10)),
          .version = {.stamp = {.physical_time_us = 100,
                                .device_tiebreak = "device-a"}}};
}

WorkspaceRecord Workspace(unsigned id) {
  return {.id = Id(id),
          .name = "Allowed workspace",
          .icon = "compass",
          .sort_key = "a",
          .created_at =
              base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(10)),
          .modified_at =
              base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(100)),
          .version = {.stamp = {.physical_time_us = 100,
                                .device_tiebreak = "device-a"}}};
}

SyncChange Remote(const SyncRecord& record, const char* mutation) {
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  return {.mutation_id = mutation,
          .entity_type = GetEntityType(record),
          .entity_id = GetEntityId(record),
          .version = GetVersion(record),
          .payload = std::move(payload)};
}

class ControlledProvider final : public SyncProvider {
 public:
  struct Authority {
    std::atomic<bool> enabled{true};
    std::atomic<uint64_t> generation{0};
  };
  struct UploadRequest {
    std::vector<SyncChange> changes;
    UploadCallback callback;
  };
  struct DownloadRequest {
    std::string token;
    DownloadCallback callback;
  };
  void SetBookmarkSyncEnabled(bool enabled) override {
    bookmarks_enabled = enabled;
    authority->enabled.store(enabled);
    authority->generation.fetch_add(1);
  }
  BookmarkSyncAuthorization GetBookmarkSyncAuthorization() override {
    if (!bookmarks_enabled || revoked) {
      return {};
    }
    return base::BindRepeating(
        [](std::weak_ptr<Authority> weak, uint64_t generation) {
          const auto state = weak.lock();
          return state && state->enabled.load() &&
                 state->generation.load() == generation;
        },
        std::weak_ptr<Authority>(authority), authority->generation.load());
  }
  void RevokeFromProvider() {
    revoked = true;
    SetBookmarkSyncEnabled(false);
  }
  void ReapproveFromProvider() {
    revoked = false;
    SetBookmarkSyncEnabled(true);
  }
  bool IsBookmarkConsentRevoked() override { return revoked; }
  void Upload(std::vector<SyncChange> changes,
              UploadCallback callback) override {
    uploads.push_back({std::move(changes), std::move(callback)});
    if (!hold_uploads) {
      CompleteUpload(uploads.size() - 1);
    }
  }
  void Download(std::string token, DownloadCallback callback) override {
    downloads.push_back({std::move(token), std::move(callback)});
    if (!hold_downloads) {
      const size_t index = downloads.size() - 1;
      CompleteDownload(index, {.next_change_token = downloads[index].token});
    }
  }
  // These callbacks deliberately may arrive after revocation, like an already
  // dispatched provider completion. The pump must reject their old authority.
  void CompleteUpload(size_t index) {
    std::vector<std::string> ids;
    for (const auto& change : uploads[index].changes) {
      ids.push_back(change.mutation_id);
    }
    std::move(uploads[index].callback).Run(true, std::move(ids), {});
  }
  void CompleteDownload(size_t index, ProviderBatch batch) {
    std::move(downloads[index].callback).Run(true, std::move(batch), {});
  }
  bool bookmarks_enabled = true;
  bool revoked = false;
  bool hold_uploads = false;
  bool hold_downloads = false;
  std::vector<UploadRequest> uploads;
  std::vector<DownloadRequest> downloads;
  std::shared_ptr<Authority> authority = std::make_shared<Authority>();
};

TEST(BookmarkSyncConsentTest,
     DefaultOffRetainsBookmarksWithoutStarvingOtherRows) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  for (unsigned i = 1; i <= 5; ++i) {
    ASSERT_EQ(Result::kOk,
              store.PutLocalRecord(Bookmark(i),
                                   base::StringPrintf("bookmark-%u", i)));
  }
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(Workspace(9), "workspace"));
  ControlledProvider provider;
  SyncPump pump(&store, &provider, {.upload_batch_size = 2});
  EXPECT_FALSE(provider.bookmarks_enabled);
  bool completed = false;
  ASSERT_TRUE(pump.SyncNow(
      base::BindLambdaForTesting([&](bool success, std::string error) {
        completed = true;
        EXPECT_TRUE(success);
        EXPECT_TRUE(error.empty());
      })));
  tasks.RunUntilIdle();
  EXPECT_TRUE(completed);
  ASSERT_EQ(1u, provider.uploads.size());
  ASSERT_EQ(1u, provider.uploads[0].changes.size());
  EXPECT_EQ(EntityType::kWorkspace, provider.uploads[0].changes[0].entity_type);
  EXPECT_EQ(5, store.PendingOutboxCount());
  std::vector<SyncChange> all;
  ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &all));
  EXPECT_EQ(5u, all.size());
  ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &all, false));
  EXPECT_TRUE(all.empty());

  pump.SetBookmarkSyncEnabled(true);
  EXPECT_TRUE(provider.bookmarks_enabled);
  ASSERT_TRUE(pump.SyncNow({}));
  tasks.RunUntilIdle();
  EXPECT_EQ(0, store.PendingOutboxCount());
  EXPECT_EQ(4u, provider.uploads.size());
}

TEST(BookmarkSyncConsentTest, RevocationInvalidatesLateUploadAcknowledgements) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(Bookmark(1), "bookmark"));
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(Workspace(2), "workspace"));
  ControlledProvider provider;
  provider.hold_uploads = true;
  SyncPump pump(&store, &provider, {.upload_batch_size = 1});
  pump.SetBookmarkSyncEnabled(true);
  bool cancelled = false;
  ASSERT_TRUE(pump.SyncNow(
      base::BindLambdaForTesting([&](bool success, std::string error) {
        cancelled = !success && error == "cancelled";
      })));
  ASSERT_EQ(1u, provider.uploads.size());
  pump.SetBookmarkSyncEnabled(false);
  provider.CompleteUpload(0);
  tasks.RunUntilIdle();
  EXPECT_TRUE(cancelled);
  EXPECT_EQ(2, store.PendingOutboxCount());
  EXPECT_EQ(0, store.GetRetryState().attempt);

  ASSERT_TRUE(pump.SyncNow({}));
  ASSERT_EQ(2u, provider.uploads.size());
  ASSERT_EQ(EntityType::kWorkspace, provider.uploads[1].changes[0].entity_type);
  provider.CompleteUpload(1);
  tasks.RunUntilIdle();
  EXPECT_EQ(1, store.PendingOutboxCount());
  std::vector<SyncChange> retained;
  ASSERT_EQ(Result::kOk, store.ReadOutbox(10, &retained));
  ASSERT_EQ(1u, retained.size());
  EXPECT_EQ("bookmark", retained[0].mutation_id);
}

TEST(BookmarkSyncConsentTest, ConstructorCannotRestoreRevokedCategoryConsent) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(Bookmark(1), "bookmark"));
  ControlledProvider provider;
  provider.revoked = true;
  SyncPump pump(&store, &provider, {.bookmark_sync_enabled = true});
  EXPECT_FALSE(provider.bookmarks_enabled);
  ASSERT_TRUE(pump.SyncNow({}));
  tasks.RunUntilIdle();
  EXPECT_TRUE(provider.uploads.empty());
  EXPECT_EQ(1, store.PendingOutboxCount());
}

TEST(BookmarkSyncConsentTest,
     LateDownloadCannotHydrateOrAdvanceTokenAfterRevocation) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ControlledProvider provider;
  provider.hold_downloads = true;
  SyncPump pump(&store, &provider);
  pump.SetBookmarkSyncEnabled(true);
  ASSERT_TRUE(pump.SyncNow({}));
  ASSERT_EQ(1u, provider.downloads.size());
  pump.SetBookmarkSyncEnabled(false);
  provider.CompleteDownload(0, {.changes = {Remote(Bookmark(1), "book")},
                                .next_change_token = "old-approved"});
  tasks.RunUntilIdle();
  SyncRecord record;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kBookmark, Id(1), &record));
  EXPECT_TRUE(store.GetChangeToken().empty());
  EXPECT_EQ(0, store.InboxCount());

  ASSERT_TRUE(pump.SyncNow({}));
  provider.CompleteDownload(1, {.changes = {Remote(Workspace(2), "workspace")},
                                .next_change_token = "allowed"});
  tasks.RunUntilIdle();
  EXPECT_EQ(Result::kOk,
            store.GetRecord(EntityType::kWorkspace, Id(2), &record));
  EXPECT_EQ("allowed", store.GetChangeToken());
  pump.SetBookmarkSyncEnabled(true);
  ASSERT_TRUE(pump.SyncNow({}));
  provider.CompleteDownload(2, {.changes = {Remote(Bookmark(1), "book")},
                                .next_change_token = "approved-replay"});
  tasks.RunUntilIdle();
  EXPECT_EQ(Result::kOk,
            store.GetRecord(EntityType::kBookmark, Id(1), &record));
  EXPECT_EQ(2, store.InboxCount());
}

TEST(BookmarkSyncConsentTest,
     ProviderIgnoringDefaultOffCannotImportMixedBatch) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ControlledProvider provider;
  provider.hold_downloads = true;
  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow({}));
  provider.CompleteDownload(0, {.changes = {Remote(Bookmark(1), "book"),
                                            Remote(Workspace(2), "workspace")},
                                .next_change_token = "must-not-commit"});
  tasks.RunUntilIdle();
  EXPECT_TRUE(store.GetChangeToken().empty());
  EXPECT_EQ(0, store.InboxCount());
  SyncRecord record;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kBookmark, Id(1), &record));
  EXPECT_EQ("provider_error", store.GetRetryState().last_error);
}

TEST(BookmarkSyncConsentTest, NewPumpNeverInfersConsentFromRetainedOutbox) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(Bookmark(1), "bookmark"));
  ControlledProvider provider;
  {
    SyncPump pump(&store, &provider);
    pump.SetBookmarkSyncEnabled(true);
  }
  ASSERT_TRUE(provider.bookmarks_enabled);
  SyncPump restarted(&store, &provider);
  EXPECT_FALSE(provider.bookmarks_enabled);
  ASSERT_TRUE(restarted.SyncNow({}));
  tasks.RunUntilIdle();
  EXPECT_TRUE(provider.uploads.empty());
  EXPECT_EQ(1, store.PendingOutboxCount());
}

TEST(BookmarkSyncConsentTest,
     ProviderRevocationAfterPostedUploadCannotAcknowledgeOutbox) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_EQ(Result::kOk, store.PutLocalRecord(Bookmark(1), "pending-bookmark"));
  ControlledProvider provider;
  SyncPump pump(&store, &provider,
                SyncPump::Options{.bookmark_sync_enabled = true});
  bool completed = false;
  ASSERT_TRUE(pump.SyncNow(
      base::BindLambdaForTesting([&](bool success, std::string error) {
        completed = true;
        EXPECT_FALSE(success);
        EXPECT_EQ("cancelled", error);
      })));
  ASSERT_EQ(1u, provider.uploads.size());
  // The provider already returned success; BindPostTask has queued the local
  // acknowledgement. Do not update the pump's deliberately stale cached flag.
  provider.RevokeFromProvider();
  tasks.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_EQ(1, store.PendingOutboxCount());
  EXPECT_TRUE(provider.downloads.empty());
}

TEST(BookmarkSyncConsentTest,
     ProviderReapprovalCannotAuthorizeAnAlreadyPostedDownload) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ControlledProvider provider;
  provider.hold_downloads = true;
  SyncPump pump(&store, &provider,
                SyncPump::Options{.bookmark_sync_enabled = true});
  bool completed = false;
  ASSERT_TRUE(pump.SyncNow(
      base::BindLambdaForTesting([&](bool success, std::string error) {
        completed = true;
        EXPECT_FALSE(success);
        EXPECT_EQ("cancelled", error);
      })));
  ASSERT_EQ(1u, provider.downloads.size());
  provider.CompleteDownload(0,
                            {.changes = {Remote(Bookmark(1), "stale-bookmark")},
                             .next_change_token = "stale-delivery"});
  // Model account/consent events between provider dispatch and the pump's
  // posted task. Current approval is true again, but the original scope is not.
  provider.RevokeFromProvider();
  provider.ReapproveFromProvider();
  tasks.RunUntilIdle();
  EXPECT_TRUE(completed);
  SyncRecord record;
  EXPECT_EQ(Result::kNotFound,
            store.GetRecord(EntityType::kBookmark, Id(1), &record));
  EXPECT_TRUE(store.GetChangeToken().empty());
  EXPECT_EQ(0, store.InboxCount());
}

}  // namespace
}  // namespace ahoi::sync
