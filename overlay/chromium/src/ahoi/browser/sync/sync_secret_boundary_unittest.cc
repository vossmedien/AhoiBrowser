// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

// Payload stability and the DoD-14 secret boundary for every synced record
// type, split from sync_unittest.cc (source line budget).

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahoi/browser/sync/shared_workspace_structure_types.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_unified_validation.h"
#include "ahoi/browser/sync/sync_unittest_support.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using test_support::Id;
using test_support::kDeviceA;
using test_support::Tab;
using test_support::Ts;
using test_support::Version;

// A valid one-page archive entry; its id is derived from the snapshot.
TabArchiveEntryRecord SampleArchiveEntry(const base::Uuid& workspace_id,
                                         const SyncVersion& version,
                                         std::string url = "https://archive.test/") {
  TabArchiveEntryRecord entry{
      .snapshot = {.workspace_id = workspace_id,
                   .pages = {{.tree_node_id =
                                  Id("10000000-0000-4000-8000-00000000006f"),
                              .sort_key = "a",
                              .title = "Archived",
                              .target = {.kind = SharedTabTargetKind::kWeb,
                                         .url = std::move(url)}}}},
      .reason = SharedArchiveReason::kManual,
      .archived_at = Ts(3),
      .version = version};
  entry.id = ArchiveIdForSnapshot(entry.snapshot);
  return entry;
}

// One valid record of each type the stable-payload and secret-boundary tests
// share.
std::vector<SyncRecord> SampleRecordsOfEveryType() {
  const base::Uuid device_id = Id("10000000-0000-4000-8000-000000000060");
  const base::Uuid workspace_id = Id("10000000-0000-4000-8000-000000000061");
  const base::Uuid session_id = Id("10000000-0000-4000-8000-000000000062");
  const SyncVersion version = Version(kDeviceA, 60);
  std::vector<SyncRecord> records;
  records.emplace_back(DeviceRecord{.id = device_id,
                                    .type = DeviceType::kMacDesktop,
                                    .display_name = "Mac",
                                    .created_at = Ts(1),
                                    .last_seen = Ts(2),
                                    .version = version});
  records.emplace_back(WorkspaceRecord{.id = workspace_id,
                                       .name = "Work",
                                       .sort_key = "a",
                                       .created_at = Ts(1),
                                       .modified_at = Ts(2),
                                       .version = version});
  records.emplace_back(
      TreeNodeRecord{.id = Id("10000000-0000-4000-8000-000000000063"),
                     .workspace_id = workspace_id,
                     .kind = TreeNodeKind::kFolder,
                     .title = "Folder",
                     .sort_key = "a",
                     .created_at = Ts(1),
                     .modified_at = Ts(2),
                     .version = version});
  records.emplace_back(
      HistoryRecord{.id = Id("10000000-0000-4000-8000-000000000064"),
                    .device_id = device_id,
                    .url = "https://history.test",
                    .title = "History",
                    .last_visit = Ts(2),
                    .visit_count = 3,
                    .version = version});
  records.emplace_back(Tab("10000000-0000-4000-8000-000000000065",
                           device_id.AsLowercaseString().c_str(),
                           session_id.AsLowercaseString().c_str(),
                           "https://tab.test", version));
  records.emplace_back(DeviceSessionRecord{.id = session_id,
                                           .device_id = device_id,
                                           .started_at = Ts(1),
                                           .last_seen = Ts(2),
                                           .version = version});
  records.emplace_back(RemoteCommandRecord{
      .id = Id("10000000-0000-4000-8000-000000000066"),
      .source_device_id = device_id,
      .target_device_id = Id("10000000-0000-4000-8000-000000000067"),
      .nonce_base64 = base::Base64Encode(std::string(16, '\0')),
      .issued_at = Ts(1000),
      .expires_at = Ts(1000) + base::Minutes(5),
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
  // Shared tab presence, native bookmark and device capability: the first two
  // carry a top-level URL the secret boundary must police.
  records.emplace_back(Tab("10000000-0000-4000-8000-00000000006f",
                           "10000000-0000-4000-8000-000000000060",
                           "10000000-0000-4000-8000-000000000062",
                           "https://example.test/shared", version));
  records.emplace_back(BookmarkRecord{
      .id = Id("10000000-0000-4000-8000-000000000070"),
      .kind = BookmarkKind::kUrl,
      .root_kind = BookmarkRoot::kBookmarkBar,
      .sort_key = "a",
      .title = "Bookmark",
      .url = "https://example.test/bookmark",
      .created_at = Ts(3),
      .version = version});
  records.emplace_back(DeviceCapabilityRecord{
      .id = CapabilityIdForDevice(Id(kDeviceA)),
      .device_id = Id(kDeviceA),
      .features = {"shared-tabs"},
      .version = version});
  // ADR-0011 shared structure: a split group and an archive entry whose page
  // targets are nested inside the snapshot.
  records.emplace_back(SplitGroupRecord{
      .id = Id("10000000-0000-4000-8000-00000000006c"),
      .workspace_id = workspace_id,
      .topology = {.member_ids = {Id("10000000-0000-4000-8000-00000000006d"),
                                  Id("10000000-0000-4000-8000-00000000006e")}},
      .version = version});
  records.emplace_back(SampleArchiveEntry(workspace_id, version));

  return records;
}

// Guards the two tests below: a new SyncRecord alternative must get a sample,
// or the secret boundary would silently not cover it.
TEST(SyncSerializationTest, SamplesCoverEveryRecordType) {
  std::set<size_t> covered;
  for (const SyncRecord& record : SampleRecordsOfEveryType()) {
    covered.insert(record.index());
  }
  EXPECT_EQ(std::variant_size_v<SyncRecord>, covered.size());
}

TEST(SyncSerializationTest, EveryRecordTypeHasAStablePayload) {
  const std::vector<SyncRecord> records = SampleRecordsOfEveryType();
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

// DoD 14: no synchronized record can carry credentials, local-only
// addresses or a field meant for secrets.
TEST(SyncSecretBoundaryTest, NoRecordCarriesCredentialsOrLocalUrls) {
  constexpr const char* kUnsyncable[] = {
      "https://user:canary-secret@example.test/private",
      "https://:canary-secret@example.test/",
      "file:///Users/someone/Documents/private.txt",
      "chrome://settings/passwords",
      "javascript:alert(document.cookie)",
      "data:text/html,canary-secret",
  };
  for (SyncRecord record : SampleRecordsOfEveryType()) {
    std::string* url = std::visit(
        [](auto& value) -> std::string* {
          if constexpr (requires { value.url; }) {
            return &value.url;
          } else {
            return nullptr;
          }
        },
        record);
    if (!url) {
      continue;
    }
    ASSERT_TRUE(ValidateRecord(record));
    for (const char* bad : kUnsyncable) {
      *url = bad;
      EXPECT_FALSE(ValidateRecord(record))
          << static_cast<int>(GetEntityType(record)) << " accepted " << bad;
      std::string payload;
      EXPECT_FALSE(SerializeRecord(record, &payload))
          << static_cast<int>(GetEntityType(record)) << " serialized " << bad;
    }
  }
}

// DoD 14, ADR-0011 zones: archive page and Home targets are nested in the
// snapshot, so the top-level URL check above does not reach them.
TEST(SyncSecretBoundaryTest, ArchiveTargetsCarryNoCredentialsOrLocalUrls) {
  const base::Uuid workspace_id = Id("10000000-0000-4000-8000-000000000061");
  const SyncVersion version = Version(kDeviceA, 60);
  ASSERT_TRUE(ValidateRecord(SampleArchiveEntry(workspace_id, version)));
  constexpr const char* kUnsyncable[] = {
      "https://user:canary-secret@example.test/private",
      "https://:canary-secret@example.test/",
      "file:///Users/someone/Documents/private.txt",
      "chrome://settings/passwords",
      "javascript:alert(document.cookie)",
      "data:text/html,canary-secret",
  };
  for (const char* bad : kUnsyncable) {
    TabArchiveEntryRecord page = SampleArchiveEntry(workspace_id, version, bad);
    EXPECT_FALSE(ValidateArchiveSnapshot(page.snapshot)) << "page " << bad;
    std::string error;
    EXPECT_FALSE(ValidateRecord(page, &error)) << "page " << bad;
    EXPECT_EQ(error.find("canary-secret"), std::string::npos) << error;
    std::string payload;
    EXPECT_FALSE(SerializeRecord(page, &payload)) << "page " << bad;

    TabArchiveEntryRecord home = SampleArchiveEntry(workspace_id, version);
    home.snapshot.pages[0].home_target =
        SharedTabTarget{.kind = SharedTabTargetKind::kWeb, .url = bad};
    home.id = ArchiveIdForSnapshot(home.snapshot);
    EXPECT_FALSE(ValidateArchiveSnapshot(home.snapshot)) << "home " << bad;
    EXPECT_FALSE(SerializeRecord(home, &payload)) << "home " << bad;
  }
}

TEST(SyncSecretBoundaryTest, NoPayloadHasAFieldForSecrets) {
  constexpr std::string_view kSecretNames[] = {
      "password", "passwd", "cookie", "authorization", "bearer",
      "credential", "secret", "token", "private_key"};
  for (const SyncRecord& record : SampleRecordsOfEveryType()) {
    std::string payload;
    ASSERT_TRUE(SerializeRecord(record, &payload));
    const std::optional<base::Value> parsed = base::JSONReader::Read(
        payload, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
    ASSERT_TRUE(parsed && parsed->is_dict());
    std::vector<const base::Value*> pending = {&*parsed};
    while (!pending.empty()) {
      const base::Value* value = pending.back();
      pending.pop_back();
      if (const base::DictValue* dict = value->GetIfDict()) {
        for (const auto [key, child] : *dict) {
          const std::string lower = base::ToLowerASCII(key);
          for (std::string_view name : kSecretNames) {
            EXPECT_EQ(std::string::npos, lower.find(name))
                << static_cast<int>(GetEntityType(record)) << " field " << key;
          }
          pending.push_back(&child);
        }
      } else if (const base::ListValue* list = value->GetIfList()) {
        for (const base::Value& child : *list) {
          pending.push_back(&child);
        }
      }
    }
  }
}

TEST(SyncMergeTest, ProductRecordsEnforceOptInAndSecretBoundaries) {
  const SyncVersion version = Version(kDeviceA, 61);
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

}  // namespace
}  // namespace ahoi::sync
