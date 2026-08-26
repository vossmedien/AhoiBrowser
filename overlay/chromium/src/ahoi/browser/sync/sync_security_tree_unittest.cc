// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <string>

#include "ahoi/browser/sync/history_sync_filter.h"
#include "ahoi/browser/sync/remote_command_security.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace ahoi::sync {
namespace {

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

base::Time At(int64_t micros) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
}

SyncVersion Version(const char* device, int64_t physical) {
  return SyncVersion{.model_version = kCurrentModelVersion,
                     .stamp = HlcStamp{.physical_time_us = physical,
                                       .device_tiebreak = device}};
}

}  // namespace

TEST(RemoteCommandSecurityTest, MatchesSwiftGoldenAndVerifiesEd25519) {
  constexpr int64_t kIssued = 11644474600000000LL;
  RemoteCommandRecord command{
      .id = Id("c0000000-0000-4000-8000-000000000003"),
      .source_device_id = Id("a0000000-0000-4000-8000-000000000001"),
      .target_device_id = Id("b0000000-0000-4000-8000-000000000002"),
      .nonce_base64 = base::Base64Encode(std::string(16, '\0')),
      .issued_at = At(kIssued),
      .expires_at = At(kIssued) + base::Minutes(5),
      .kind = RemoteCommandKind::kOpen,
      .url = "https://example.test/a/b",
      .signature_base64 = base::Base64Encode(std::string(64, '\0')),
      .version = Version("a0000000-0000-4000-8000-000000000001", kIssued)};
  std::string canonical;
  ASSERT_TRUE(CanonicalRemoteCommandPayload(command, &canonical));
  EXPECT_EQ(canonical,
            "{\"command\":{\"kind\":\"open\",\"openRequest\":{\"url\":"
            "\"https://example.test/a/b\"}},\"commandID\":"
            "\"C0000000-0000-4000-8000-000000000003\","
            "\"issuedAtMilliseconds\":1000000,\"nonce\":"
            "\"AAAAAAAAAAAAAAAAAAAAAA==\",\"sourceDeviceID\":{\"rawValue\":"
            "\"A0000000-0000-4000-8000-000000000001\"},\"targetDeviceID\":"
            "{\"rawValue\":\"B0000000-0000-4000-8000-000000000002\"}}");

  uint8_t seed[32] = {};
  uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
  uint8_t private_key[ED25519_PRIVATE_KEY_LEN];
  uint8_t signature[ED25519_SIGNATURE_LEN];
  ED25519_keypair_from_seed(public_key, private_key, seed);
  ASSERT_EQ(ED25519_sign(signature,
                         reinterpret_cast<const uint8_t*>(canonical.data()),
                         canonical.size(), private_key),
            1);
  command.signature_base64 = base::Base64Encode(signature);
  RemoteCommandPolicy policy{
      .enabled = true,
      .approved_public_keys_base64 = {
          {command.source_device_id, base::Base64Encode(public_key)}}};
  EXPECT_EQ(ValidateRemoteCommandForExecution(
                command, command.target_device_id, policy,
                command.issued_at + base::Seconds(1)),
            RemoteCommandValidationFailure::kNone);
  command.url = "file:///tmp/secret";
  EXPECT_EQ(ValidateRemoteCommandForExecution(
                command, command.target_device_id, policy,
                command.issued_at + base::Seconds(1)),
            RemoteCommandValidationFailure::kInvalidPayload);
}

TEST(HistorySyncFilterTest, AllowsOnlyOrdinaryBrowsedNetworkVisits) {
  EXPECT_TRUE(
      ShouldSyncHistoryVisit({.url = GURL("https://example.test/path")}));
  EXPECT_TRUE(
      ShouldSyncHistoryVisit({.url = GURL("http://example.test/path")}));
  EXPECT_FALSE(ShouldSyncHistoryVisit(
      {.url = GURL("https://user:password@example.test/path")}));
  EXPECT_FALSE(ShouldSyncHistoryVisit({.url = GURL("file:///tmp/private")}));
  EXPECT_FALSE(ShouldSyncHistoryVisit(
      {.url = GURL("https://example.test"), .hidden = true}));
  EXPECT_FALSE(ShouldSyncHistoryVisit(
      {.url = GURL("https://example.test"), .response_is_404 = true}));
  EXPECT_FALSE(ShouldSyncHistoryVisit(
      {.url = GURL("https://example.test"), .source_is_browsed = false}));
}

TEST(SyncStoreTest, RemoteCommandReplayPersistsCommandAndNonce) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("sync.sqlite");
  const base::Uuid command = Id("30000000-0000-4000-8000-000000000011");
  const base::Uuid source = Id("10000000-0000-4000-8000-000000000012");
  const base::Time now = base::Time::Now();
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    EXPECT_EQ(store.ConsumeRemoteCommand(command, source, "nonce-a",
                                         now + base::Minutes(5), now),
              SyncStore::Result::kOk);
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    EXPECT_EQ(store.ConsumeRemoteCommand(command, source, "nonce-b",
                                         now + base::Minutes(5), now),
              SyncStore::Result::kAlreadyApplied);
    EXPECT_EQ(store.ConsumeRemoteCommand(
                  Id("30000000-0000-4000-8000-000000000013"), source, "nonce-a",
                  now + base::Minutes(5), now),
              SyncStore::Result::kAlreadyApplied);
  }
}

TEST(TabTreeSyncAdapterTest, RepairsCycleAndDeletedParentDeterministically) {
  const base::Uuid workspace = Id("40000000-0000-4000-8000-000000000001");
  const base::Uuid first = Id("40000000-0000-4000-8000-000000000002");
  const base::Uuid second = Id("40000000-0000-4000-8000-000000000003");
  const WorkspaceRecord workspace_record{.id = workspace,
                                         .name = "Work",
                                         .sort_key = "a",
                                         .created_at = base::Time::UnixEpoch(),
                                         .modified_at = base::Time::UnixEpoch(),
                                         .version = Version("device-a", 1)};
  TreeNodeRecord a{.id = first,
                   .workspace_id = workspace,
                   .parent_id = second,
                   .kind = TreeNodeKind::kFolder,
                   .title = "A",
                   .sort_key = "a",
                   .created_at = base::Time::UnixEpoch(),
                   .modified_at = base::Time::UnixEpoch(),
                   .version = Version("device-a", 2)};
  TreeNodeRecord b = a;
  b.id = second;
  b.parent_id = first;
  b.title = "B";
  b.sort_key = "b";
  b.version = Version("device-b", 2);
  std::optional<tab_tree::TabTreeSnapshot> repaired =
      ReconcileTabTreeRecords({}, {workspace_record}, {b, a});
  ASSERT_TRUE(repaired);
  ASSERT_EQ(repaired->nodes.size(), 3u);
  const auto recovered =
      std::ranges::find_if(repaired->nodes, [](const tab_tree::TreeNode& node) {
        return node.title == u"Wiederhergestellt";
      });
  ASSERT_NE(recovered, repaired->nodes.end());
  const auto cut =
      std::ranges::find(repaired->nodes, first, &tab_tree::TreeNode::id);
  ASSERT_NE(cut, repaired->nodes.end());
  EXPECT_EQ(cut->parent_id, recovered->id);
}

TEST(SyncStoreTest, AcceptsPartialTreeProviderPageForLaterRepair) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const TreeNodeRecord orphan{
      .id = Id("50000000-0000-4000-8000-000000000001"),
      .workspace_id = Id("50000000-0000-4000-8000-000000000002"),
      .parent_id = Id("50000000-0000-4000-8000-000000000003"),
      .kind = TreeNodeKind::kPage,
      .title = "Offline child",
      .url = "https://example.test/offline",
      .sort_key = "a",
      .created_at = base::Time::UnixEpoch(),
      .modified_at = base::Time::UnixEpoch(),
      .version = Version("device-b", 10)};
  std::string payload;
  ASSERT_TRUE(SerializeRecord(orphan, &payload));
  EXPECT_EQ(store.ApplyRemoteBatch(ProviderBatch{
                {SyncChange{"partial", EntityType::kTreeNode, orphan.id,
                            ChangeKind::kUpsert, orphan.version, payload}},
                "partial-token",
                false}),
            SyncStore::Result::kOk);
}

}  // namespace ahoi::sync
