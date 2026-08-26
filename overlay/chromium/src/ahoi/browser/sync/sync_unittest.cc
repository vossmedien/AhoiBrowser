// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/device_tabs_service.h"
#include "ahoi/browser/sync/history_sync_filter.h"
#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/remote_command_security.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
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

SyncVersion Version(const char* device,
                    int64_t physical,
                    uint32_t logical = 0) {
  return SyncVersion{.model_version = kCurrentModelVersion,
                     .stamp = HlcStamp{.physical_time_us = physical,
                                       .logical = logical,
                                       .device_tiebreak = device}};
}

RemoteTabRecord Tab(const char* id,
                    const char* device,
                    const char* session,
                    const char* url,
                    SyncVersion version,
                    bool incognito = false) {
  return RemoteTabRecord{.id = Id(id),
                         .device_id = Id(device),
                         .session_id = Id(session),
                         .url = url,
                         .title = "Ahoi",
                         .opened_at = At(10),
                         .last_active = At(version.stamp.physical_time_us),
                         .is_incognito = incognito,
                         .version = std::move(version)};
}

class SnapshotObserver final : public DeviceTabsService::Observer {
 public:
  void OnDeviceTabsSnapshot(const DeviceTabsSnapshot& snapshot) override {
    last = snapshot;
    ++calls;
  }

  DeviceTabsSnapshot last;
  int calls = 0;
};

class FakeSyncProvider final : public SyncProvider {
 public:
  struct UploadResult {
    bool success = true;
    std::vector<std::string> acknowledged_ids;
    std::string error;
  };
  struct DownloadResult {
    bool success = true;
    ProviderBatch batch;
    std::string error;
  };

  void Upload(std::vector<SyncChange> changes,
              UploadCallback callback) override {
    uploads.push_back(std::move(changes));
    ASSERT_FALSE(upload_results.empty());
    UploadResult result = std::move(upload_results.front());
    upload_results.pop_front();
    std::move(callback).Run(result.success, std::move(result.acknowledged_ids),
                            std::move(result.error));
  }

  void Download(std::string change_token, DownloadCallback callback) override {
    download_tokens.push_back(std::move(change_token));
    ASSERT_FALSE(download_results.empty());
    DownloadResult result = std::move(download_results.front());
    download_results.pop_front();
    std::move(callback).Run(result.success, std::move(result.batch),
                            std::move(result.error));
  }

  std::deque<UploadResult> upload_results;
  std::deque<DownloadResult> download_results;
  std::vector<std::vector<SyncChange>> uploads;
  std::vector<std::string> download_tokens;
};

}  // namespace

TEST(HybridLogicalClockTest, TicksAndObservesWithStableDeviceTieBreak) {
  HybridLogicalClock clock("device-a");
  const HlcStamp first = clock.Tick(At(100));
  const HlcStamp second = clock.Tick(At(100));
  EXPECT_EQ(first.physical_time_us, 100);
  EXPECT_EQ(second.logical, 1u);
  EXPECT_GT(second, first);

  const HlcStamp observed = clock.Observe(
      HlcStamp{
          .physical_time_us = 200, .logical = 4, .device_tiebreak = "device-z"},
      At(150));
  EXPECT_EQ(observed.physical_time_us, 200);
  EXPECT_EQ(observed.logical, 5u);
  EXPECT_EQ(observed.device_tiebreak, "device-a");
}

TEST(SyncMergeTest, ConcurrentVersionsUseDeviceTieBreakAndAreIdempotent) {
  const SyncVersion left = Version("device-a", 10);
  const SyncVersion right = Version("device-b", 10);
  EXPECT_EQ(DecideMerge(left, "left", right, "right"),
            MergeDecision::kAcceptIncoming);
  EXPECT_EQ(DecideMerge(right, "right", left, "left"),
            MergeDecision::kKeepExisting);
  EXPECT_EQ(DecideMerge(left, "same", left, "same"), MergeDecision::kDuplicate);
  EXPECT_EQ(DecideMerge(left, "left", left, "different"),
            MergeDecision::kInvalid);
}

TEST(SyncSerializationTest, RemoteTabRoundTripsAndRejectsIncognitoEnvelope) {
  RemoteTabRecord tab =
      Tab("10000000-0000-4000-8000-000000000001",
          "10000000-0000-4000-8000-000000000002",
          "10000000-0000-4000-8000-000000000003", "https://example.test/path",
          Version("device-a", 20));
  SyncRecord original = tab;
  std::string payload;
  ASSERT_TRUE(SerializeRecord(original, &payload));
  SyncRecord decoded;
  ASSERT_TRUE(DeserializeRecord(EntityType::kRemoteTab, payload, &decoded));
  EXPECT_EQ(std::get<RemoteTabRecord>(decoded).url, tab.url);
  EXPECT_EQ(std::get<RemoteTabRecord>(decoded).version, tab.version);

  tab.is_incognito = true;
  EXPECT_FALSE(ValidateRecord(tab));
  tab.is_incognito = false;
  tab.url = "https://user:password@example.test/private";
  EXPECT_FALSE(ValidateRecord(tab));
  tab.tombstone = true;
  tab.url = "file:///tmp/private";
  EXPECT_FALSE(ValidateRecord(tab));
}

TEST(SyncSerializationTest, DecodesLegacyMacIOSWireV1GoldenPayload) {
  constexpr int64_t kPhysical = 11644473601000000LL;
  const RemoteTabRecord tab{
      .model_version = 1,
      .id = Id("10000000-0000-4000-8000-000000000001"),
      .device_id = Id("10000000-0000-4000-8000-000000000002"),
      .session_id = Id("10000000-0000-4000-8000-000000000003"),
      .url = "https://example.test/path",
      .title = "Ahoi",
      .opened_at = At(kPhysical),
      .last_active = At(kPhysical),
      .version = {.model_version = 1,
                  .stamp = {.physical_time_us = kPhysical,
                            .logical = 2,
                            .device_tiebreak =
                                "10000000-0000-4000-8000-000000000002"}}};
  std::string payload;
  ASSERT_TRUE(SerializeRecord(tab, &payload));
  EXPECT_EQ(payload,
            "{\"device_id\":\"10000000-0000-4000-8000-000000000002\","
            "\"id\":\"10000000-0000-4000-8000-000000000001\","
            "\"is_incognito\":false,\"last_active\":\"11644473601000000\","
            "\"model_version\":1,\"opened_at\":\"11644473601000000\","
            "\"pinned\":false,"
            "\"session_id\":\"10000000-0000-4000-8000-000000000003\","
            "\"title\":\"Ahoi\",\"tombstone\":false,"
            "\"url\":\"https://example.test/path\","
            "\"version_device\":\"10000000-0000-4000-8000-000000000002\","
            "\"version_logical\":2,\"version_model\":1,"
            "\"version_physical\":\"11644473601000000\"}");
}

TEST(SyncPayloadCryptorTest, AESGCMEnvelopeRoundTripsAndRejectsTampering) {
  Aes256GcmSyncPayloadCryptor cryptor(std::vector<uint8_t>(32, 0x42), 7);
  std::optional<std::string> sealed = cryptor.Seal("private payload");
  ASSERT_TRUE(sealed);
  EXPECT_EQ(sealed->find("private payload"), std::string::npos);
  EXPECT_EQ(cryptor.Open(*sealed), "private payload");
  (*sealed)[sealed->size() / 2] ^= 1;
  EXPECT_FALSE(cryptor.Open(*sealed));
}

TEST(SyncPayloadCryptorTest, OpensSharedCryptoKitGoldenEnvelope) {
  Aes256GcmSyncPayloadCryptor cryptor(std::vector<uint8_t>(32, 0), 1);
  constexpr char kEnvelope[] =
      R"({"algorithm":"AES-256-GCM","ciphertextAndTag":"zqdAPU1ga24HTsXTuvOdGNDRyKeZmWvwJluYtdSKuRk=","keyVersion":1,"nonce":"AAAAAAAAAAAAAAAA"})";
  EXPECT_EQ(cryptor.Open(kEnvelope), std::string(16, '\0'));
}

TEST(SyncSerializationTest, EveryRecordTypeHasAStablePayload) {
  const base::Uuid device_id = Id("10000000-0000-4000-8000-000000000060");
  const base::Uuid workspace_id = Id("10000000-0000-4000-8000-000000000061");
  const base::Uuid session_id = Id("10000000-0000-4000-8000-000000000062");
  const SyncVersion version = Version("device-a", 60);
  std::vector<SyncRecord> records;
  records.emplace_back(DeviceRecord{.id = device_id,
                                    .type = DeviceType::kMacDesktop,
                                    .display_name = "Mac",
                                    .created_at = At(1),
                                    .last_seen = At(2),
                                    .version = version});
  records.emplace_back(WorkspaceRecord{.id = workspace_id,
                                       .name = "Work",
                                       .sort_key = "a",
                                       .created_at = At(1),
                                       .modified_at = At(2),
                                       .version = version});
  records.emplace_back(
      TreeNodeRecord{.id = Id("10000000-0000-4000-8000-000000000063"),
                     .workspace_id = workspace_id,
                     .kind = TreeNodeKind::kFolder,
                     .title = "Folder",
                     .sort_key = "a",
                     .created_at = At(1),
                     .modified_at = At(2),
                     .version = version});
  records.emplace_back(
      HistoryRecord{.id = Id("10000000-0000-4000-8000-000000000064"),
                    .url = "https://history.test",
                    .title = "History",
                    .last_visit = At(2),
                    .visit_count = 3,
                    .version = version});
  records.emplace_back(Tab("10000000-0000-4000-8000-000000000065",
                           device_id.AsLowercaseString().c_str(),
                           session_id.AsLowercaseString().c_str(),
                           "https://tab.test", version));
  records.emplace_back(DeviceSessionRecord{.id = session_id,
                                           .device_id = device_id,
                                           .started_at = At(1),
                                           .last_seen = At(2),
                                           .version = version});
  records.emplace_back(RemoteCommandRecord{
      .id = Id("10000000-0000-4000-8000-000000000066"),
      .source_device_id = device_id,
      .target_device_id = Id("10000000-0000-4000-8000-000000000067"),
      .nonce_base64 = base::Base64Encode(std::string(16, '\0')),
      .issued_at = At(1),
      .expires_at = At(1) + base::Minutes(5),
      .kind = RemoteCommandKind::kOpen,
      .url = "https://command.test",
      .signature_base64 = base::Base64Encode(std::string(64, '\0')),
      .version = version});
  records.emplace_back(
      AppearanceRecord{.id = Id("10000000-0000-4000-8000-000000000068"),
                       .color_mode = "dark",
                       .accent_argb = 0xff123456u,
                       .use_system_accent = false,
                       .version = version});
  records.emplace_back(
      PermittedSettingRecord{.id = Id("10000000-0000-4000-8000-000000000069"),
                             .setting_id = "ahoi.appearance.glass_enabled",
                             .value_json = "true",
                             .version = version});
  records.emplace_back(ExtensionInventoryRecord{
      .id = Id("10000000-0000-4000-8000-00000000006a"),
      .device_id = device_id,
      .extension_id = "abcdefghijklmnopabcdefghijklmnop",
      .name = "Example",
      .extension_version = "1.2.3",
      .enabled = true,
      .version = version});
  records.emplace_back(
      DeveloperAssetRecord{.id = Id("10000000-0000-4000-8000-00000000006b"),
                           .kind = DeveloperAssetKind::kCss,
                           .name = "Readable",
                           .scope = "https://example.test",
                           .source = "body { color: CanvasText; }",
                           .enabled = true,
                           .opted_in = true,
                           .version = version});

  for (const SyncRecord& original : records) {
    std::string payload;
    ASSERT_TRUE(SerializeRecord(original, &payload));
    SyncRecord decoded;
    ASSERT_TRUE(DeserializeRecord(GetEntityType(original), payload, &decoded));
    SyncRecord normalized = original;
    ASSERT_TRUE(NormalizeFieldVersions(&normalized));
    EXPECT_EQ(decoded, normalized);
    EXPECT_TRUE(ValidateRecord(decoded));
  }
}

TEST(SyncMergeTest, ProductRecordsEnforceOptInAndSecretBoundaries) {
  const SyncVersion version = Version("device-a", 61);
  PermittedSettingRecord setting{
      .id = Id("20000000-0000-4000-8000-000000000061"),
      .setting_id = "ahoi.appearance.glass_enabled",
      .value_json = "not-json",
      .version = version};
  EXPECT_FALSE(ValidateRecord(setting));
  setting.value_json = "false";
  EXPECT_TRUE(ValidateRecord(setting));

  DeveloperAssetRecord asset{.id = Id("20000000-0000-4000-8000-000000000062"),
                             .kind = DeveloperAssetKind::kJavaScript,
                             .name = "Helper",
                             .scope = "https://example.test",
                             .source = "document.body.dataset.ahoi = '1';",
                             .enabled = true,
                             .version = version};
  EXPECT_FALSE(ValidateRecord(asset));
  asset.opted_in = true;
  EXPECT_TRUE(ValidateRecord(asset));

  asset.kind = DeveloperAssetKind::kHeaderProfile;
  asset.source =
      R"({"version":1,"rules":[{"name":"Authorization","action":"set","value":"secret"}]})";
  EXPECT_FALSE(ValidateRecord(asset));
  asset.source =
      R"({"version":1,"rules":[{"name":"X-Debug","action":"remove"}]})";
  EXPECT_TRUE(ValidateRecord(asset));
}

TEST(SyncMergeTest, TreeGraphRejectsCyclesAndCrossWorkspaceParents) {
  const base::Uuid workspace = Id("10000000-0000-4000-8000-000000000010");
  TreeNodeRecord folder_a{.id = Id("10000000-0000-4000-8000-000000000011"),
                          .workspace_id = workspace,
                          .kind = TreeNodeKind::kFolder,
                          .version = Version("device-a", 1)};
  TreeNodeRecord folder_b{.id = Id("10000000-0000-4000-8000-000000000012"),
                          .workspace_id = workspace,
                          .parent_id = folder_a.id,
                          .kind = TreeNodeKind::kFolder,
                          .version = Version("device-a", 2)};
  EXPECT_TRUE(ValidateTreeGraph({folder_a, folder_b}));
  folder_a.parent_id = folder_b.id;
  EXPECT_FALSE(ValidateTreeGraph({folder_a, folder_b}));
  folder_a.parent_id.reset();
  folder_b.workspace_id = Id("10000000-0000-4000-8000-000000000013");
  EXPECT_FALSE(ValidateTreeGraph({folder_a, folder_b}));
  folder_b.workspace_id = workspace;
  folder_a.tombstone = true;
  folder_b.parent_id = folder_a.id;
  EXPECT_TRUE(ValidateTreeGraph({folder_a, folder_b}));
}

TEST(SyncStoreTest, LocalMutationIsAtomicWithOutboxAndPersists) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("sync.sqlite");
  const base::Uuid device = Id("10000000-0000-4000-8000-000000000020");
  const base::Uuid session = Id("10000000-0000-4000-8000-000000000021");
  const RemoteTabRecord tab = Tab(
      "10000000-0000-4000-8000-000000000022",
      device.AsLowercaseString().c_str(), session.AsLowercaseString().c_str(),
      "https://local.test", Version("device-a", 30));

  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    EXPECT_EQ(store.PutLocalRecord(tab, "mutation-local"),
              SyncStore::Result::kOk);
    EXPECT_EQ(store.PutLocalRecord(tab, "mutation-local"),
              SyncStore::Result::kAlreadyApplied);
    RemoteTabRecord conflicting = tab;
    conflicting.title = "different payload";
    EXPECT_EQ(store.PutLocalRecord(conflicting, "mutation-local"),
              SyncStore::Result::kConflict);
    EXPECT_EQ(store.PendingOutboxCount(), 1);
    std::vector<SyncChange> outbox;
    ASSERT_EQ(store.ReadOutbox(10, &outbox), SyncStore::Result::kOk);
    ASSERT_EQ(outbox.size(), 1u);
    EXPECT_EQ(outbox[0].mutation_id, "mutation-local");
    EXPECT_EQ(store.AcknowledgeOutbox({"mutation-local"}),
              SyncStore::Result::kOk);
    EXPECT_EQ(store.PendingOutboxCount(), 0);
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    std::vector<RemoteTabRecord> tabs;
    ASSERT_EQ(store.GetRemoteTabs(&tabs), SyncStore::Result::kOk);
    ASSERT_EQ(tabs.size(), 1u);
    EXPECT_EQ(tabs[0].url, "https://local.test");
  }
}

TEST(SyncStoreTest, CloudRecoveryPreservesRecordsAndRebuildsOutbox) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const RemoteTabRecord tab =
      Tab("10000000-0000-4000-8000-000000000023",
          "10000000-0000-4000-8000-000000000024",
          "10000000-0000-4000-8000-000000000025", "https://recovery.test",
          Version("device-a", 35));
  ASSERT_EQ(store.PutLocalRecord(tab, "before-account-change"),
            SyncStore::Result::kOk);
  EXPECT_EQ(store.PrepareOutboxForCloudRecovery(false), SyncStore::Result::kOk);
  EXPECT_EQ(store.PendingOutboxCount(), 0);
  SyncRecord retained;
  EXPECT_EQ(store.GetRecord(EntityType::kRemoteTab, tab.id, &retained),
            SyncStore::Result::kOk);
  EXPECT_EQ(store.PrepareOutboxForCloudRecovery(true), SyncStore::Result::kOk);
  std::vector<SyncChange> outbox;
  ASSERT_EQ(store.ReadOutbox(10, &outbox), SyncStore::Result::kOk);
  ASSERT_EQ(outbox.size(), 1u);
  std::string retained_payload;
  ASSERT_TRUE(SerializeRecord(retained, &retained_payload));
  EXPECT_NE(outbox[0].mutation_id, "before-account-change");
  EXPECT_EQ(outbox[0].payload, retained_payload);
}

TEST(SyncStoreTest, RemotePageIsIdempotentAndAdvancesTokenAtomically) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const RemoteTabRecord tab =
      Tab("10000000-0000-4000-8000-000000000030",
          "10000000-0000-4000-8000-000000000031",
          "10000000-0000-4000-8000-000000000032", "https://remote.test",
          Version("device-b", 40));
  std::string payload;
  ASSERT_TRUE(SerializeRecord(tab, &payload));
  const SyncChange change{.mutation_id = "remote-mutation",
                          .entity_type = EntityType::kRemoteTab,
                          .entity_id = tab.id,
                          .kind = ChangeKind::kUpsert,
                          .version = tab.version,
                          .payload = payload};
  const ProviderBatch batch{
      .changes = {change}, .next_change_token = "token-1", .has_more = false};
  EXPECT_EQ(store.ApplyRemoteBatch(batch), SyncStore::Result::kOk);
  EXPECT_EQ(store.ApplyRemoteBatch(batch), SyncStore::Result::kOk);
  EXPECT_EQ(store.InboxCount(), 1);
  EXPECT_EQ(store.GetChangeToken(), "token-1");
  std::vector<RemoteTabRecord> tabs;
  ASSERT_EQ(store.GetRemoteTabs(&tabs), SyncStore::Result::kOk);
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_EQ(tabs[0].device_id, tab.device_id);

  RemoteTabRecord deleted = tab;
  deleted.tombstone = true;
  deleted.version = Version("device-b", 41);
  ASSERT_TRUE(SerializeRecord(deleted, &payload));
  const SyncChange delete_change{.mutation_id = "remote-delete",
                                 .entity_type = EntityType::kRemoteTab,
                                 .entity_id = deleted.id,
                                 .kind = ChangeKind::kDelete,
                                 .version = deleted.version,
                                 .payload = payload};
  EXPECT_EQ(
      store.ApplyRemoteBatch(ProviderBatch{{delete_change}, "token-2", false}),
      SyncStore::Result::kOk);
  ASSERT_EQ(store.GetRemoteTabs(&tabs), SyncStore::Result::kOk);
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_TRUE(tabs[0].tombstone);
}

TEST(DeviceTabsServiceTest, PublishesNormalLocalAndRemoteTabsOnly) {
  auto store = std::make_unique<SyncStore>();
  ASSERT_TRUE(store->InitializeInMemory());
  const base::Uuid local_device = Id("10000000-0000-4000-8000-000000000040");
  auto service =
      DeviceTabsService::CreateForTesting(std::move(store), local_device);
  SnapshotObserver observer;
  service->Subscribe(&observer);
  EXPECT_EQ(observer.calls, 1);

  const RemoteTabRecord local =
      Tab("10000000-0000-4000-8000-000000000041",
          "10000000-0000-4000-8000-000000000040",
          "10000000-0000-4000-8000-000000000042", "https://local.test",
          Version("device-a", 50));
  const RemoteTabRecord remote =
      Tab("10000000-0000-4000-8000-000000000043",
          "10000000-0000-4000-8000-000000000044",
          "10000000-0000-4000-8000-000000000045", "https://remote.test",
          Version("device-b", 51));
  const DeviceSessionRecord local_session{.id = local.session_id,
                                          .device_id = local.device_id,
                                          .started_at = base::Time::Now(),
                                          .last_seen = base::Time::Now(),
                                          .version = Version("device-a", 49)};
  const DeviceRecord remote_device{.id = remote.device_id,
                                   .type = DeviceType::kIPhone,
                                   .display_name = "iPhone",
                                   .created_at = At(1),
                                   .last_seen = At(52),
                                   .version = Version("device-b", 52)};
  const WorkspaceRecord remote_workspace{
      .id = Id("10000000-0000-4000-8000-000000000047"),
      .name = "Mobil",
      .sort_key = "a",
      .created_at = At(1),
      .modified_at = At(53),
      .version = Version("device-b", 53)};
  const DeviceSessionRecord remote_session{
      .id = remote.session_id,
      .device_id = remote.device_id,
      .started_at = base::Time::Now() - base::Days(8),
      .last_seen = base::Time::Now() - base::Days(8),
      .version = Version("device-b", 54)};
  RemoteTabRecord incognito = remote;
  incognito.id = Id("10000000-0000-4000-8000-000000000046");
  incognito.is_incognito = true;
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(local),
            SyncStore::Result::kOk);
  EXPECT_TRUE(observer.last.local_tabs.empty());
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(local_session),
            SyncStore::Result::kOk);
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(remote_device),
            SyncStore::Result::kOk);
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(remote_workspace),
            SyncStore::Result::kOk);
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(remote_session),
            SyncStore::Result::kOk);
  std::string payload;
  ASSERT_TRUE(SerializeRecord(remote, &payload));
  EXPECT_EQ(service->store_for_testing()->ApplyRemoteBatch(ProviderBatch{
                {SyncChange{"remote", EntityType::kRemoteTab, remote.id,
                            ChangeKind::kUpsert, remote.version, payload}},
                "token",
                false}),
            SyncStore::Result::kOk);
  EXPECT_EQ(observer.last.local_tabs.size(), 1u);
  EXPECT_TRUE(observer.last.remote_tabs.empty());

  DeviceSessionRecord mismatched_session = remote_session;
  mismatched_session.device_id = local_device;
  mismatched_session.last_seen = base::Time::Now();
  mismatched_session.version = Version("device-b", 55);
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(mismatched_session),
            SyncStore::Result::kOk);
  EXPECT_TRUE(observer.last.remote_tabs.empty());

  DeviceSessionRecord current_session = remote_session;
  current_session.last_seen = base::Time::Now();
  current_session.version = Version("device-b", 56);
  EXPECT_TRUE(DeviceTabsService::IsRemoteSessionActionable(current_session,
                                                           base::Time::Now()));
  current_session.last_seen = base::Time::Now() - base::Minutes(16);
  EXPECT_FALSE(DeviceTabsService::IsRemoteSessionActionable(current_session,
                                                            base::Time::Now()));
  current_session.last_seen = base::Time::Now();
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(current_session),
            SyncStore::Result::kOk);
  ASSERT_EQ(observer.last.remote_tabs.size(), 1u);
  EXPECT_EQ(observer.last.local_tabs[0].device_id, local_device);
  EXPECT_EQ(observer.last.remote_tabs[0].device_id, remote.device_id);
  ASSERT_EQ(observer.last.devices.size(), 1u);
  EXPECT_EQ(observer.last.devices[0].display_name, "iPhone");
  ASSERT_EQ(observer.last.workspaces.size(), 1u);
  EXPECT_EQ(observer.last.workspaces[0].name, "Mobil");
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(incognito),
            SyncStore::Result::kInvalidArgument);

  DeviceSessionRecord ended_session = current_session;
  ended_session.active = false;
  ended_session.version = Version("device-b", 57);
  EXPECT_EQ(service->store_for_testing()->PutLocalRecord(ended_session),
            SyncStore::Result::kOk);
  EXPECT_TRUE(observer.last.remote_tabs.empty());
  service->Unsubscribe(&observer);
}

TEST(SyncPumpTest, AcknowledgesOutboxAndDrainsEveryRemotePage) {
  base::test::TaskEnvironment task_environment;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const RemoteTabRecord local =
      Tab("10000000-0000-4000-8000-000000000071",
          "10000000-0000-4000-8000-000000000072",
          "10000000-0000-4000-8000-000000000073", "https://local.test",
          Version("device-a", 70));
  ASSERT_EQ(store.PutLocalRecord(local, "local-70"), SyncStore::Result::kOk);

  const RemoteTabRecord remote =
      Tab("10000000-0000-4000-8000-000000000074",
          "10000000-0000-4000-8000-000000000075",
          "10000000-0000-4000-8000-000000000076", "https://remote.test",
          Version("device-b", 71));
  std::string payload;
  ASSERT_TRUE(SerializeRecord(remote, &payload));
  const SyncChange remote_change{.mutation_id = "remote-71",
                                 .entity_type = EntityType::kRemoteTab,
                                 .entity_id = remote.id,
                                 .kind = ChangeKind::kUpsert,
                                 .version = remote.version,
                                 .payload = payload};

  FakeSyncProvider provider;
  provider.upload_results.push_back(
      {.success = true, .acknowledged_ids = {"local-70"}});
  provider.download_results.push_back(
      {.success = true,
       .batch = ProviderBatch{{remote_change}, "page-1", true}});
  provider.download_results.push_back(
      {.success = true, .batch = ProviderBatch{{}, "page-2", false}});

  bool completed = false;
  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow(base::BindOnce(
      [](bool* completed, bool success, std::string error) {
        EXPECT_TRUE(success);
        EXPECT_TRUE(error.empty());
        *completed = true;
      },
      &completed)));
  task_environment.RunUntilIdle();

  EXPECT_TRUE(completed);
  EXPECT_FALSE(pump.syncing_for_testing());
  EXPECT_EQ(store.PendingOutboxCount(), 0);
  EXPECT_EQ(store.GetChangeToken(), "page-2");
  ASSERT_EQ(provider.uploads.size(), 1u);
  ASSERT_EQ(provider.uploads[0].size(), 1u);
  EXPECT_EQ(provider.download_tokens, (std::vector<std::string>{"", "page-1"}));
  std::vector<RemoteTabRecord> tabs;
  ASSERT_EQ(store.GetRemoteTabs(&tabs), SyncStore::Result::kOk);
  ASSERT_EQ(tabs.size(), 2u);
}

TEST(SyncPumpTest, InvalidAcknowledgementKeepsOutboxAndPersistsSafeBackoff) {
  base::test::TaskEnvironment task_environment;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  const RemoteTabRecord local =
      Tab("10000000-0000-4000-8000-000000000081",
          "10000000-0000-4000-8000-000000000082",
          "10000000-0000-4000-8000-000000000083", "https://local.test",
          Version("device-a", 80));
  ASSERT_EQ(store.PutLocalRecord(local, "local-80"), SyncStore::Result::kOk);

  FakeSyncProvider provider;
  provider.upload_results.push_back(
      {.success = true, .acknowledged_ids = {"not-ours"}});
  bool completed = false;
  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow(base::BindOnce(
      [](bool* completed, bool success, std::string error) {
        EXPECT_FALSE(success);
        EXPECT_EQ(error, "provider_error");
        *completed = true;
      },
      &completed)));
  task_environment.RunUntilIdle();

  EXPECT_TRUE(completed);
  EXPECT_EQ(store.PendingOutboxCount(), 1);
  EXPECT_EQ(store.GetRetryState().attempt, 1);
  EXPECT_EQ(store.GetRetryState().last_error, "provider_error");
  EXPECT_TRUE(provider.download_tokens.empty());
}

TEST(SyncPumpTest, RejectsNonAdvancingProviderPaginationToken) {
  base::test::TaskEnvironment task_environment;
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  FakeSyncProvider provider;
  provider.download_results.push_back(
      {.success = true, .batch = ProviderBatch{{}, "", true}});
  bool completed = false;
  SyncPump pump(&store, &provider);
  ASSERT_TRUE(pump.SyncNow(base::BindOnce(
      [](bool* completed, bool success, std::string error) {
        EXPECT_FALSE(success);
        EXPECT_EQ(error, "provider_error");
        *completed = true;
      },
      &completed)));
  task_environment.RunUntilIdle();

  EXPECT_TRUE(completed);
  EXPECT_EQ(store.GetRetryState().attempt, 1);
  ASSERT_EQ(provider.download_tokens.size(), 1u);
}

}  // namespace ahoi::sync
