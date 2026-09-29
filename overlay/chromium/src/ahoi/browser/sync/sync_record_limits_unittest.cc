// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_record_limits.h"

#include <memory>
#include <string>
#include <vector>

#include "ahoi/browser/sync/history_sync_filter.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

// Three UTF-8 bytes each.
constexpr char kWide[] = "\xE8\xAA\x9E";
// Four UTF-8 bytes (U+1F600).
constexpr char kEmoji[] = "\xF0\x9F\x98\x80";
// Two UTF-8 bytes (U+00E9).
constexpr char kAccent[] = "\xC3\xA9";

std::string Repeat(std::string_view unit, size_t count) {
  std::string result;
  for (size_t i = 0; i < count; ++i) {
    result.append(unit);
  }
  return result;
}

// An https address of exactly `bytes` bytes that GURL keeps unchanged.
std::string AddressOfSize(size_t bytes) {
  std::string address = "https://example.test/";
  address.append(bytes - address.size(), 'a');
  return address;
}

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

TEST(SyncRecordLimitsTest, TitleVectorsAroundTheReaderLimit) {
  EXPECT_EQ(kMaxSyncTitleBytes, 1024u);
  for (const size_t size : {size_t{1023}, size_t{1024}}) {
    const std::string title(size, 't');
    EXPECT_EQ(FitSyncText(title, kMaxSyncTitleBytes), title) << size;
  }
  EXPECT_EQ(FitSyncText(std::string(1025, 't'), kMaxSyncTitleBytes),
            std::string(1024, 't'));
  EXPECT_EQ(FitSyncText("", kMaxSyncTitleBytes), "");
}

TEST(SyncRecordLimitsTest, CutsOnlyBetweenWholeCharacters) {
  // 341 three-byte characters are 1023 bytes; a 342nd would straddle 1024.
  const std::string wide = Repeat(kWide, 342);
  const std::string fitted = FitSyncText(wide, kMaxSyncTitleBytes);
  EXPECT_EQ(fitted, Repeat(kWide, 341));
  EXPECT_EQ(fitted.size(), 1023u);
  EXPECT_TRUE(base::IsStringUTF8(fitted));

  // A four-byte character that starts at byte 1022 does not fit at all.
  const std::string emoji = std::string(1022, 'a') + kEmoji;
  EXPECT_EQ(FitSyncText(emoji, kMaxSyncTitleBytes), std::string(1022, 'a'));
  // One that ends exactly at the limit stays whole.
  const std::string exact = std::string(1020, 'a') + kEmoji;
  EXPECT_EQ(FitSyncText(exact, kMaxSyncTitleBytes), exact);
}

TEST(SyncRecordLimitsTest, NamesFitTheirOwnLimit) {
  EXPECT_EQ(kMaxSyncDeviceNameBytes, 256u);
  EXPECT_EQ(kMaxSyncWorkspaceNameBytes, 256u);
  EXPECT_EQ(FitSyncText(std::string(257, 'n'), kMaxSyncDeviceNameBytes),
            std::string(256, 'n'));
  // 255 bytes plus a two-byte character is 257: the character is dropped.
  EXPECT_EQ(FitSyncText(std::string(255, 'n') + kAccent,
                        kMaxSyncWorkspaceNameBytes),
            std::string(255, 'n'));

  DeviceRecord device{.display_name = std::string(257, 'd')};
  FitDeviceRecordForSync(&device);
  EXPECT_EQ(device.display_name, std::string(256, 'd'));

  WorkspaceRecord workspace{.name = std::string(256, 'w')};
  FitWorkspaceRecordForSync(&workspace);
  EXPECT_EQ(workspace.name, std::string(256, 'w'));
}

TEST(SyncRecordLimitsTest, HistoryAddressesPastTheLimitStayLocal) {
  EXPECT_EQ(kMaxSyncHistoryUrlBytes, 16384u);
  const std::string fits = AddressOfSize(16384);
  const std::string too_long = AddressOfSize(16385);
  ASSERT_EQ(GURL(fits).spec(), fits);
  ASSERT_EQ(GURL(too_long).spec(), too_long);
  EXPECT_TRUE(FitsSyncHistoryUrl(fits));
  EXPECT_FALSE(FitsSyncHistoryUrl(too_long));
  EXPECT_TRUE(ShouldSyncHistoryVisit({.url = GURL(fits)}));
  EXPECT_FALSE(ShouldSyncHistoryVisit({.url = GURL(too_long)}));

  HistoryRecord kept{.url = too_long,
                     .title = std::string(2000, 't'),
                     .transition = "link"};
  EXPECT_FALSE(FitHistoryRecordForSync(&kept));
  EXPECT_EQ(kept.title.size(), 2000u);  // Untouched: it is never written.

  HistoryRecord fitted{.url = fits,
                       .title = std::string(2000, 't'),
                       .transition = std::string(129, 'x')};
  EXPECT_TRUE(FitHistoryRecordForSync(&fitted));
  EXPECT_EQ(fitted.url, fits);
  EXPECT_EQ(fitted.title, std::string(1024, 't'));
  EXPECT_EQ(fitted.transition, std::string(128, 'x'));
}

TEST(SyncRecordLimitsTest, PresenceTitleIsFittedAndAddressKept) {
  const std::string address = "https://example.test/" + std::string(20000, 'p');
  RemoteTabRecord tab{.url = address, .title = Repeat(kWide, 700)};
  FitRemoteTabRecordForSync(&tab);
  EXPECT_EQ(tab.title, Repeat(kWide, 341));
  EXPECT_EQ(tab.url, address);  // Tab addresses have their own 128 KiB rule.
}

TEST(SyncRecordLimitsTest, WorkspaceProjectionFitsTheName) {
  tab_tree::Workspace workspace;
  workspace.id = Id("a6000000-0000-4000-8000-000000000001");
  workspace.name = std::u16string(300, u'n');
  workspace.sort_key = "m";
  const WorkspaceRecord record = WorkspaceToSyncRecord(workspace, {});
  EXPECT_EQ(record.name, std::string(256, 'n'));
  // The native Workspace keeps its whole name.
  EXPECT_EQ(workspace.name.size(), 300u);
}

TEST(SyncRecordLimitsTest, BackendWritesOnlyRecordsEveryReaderAccepts) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("limits.sqlite");
  const std::string fits = AddressOfSize(16384);
  {
    ProfileSyncBackend backend(
        path, Id("a6000000-0000-4000-8000-000000000002"),
        Id("a6000000-0000-4000-8000-000000000003"), std::string(300, 'm'),
        /*transport_enabled=*/false, /*history_retention_days=*/90,
        /*bookmark_sync_enabled=*/false,
        /*profile_authorization=*/base::BindRepeating([] { return true; }));
    ASSERT_TRUE(backend.Initialize().has_value());
    const base::Time now = base::Time::Now();
    ASSERT_TRUE(backend
                    .AddHistoryVisit(fits, std::string(2000, 't'), now, "link",
                                     /*visit_id=*/1)
                    .has_value());
    // Stays local: no record, but the call itself succeeds.
    ASSERT_TRUE(backend
                    .AddHistoryVisit(AddressOfSize(16385), "long", now, "link",
                                     /*visit_id=*/2)
                    .has_value());
  }
  SyncStore store;
  ASSERT_TRUE(store.Initialize(path));
  std::vector<SyncRecord> history;
  ASSERT_EQ(store.GetRecords(EntityType::kHistoryEntry, &history),
            SyncStore::Result::kOk);
  ASSERT_EQ(history.size(), 1u);
  const auto& visit = std::get<HistoryRecord>(history[0]);
  EXPECT_EQ(visit.url, fits);
  EXPECT_EQ(visit.title, std::string(1024, 't'));

  std::vector<SyncRecord> devices;
  ASSERT_EQ(store.GetRecords(EntityType::kDevice, &devices),
            SyncStore::Result::kOk);
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_EQ(std::get<DeviceRecord>(devices[0]).display_name,
            std::string(256, 'm'));
}

}  // namespace
}  // namespace ahoi::sync
