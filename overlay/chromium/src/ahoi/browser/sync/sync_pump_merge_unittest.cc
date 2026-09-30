// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// A "merge_required" upload (CloudKit serverRecordChanged with a staged
// newer server version) is resolved in the same cycle, or retried after
// seconds. It must never wait out the generic transport backoff.

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/sync_unittest_support.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using test_support::Id;
using test_support::kDeviceA;
using test_support::Ts;
using test_support::Version;

class MergeFakeProvider final : public SyncProvider {
 public:
  struct Reply {
    bool success = true;
    std::vector<std::string> acknowledged_ids;
    std::string error;
    std::string next_token;
  };

  void Upload(std::vector<SyncChange> changes,
              UploadCallback callback) override {
    uploads.push_back(std::move(changes));
    ASSERT_FALSE(upload_replies.empty());
    Reply reply = std::move(upload_replies.front());
    upload_replies.pop_front();
    std::move(callback).Run(reply.success, std::move(reply.acknowledged_ids),
                            std::move(reply.error));
  }

  void Download(std::string change_token, DownloadCallback callback) override {
    download_tokens.push_back(std::move(change_token));
    ASSERT_FALSE(download_replies.empty());
    Reply reply = std::move(download_replies.front());
    download_replies.pop_front();
    std::move(callback).Run(reply.success,
                            ProviderBatch{{}, reply.next_token, false},
                            std::move(reply.error));
  }

  SyncAuthorization GetTransportAuthorization() override { return allowed; }
  SyncAuthorization GetDownloadAuthorization(const std::string&) override {
    return allowed;
  }
  bool AcknowledgeDownloaded(std::string, SyncAuthorization) override {
    return true;
  }

  SyncAuthorization allowed = base::BindRepeating([] { return true; });
  std::deque<Reply> upload_replies;
  std::deque<Reply> download_replies;
  std::vector<std::vector<SyncChange>> uploads;
  std::vector<std::string> download_tokens;
};

void QueueLocalPage(SyncStore* store) {
  const TreeNodeRecord page{
      .id = Id("10000000-0000-4000-8000-0000000000a1"),
      .workspace_id = Id("10000000-0000-4000-8000-0000000000a0"),
      .kind = TreeNodeKind::kPage,
      .title = "Ahoi",
      .url = "https://merge.test",
      .sort_key = "0",
      .created_at = Ts(1),
      .modified_at = Ts(1),
      .version = Version(kDeviceA, 1),
      .is_temporary = true,
      .target_kind = SharedTabTargetKind::kWeb};
  ASSERT_EQ(store->PutLocalRecord(page, "local-page"), SyncStore::Result::kOk);
}

// Earlier transport failures leave a long generic backoff behind.
void MarkEarlierTransportFailures(SyncStore* store) {
  for (int i = 0; i < 12; ++i) {
    ASSERT_EQ(store->MarkRetry(base::Time::Now() + base::Hours(1), "network"),
              SyncStore::Result::kOk);
  }
}

MergeFakeProvider::Reply MergeRequired() {
  return {.success = false, .error = "merge_required"};
}

}  // namespace

TEST(SyncPumpMergeTest, ImportsStagedServerVersionAndResendsInSameCycle) {
  base::test::TaskEnvironment tasks;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_NO_FATAL_FAILURE(QueueLocalPage(&store));
  MergeFakeProvider provider;
  provider.upload_replies.push_back(MergeRequired());
  provider.download_replies.push_back({.next_token = "merged-1"});
  provider.upload_replies.push_back({.acknowledged_ids = {"local-page"}});
  provider.download_replies.push_back({.next_token = "merged-1"});

  bool succeeded = false;
  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow(base::BindLambdaForTesting(
      [&](bool success, std::string error) {
        EXPECT_TRUE(error.empty());
        succeeded = success;
      })));
  tasks.RunUntilIdle();

  EXPECT_TRUE(succeeded);
  EXPECT_EQ(provider.uploads.size(), 2u);
  EXPECT_EQ(provider.download_tokens,
            (std::vector<std::string>{"", "merged-1"}));
  EXPECT_EQ(store.PendingOutboxCount(), 0);
  EXPECT_EQ(store.GetRetryState().attempt, 0);
}

TEST(SyncPumpMergeTest, PersistentConflictRetriesAfterSecondsOnItsOwn) {
  base::test::TaskEnvironment tasks(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_NO_FATAL_FAILURE(QueueLocalPage(&store));
  MergeFakeProvider provider;
  // Two resend rounds within the cycle, then a short scheduled retry.
  for (int round = 0; round < 3; ++round) {
    provider.upload_replies.push_back(MergeRequired());
    if (round < 2)
      provider.download_replies.push_back({.next_token = "t"});
  }

  std::string failure;
  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow(base::BindLambdaForTesting(
      [&](bool success, std::string error) {
        EXPECT_FALSE(success);
        failure = std::move(error);
      })));
  tasks.RunUntilIdle();

  EXPECT_EQ(failure, "merge_required");
  EXPECT_EQ(provider.uploads.size(), 3u);
  EXPECT_EQ(store.PendingOutboxCount(), 1);
  const RetryState retry = store.GetRetryState();
  EXPECT_EQ(retry.last_error, "merge_required");
  EXPECT_LE(retry.next_attempt - base::Time::Now(), base::Seconds(5));

  // No periodic check or user click: the pump wakes itself up.
  provider.upload_replies.push_back({.acknowledged_ids = {"local-page"}});
  provider.download_replies.push_back({.next_token = "t"});
  tasks.FastForwardBy(base::Seconds(5));

  EXPECT_EQ(provider.uploads.size(), 4u);
  EXPECT_EQ(store.PendingOutboxCount(), 0);
  EXPECT_EQ(store.GetRetryState().attempt, 0);
  EXPECT_FALSE(pump.syncing_for_testing());
}

TEST(SyncPumpMergeTest, ConflictDeadlineIgnoresEarlierTransportBackoff) {
  base::test::TaskEnvironment tasks(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_NO_FATAL_FAILURE(QueueLocalPage(&store));
  ASSERT_NO_FATAL_FAILURE(MarkEarlierTransportFailures(&store));
  MergeFakeProvider provider;
  provider.upload_replies.push_back(MergeRequired());
  provider.upload_replies.push_back(MergeRequired());

  SyncPump::Options options;
  options.maximum_merge_rounds = 0;
  SyncPump pump(&store, &provider, options);
  // The user-initiated attempt bypasses the stored one-hour deadline.
  ASSERT_TRUE(pump.SyncNow({}, /*user_initiated=*/true));
  tasks.RunUntilIdle();

  // Twelve earlier failures would put the generic backoff at the one-hour
  // cap. The conflict deadline is five seconds, then doubles.
  EXPECT_EQ(provider.uploads.size(), 1u);
  EXPECT_EQ(store.GetRetryState().next_attempt - base::Time::Now(),
            base::Seconds(5));
  tasks.FastForwardBy(base::Seconds(5));
  EXPECT_EQ(provider.uploads.size(), 2u);
  EXPECT_EQ(store.GetRetryState().next_attempt - base::Time::Now(),
            base::Seconds(10));
  EXPECT_TRUE(provider.download_tokens.empty());
}

TEST(SyncPumpMergeTest, OtherFailuresKeepTheGenericBackoff) {
  base::test::TaskEnvironment tasks(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  ASSERT_NO_FATAL_FAILURE(QueueLocalPage(&store));
  ASSERT_NO_FATAL_FAILURE(MarkEarlierTransportFailures(&store));
  MergeFakeProvider provider;
  provider.upload_replies.push_back({.success = false, .error = "network"});

  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow({}, /*user_initiated=*/true));
  tasks.RunUntilIdle();
  EXPECT_EQ(store.GetRetryState().next_attempt - base::Time::Now(),
            base::Hours(1));
  // No self-scheduled wake-up for a transport failure.
  tasks.FastForwardBy(base::Minutes(10));
  EXPECT_EQ(provider.uploads.size(), 1u);
  EXPECT_TRUE(provider.download_tokens.empty());
}

}  // namespace ahoi::sync
