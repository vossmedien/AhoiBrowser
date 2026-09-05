// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>

#include <memory>
#include <string>
#include <utility>

#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_record_codec_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_util_mac.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"

namespace ahoi::sync {
namespace {

struct CryptorCalls {
  int opened = 0;
  int sealed = 0;
};

class CountingCryptor final : public SyncPayloadCryptor {
 public:
  explicit CountingCryptor(std::shared_ptr<CryptorCalls> calls)
      : calls_(std::move(calls)) {}
  std::optional<std::string> Seal(std::string_view plaintext) override {
    ++calls_->sealed;
    return "sealed:" + std::string(plaintext);
  }
  std::optional<std::string> Open(std::string_view envelope) override {
    ++calls_->opened;
    return envelope.starts_with("sealed:")
               ? std::make_optional(std::string(envelope.substr(7)))
               : std::nullopt;
  }

 private:
  std::shared_ptr<CryptorCalls> calls_;
};

SyncChange BookmarkChange() {
  constexpr int64_t kTime = 11644473600000200LL;
  const BookmarkRecord record{
      .id = base::Uuid::ParseLowercase("94000000-0000-4000-8000-000000000001"),
      .root_kind = BookmarkRoot::kOther,
      .sort_key = "a",
      .title = "Never decode before consent",
      .created_at =
          base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(kTime)),
      .version = {.stamp = {.physical_time_us = kTime,
                            .logical = 3,
                            .device_tiebreak = "device-a"}}};
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  return {.mutation_id = "fixture",
          .entity_type = EntityType::kBookmark,
          .entity_id = record.id,
          .version = record.version,
          .payload = std::move(payload)};
}

CKRecord* BookmarkCloudRecord(bool valid_ciphertext = true) {
  const SyncChange change = BookmarkChange();
  CKRecordID* record_id = [[CKRecordID alloc]
      initWithRecordName:ToNSString(change.entity_id.AsLowercaseString())];
  return EncodeCloudKitSyncRecord(
      change,
      valid_ciphertext ? "sealed:" + change.payload : "unreadable-ciphertext",
      record_id, nil);
}

CKRecord* AppearanceCloudRecord() {
  const SyncChange bookmark = BookmarkChange();
  const AppearanceRecord record{
      .id = base::Uuid::ParseLowercase("94000000-0000-4000-8000-000000000002"),
      .color_mode = "dark",
      .version = bookmark.version};
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  const SyncChange change{.mutation_id = "appearance",
                          .entity_type = EntityType::kAppearance,
                          .entity_id = record.id,
                          .version = record.version,
                          .payload = payload};
  CKRecordID* record_id = [[CKRecordID alloc]
      initWithRecordName:ToNSString(record.id.AsLowercaseString())];
  return EncodeCloudKitSyncRecord(change, "sealed:" + payload, record_id, nil);
}

}  // namespace

// This fixture skips CKSyncEngine/network initialization, but exercises the
// production Core receive/cache/decode/delivery/account paths with real
// CKRecords.
class CloudKitBookmarkSyncConsentTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    path_ = directory_.GetPath().AppendASCII("consent.state");
  }
  std::unique_ptr<CloudKitSyncProviderMac> Create(
      const std::shared_ptr<CryptorCalls>& calls,
      bool enabled = false) {
    return CloudKitSyncProviderMac::CreateForConsentTesting(
        path_, std::make_unique<CountingCryptor>(calls), enabled);
  }
  void Receive(CloudKitSyncProviderMac& provider, CKRecord* record) {
    provider.ReceiveRecordForTesting(record);
  }
  void AccountChanged(CloudKitSyncProviderMac& provider) {
    provider.AccountChangedForTesting();
  }
  base::RepeatingCallback<bool()> QueuedRecord(
      CloudKitSyncProviderMac& provider,
      CKRecord* record) {
    return provider.MakeDelayedRecordDeliveryForTesting(record);
  }
  void QueueRead(CloudKitSyncProviderMac& provider,
                 std::string token,
                 SyncProvider::DownloadCallback callback) {
    provider.ReadCachedChangesForTesting(std::move(token), std::move(callback));
  }
  ProviderBatch Read(CloudKitSyncProviderMac& provider,
                     std::string token = {}) {
    ProviderBatch result;
    bool completed = false;
    QueueRead(provider, std::move(token),
              base::BindLambdaForTesting(
                  [&](bool success, ProviderBatch batch, std::string error) {
                    completed = true;
                    EXPECT_TRUE(success);
                    EXPECT_TRUE(error.empty());
                    result = std::move(batch);
                  }));
    tasks_.RunUntilIdle();
    EXPECT_TRUE(completed);
    return result;
  }
  NSDictionary* Inbox() {
    NSData* data = [NSData
        dataWithContentsOfFile:ToNSString(
                                   path_.AddExtensionASCII("inbox").value())];
    EXPECT_TRUE(data);
    return data ? [NSJSONSerialization JSONObjectWithData:data
                                                  options:0
                                                    error:nil]
                : nil;
  }
  base::test::TaskEnvironment tasks_;
  base::ScopedTempDir directory_;
  base::FilePath path_;
};

TEST_F(CloudKitBookmarkSyncConsentTest,
       OpaqueReceiveSurvivesRestartUntilOptIn) {
  auto calls = std::make_shared<CryptorCalls>();
  {
    auto provider = Create(calls);
    Receive(*provider, BookmarkCloudRecord());
    EXPECT_EQ(0, calls->opened);
    EXPECT_EQ(0, calls->sealed);
    EXPECT_TRUE(Read(*provider).changes.empty());
    NSDictionary* inbox = Inbox();
    EXPECT_EQ(0u, [inbox[@"changes"] count]);
    EXPECT_EQ(1u, [inbox[@"opaqueBookmarks"] count]);
    EXPECT_FALSE([inbox[@"bookmarkConsentRevoked"] boolValue]);
  }
  auto restarted_calls = std::make_shared<CryptorCalls>();
  {
    auto provider = Create(restarted_calls);
    EXPECT_EQ(0, restarted_calls->opened);
    EXPECT_TRUE(Read(*provider).changes.empty());
    provider->SetBookmarkSyncEnabled(true);
    EXPECT_EQ(1, restarted_calls->opened);
    const ProviderBatch delivery = Read(*provider);
    ASSERT_EQ(1u, delivery.changes.size());
    EXPECT_EQ(EntityType::kBookmark, delivery.changes[0].entity_type);
    EXPECT_EQ(BookmarkChange().payload, delivery.changes[0].payload);
    EXPECT_FALSE(delivery.next_change_token.empty());
    EXPECT_EQ(1u, [Inbox()[@"opaqueBookmarks"] count]);
    EXPECT_TRUE(Read(*provider, delivery.next_change_token).changes.empty());
    EXPECT_EQ(0u, [Inbox()[@"opaqueBookmarks"] count]);
  }
  auto final_calls = std::make_shared<CryptorCalls>();
  auto provider = Create(final_calls, true);
  EXPECT_EQ(0, final_calls->opened);
  EXPECT_TRUE(Read(*provider).changes.empty());
}

TEST_F(CloudKitBookmarkSyncConsentTest,
       RevocationInvalidatesAlreadyPostedDelivery) {
  auto calls = std::make_shared<CryptorCalls>();
  auto provider = Create(calls, true);
  Receive(*provider, BookmarkCloudRecord());
  ASSERT_EQ(1, calls->opened);
  bool completed = false;
  QueueRead(*provider, {},
            base::BindLambdaForTesting(
                [&](bool success, ProviderBatch batch, std::string error) {
                  completed = true;
                  EXPECT_FALSE(success);
                  EXPECT_TRUE(batch.changes.empty());
                  EXPECT_EQ("cancelled", error);
                }));
  provider->SetBookmarkSyncEnabled(false);
  tasks_.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_TRUE(Read(*provider).changes.empty());
  EXPECT_EQ(1, calls->opened);
  EXPECT_EQ(1u, [Inbox()[@"opaqueBookmarks"] count]);
  provider->SetBookmarkSyncEnabled(true);
  EXPECT_EQ(2, calls->opened);
  EXPECT_EQ(1u, Read(*provider).changes.size());
}

TEST_F(CloudKitBookmarkSyncConsentTest,
       OtherCategoriesProgressWithoutAcknowledgingOpaqueBookmarks) {
  auto calls = std::make_shared<CryptorCalls>();
  auto provider = Create(calls);
  Receive(*provider, BookmarkCloudRecord());
  Receive(*provider, AppearanceCloudRecord());
  EXPECT_EQ(1, calls->opened);
  const ProviderBatch delivery = Read(*provider);
  ASSERT_EQ(1u, delivery.changes.size());
  EXPECT_EQ(EntityType::kAppearance, delivery.changes[0].entity_type);
  EXPECT_TRUE(Read(*provider, delivery.next_change_token).changes.empty());
  EXPECT_EQ(1u, [Inbox()[@"opaqueBookmarks"] count]);
  EXPECT_EQ(1, calls->opened);
  provider->SetBookmarkSyncEnabled(true);
  EXPECT_EQ(2, calls->opened);
  const ProviderBatch bookmarks = Read(*provider, delivery.next_change_token);
  ASSERT_EQ(1u, bookmarks.changes.size());
  EXPECT_EQ(EntityType::kBookmark, bookmarks.changes[0].entity_type);
}

TEST_F(CloudKitBookmarkSyncConsentTest,
       MalformedCiphertextWaitsForConsentToQuarantine) {
  auto calls = std::make_shared<CryptorCalls>();
  auto provider = Create(calls);
  Receive(*provider, BookmarkCloudRecord(false));
  EXPECT_EQ(0, calls->opened);
  EXPECT_TRUE(Read(*provider).changes.empty());
  provider->SetBookmarkSyncEnabled(true);
  EXPECT_EQ(1, calls->opened);
  const ProviderBatch delivery = Read(*provider);
  ASSERT_EQ(1u, delivery.changes.size());
  EXPECT_EQ(EntityType::kBookmark, delivery.changes[0].entity_type);
  EXPECT_EQ("{}", delivery.changes[0].payload);
  EXPECT_TRUE(Read(*provider, delivery.next_change_token).changes.empty());
  EXPECT_EQ(0u, [Inbox()[@"opaqueBookmarks"] count]);
}

TEST_F(CloudKitBookmarkSyncConsentTest,
       AccountChangeDiscardsCiphertextAndApproval) {
  auto calls = std::make_shared<CryptorCalls>();
  {
    auto provider = Create(calls);
    Receive(*provider, BookmarkCloudRecord());
    EXPECT_EQ(1u, [Inbox()[@"opaqueBookmarks"] count]);
    AccountChanged(*provider);
    EXPECT_TRUE(provider->IsAccountTransitionPending());
    EXPECT_TRUE(provider->IsBookmarkConsentRevoked());
    EXPECT_EQ(0u, [Inbox()[@"opaqueBookmarks"] count]);
    EXPECT_TRUE([Inbox()[@"bookmarkConsentRevoked"] boolValue]);
    provider->SetBookmarkSyncEnabled(true);
    EXPECT_EQ(0, calls->opened);
  }
  auto restarted_calls = std::make_shared<CryptorCalls>();
  // A stale constructor value cannot override the account revocation marker.
  auto provider = Create(restarted_calls, true);
  EXPECT_TRUE(provider->IsAccountTransitionPending());
  EXPECT_TRUE(provider->IsBookmarkConsentRevoked());
  EXPECT_EQ(0, restarted_calls->opened);
}

TEST_F(CloudKitBookmarkSyncConsentTest,
       OldRecordProviderCannotSendAfterRevocationOrReapproval) {
  auto calls = std::make_shared<CryptorCalls>();
  auto provider = Create(calls, true);
  auto old_delivery = QueuedRecord(*provider, BookmarkCloudRecord());
  EXPECT_TRUE(old_delivery.Run());
  provider->SetBookmarkSyncEnabled(false);
  EXPECT_FALSE(old_delivery.Run());
  provider->SetBookmarkSyncEnabled(true);
  EXPECT_FALSE(old_delivery.Run());
  auto new_delivery = QueuedRecord(*provider, BookmarkCloudRecord());
  EXPECT_TRUE(new_delivery.Run());
  provider.reset();
  EXPECT_FALSE(new_delivery.Run());
}

}  // namespace ahoi::sync
#pragma clang diagnostic pop
