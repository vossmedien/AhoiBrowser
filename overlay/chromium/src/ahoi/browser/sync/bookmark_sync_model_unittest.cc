// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_merge.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

base::Uuid Id(unsigned value) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("91000000-0000-4000-8000-%012x", value));
}

base::Time At(int64_t micros) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
}

SyncVersion Version(const char* device, int64_t physical) {
  return {.stamp = {.physical_time_us = physical, .device_tiebreak = device}};
}

BookmarkRecord Folder(unsigned id) {
  return {.id = Id(id),
          .root_kind = BookmarkRoot::kBookmarkBar,
          .sort_key = "a",
          .title = "Ordner 海",
          .created_at = At(10),
          .version = Version("origin", 100)};
}

BookmarkRecord Page(unsigned id = 1) {
  BookmarkRecord result = Folder(id);
  result.kind = BookmarkKind::kUrl;
  result.title = "Überblick ⚓";
  result.url = "https://example.test/%C3%BCberblick?q=one#two";
  return result;
}

BookmarkRecord StampEdit(const BookmarkRecord& previous,
                         BookmarkRecord edited,
                         const char* device,
                         int64_t physical) {
  const SyncRecord before = previous;
  edited.version = Version(device, physical);
  SyncRecord after = std::move(edited);
  EXPECT_TRUE(StampLocalMutation(&before, &after));
  return std::get<BookmarkRecord>(after);
}

void ExpectInvalid(const BookmarkRecord& record) {
  std::string error;
  EXPECT_FALSE(ValidateRecord(record, &error));
  EXPECT_FALSE(error.empty());
}

TEST(BookmarkSyncModelTest, PreservesNativeUrlSchemesAsMetadata) {
  for (const char* url : {"https://example.test/path", "chrome://bookmarks/",
                          "about:blank", "file:///tmp/guide.html",
                          "javascript:alert(1)", "data:text/plain,hello"}) {
    SCOPED_TRACE(url);
    BookmarkRecord record = Page();
    record.url = url;
    EXPECT_TRUE(ValidateRecord(record));
  }
  BookmarkRecord folder = Folder(1);
  folder.title.clear();
  EXPECT_TRUE(ValidateRecord(folder));
}

TEST(BookmarkSyncModelTest, RejectsInvalidIdentityKindVersionAndLocation) {
  BookmarkRecord invalid = Page();
  invalid.id = base::Uuid();
  ExpectInvalid(invalid);
  for (int kind : {-1, 2}) {
    invalid = Page();
    invalid.kind = static_cast<BookmarkKind>(kind);
    ExpectInvalid(invalid);
  }
  for (int root : {-1, 3}) {
    invalid = Page();
    invalid.root_kind = static_cast<BookmarkRoot>(root);
    ExpectInvalid(invalid);
  }
  invalid = Page();
  invalid.root_kind.reset();
  ExpectInvalid(invalid);
  invalid.parent_id = Id(2);
  EXPECT_TRUE(ValidateRecord(invalid));
  invalid.root_kind = BookmarkRoot::kOther;
  ExpectInvalid(invalid);
  invalid.root_kind.reset();
  invalid.parent_id = base::Uuid();
  ExpectInvalid(invalid);
  invalid.parent_id = invalid.id;
  ExpectInvalid(invalid);

  for (int model : {0, 1, 3}) {
    invalid = Page();
    invalid.model_version = model;
    invalid.version.model_version = model;
    ExpectInvalid(invalid);
  }
  invalid = Page();
  invalid.version.model_version = 1;
  ExpectInvalid(invalid);
  for (int64_t created : {0, -1}) {
    invalid = Page();
    invalid.created_at = At(created);
    ExpectInvalid(invalid);
  }
}

TEST(BookmarkSyncModelTest, EnforcesUtf8ByteLimitsAndVisibleAsciiOrderKeys) {
  BookmarkRecord record = Page();
  record.title.clear();
  for (size_t i = 0; i < 32768; ++i) {
    record.title += "é";
  }
  ASSERT_EQ(65536u, record.title.size());
  EXPECT_TRUE(ValidateRecord(record));
  record.title += "x";
  ExpectInvalid(record);
  for (const std::string& title :
       {std::string("\xC3"), std::string("a\0b", 3)}) {
    record = Page();
    record.title = title;
    ExpectInvalid(record);
  }

  record = Page();
  record.sort_key.assign(1024, '~');
  EXPECT_TRUE(ValidateRecord(record));
  record.sort_key += "!";
  ExpectInvalid(record);
  for (const char* key : {"", " ", "a b", "\t", "\n", "\x7f", "ä"}) {
    record = Page();
    record.sort_key = key;
    ExpectInvalid(record);
  }
  record = Page();
  record.sort_key = "!09AZaz~";
  EXPECT_TRUE(ValidateRecord(record));

  record.url = "https://example.test/";
  record.url.append(131072 - record.url.size(), 'a');
  EXPECT_TRUE(ValidateRecord(record));
  record.url += "a";
  ExpectInvalid(record);
}

TEST(BookmarkSyncModelTest, RejectsCredentialsMalformedUrlsAndFolderUrls) {
  for (const std::string& url :
       {std::string(), std::string("not a url"),
        std::string("https://user:secret@example.test/"),
        std::string("https://user@example.test/"),
        std::string("https://example.test/\xC3"),
        std::string("https://example.test/\0x", 23)}) {
    BookmarkRecord record = Page();
    record.url = url;
    ExpectInvalid(record);
  }
  BookmarkRecord folder = Folder(1);
  folder.url = "https://example.test/";
  ExpectInvalid(folder);
}

TEST(BookmarkSyncModelTest, RejectsUnknownAndFutureFieldClocks) {
  BookmarkRecord invalid = Page();
  invalid.field_versions.emplace("root_kind", invalid.version.stamp);
  ExpectInvalid(invalid);
  invalid = Page();
  invalid.field_versions.emplace("location", Version("future", 101).stamp);
  ExpectInvalid(invalid);
  invalid = Page();
  invalid.field_versions.emplace("title", Version("negative", -1).stamp);
  ExpectInvalid(invalid);
}

TEST(BookmarkSyncModelTest,
     DisjointFieldsAndCorrectedCreatedAtConvergeBothWays) {
  const BookmarkRecord initial = Page();
  BookmarkRecord renamed = initial;
  renamed.title = "Neuer Titel";
  renamed = StampEdit(initial, renamed, "device-a", 200);
  BookmarkRecord corrected = initial;
  corrected.url = "https://example.test/corrected";
  corrected.created_at = At(5);
  corrected = StampEdit(initial, corrected, "device-b", 300);

  SyncRecord forward;
  SyncRecord reverse;
  ASSERT_EQ(MergeDecision::kMergeFields,
            MergeRecordFields(renamed, corrected, &forward));
  ASSERT_EQ(MergeDecision::kMergeFields,
            MergeRecordFields(corrected, renamed, &reverse));
  EXPECT_EQ(forward, reverse);
  const auto& merged = std::get<BookmarkRecord>(forward);
  EXPECT_EQ(renamed.title, merged.title);
  EXPECT_EQ(corrected.url, merged.url);
  EXPECT_EQ(corrected.created_at, merged.created_at);
  EXPECT_EQ(renamed.version.stamp, merged.field_versions.at("title"));
  EXPECT_EQ(corrected.version.stamp, merged.field_versions.at("created_at"));
  EXPECT_EQ(initial.version.stamp, merged.field_versions.at("kind"));
  EXPECT_TRUE(HasCompleteFieldVersions(forward));
  EXPECT_TRUE(ValidateRecord(forward));
}

TEST(BookmarkSyncModelTest, RootParentAndOrderAreOneAtomicLocationBothWays) {
  for (bool nested_wins : {false, true}) {
    SCOPED_TRACE(nested_wins);
    const BookmarkRecord initial = Page();
    BookmarkRecord root = initial;
    root.root_kind = BookmarkRoot::kMobile;
    root.sort_key = "root-order";
    root = StampEdit(initial, root, "root-device", nested_wins ? 200 : 210);
    BookmarkRecord nested = initial;
    nested.root_kind.reset();
    nested.parent_id = Id(2);
    nested.sort_key = "nested-order";
    nested =
        StampEdit(initial, nested, "nested-device", nested_wins ? 210 : 200);
    BookmarkRecord& losing_location = nested_wins ? root : nested;
    BookmarkRecord renamed = losing_location;
    renamed.title = "Latest title on the older location";
    losing_location = StampEdit(losing_location, renamed, "title-device", 240);
    const BookmarkRecord& winning_location = nested_wins ? nested : root;

    SyncRecord forward;
    SyncRecord reverse;
    ASSERT_EQ(MergeDecision::kMergeFields,
              MergeRecordFields(root, nested, &forward));
    ASSERT_EQ(MergeDecision::kMergeFields,
              MergeRecordFields(nested, root, &reverse));
    EXPECT_EQ(forward, reverse);
    const auto& merged = std::get<BookmarkRecord>(forward);
    EXPECT_EQ(winning_location.root_kind, merged.root_kind);
    EXPECT_EQ(winning_location.parent_id, merged.parent_id);
    EXPECT_EQ(winning_location.sort_key, merged.sort_key);
    EXPECT_EQ(losing_location.title, merged.title);
    EXPECT_EQ(winning_location.field_versions.at("location"),
              merged.field_versions.at("location"));
    EXPECT_TRUE(ValidateRecord(forward));
  }
}

TEST(BookmarkSyncModelTest, RejectsKindChangesAndEqualClockDivergence) {
  const BookmarkRecord page = Page();
  BookmarkRecord folder = page;
  folder.kind = BookmarkKind::kFolder;
  folder.url.clear();
  folder.version = Version("folder-device", 200);
  ASSERT_TRUE(ValidateRecord(page));
  ASSERT_TRUE(ValidateRecord(folder));
  SyncRecord merged;
  EXPECT_EQ(MergeDecision::kInvalid, MergeRecordFields(page, folder, &merged));
  EXPECT_EQ(MergeDecision::kInvalid, MergeRecordFields(folder, page, &merged));

  BookmarkRecord conflict = page;
  conflict.root_kind = BookmarkRoot::kOther;
  EXPECT_EQ(MergeDecision::kInvalid,
            MergeRecordFields(page, conflict, &merged));
  EXPECT_EQ(MergeDecision::kInvalid,
            MergeRecordFields(conflict, page, &merged));
}

TEST(BookmarkSyncModelTest, TombstoneSurvivesConcurrentMetadataAndStaleReplay) {
  const BookmarkRecord initial = Page();
  BookmarkRecord renamed = initial;
  renamed.title = "Concurrent rename";
  renamed = StampEdit(initial, renamed, "rename-device", 200);
  BookmarkRecord removed = initial;
  removed.tombstone = true;
  removed = StampEdit(initial, removed, "delete-device", 250);
  SyncRecord forward;
  SyncRecord reverse;
  ASSERT_EQ(MergeDecision::kMergeFields,
            MergeRecordFields(renamed, removed, &forward));
  ASSERT_EQ(MergeDecision::kMergeFields,
            MergeRecordFields(removed, renamed, &reverse));
  EXPECT_EQ(forward, reverse);
  const auto& merged = std::get<BookmarkRecord>(forward);
  EXPECT_TRUE(merged.tombstone);
  EXPECT_EQ(renamed.title, merged.title);
  EXPECT_EQ(removed.version.stamp, merged.field_versions.at("tombstone"));
  SyncRecord replay;
  EXPECT_EQ(MergeDecision::kKeepExisting,
            MergeRecordFields(forward, initial, &replay));
  EXPECT_TRUE(std::get<BookmarkRecord>(replay).tombstone);
}

TEST(BookmarkSyncGraphTest, AcceptsChildBeforeParentAndDeletedFolderAncestry) {
  BookmarkRecord child = Page();
  child.root_kind.reset();
  child.parent_id = Id(2);
  BookmarkRecord parent = Folder(2);
  EXPECT_TRUE(ValidateBookmarkGraph({child}));
  EXPECT_TRUE(ValidateBookmarkGraph({child, parent}));
  EXPECT_TRUE(ValidateBookmarkGraph({parent, child}));
  parent.tombstone = true;
  EXPECT_TRUE(ValidateBookmarkGraph({child, parent}));
}

TEST(BookmarkSyncGraphTest, RejectsKnownUrlParentsDuplicateIdsAndCycles) {
  BookmarkRecord child = Page();
  child.root_kind.reset();
  child.parent_id = Id(2);
  const BookmarkRecord url_parent = Page(2);
  EXPECT_FALSE(ValidateBookmarkGraph({child, url_parent}));
  EXPECT_FALSE(ValidateBookmarkGraph({url_parent, child}));
  EXPECT_FALSE(ValidateBookmarkGraph({child, child}));
  BookmarkRecord duplicate = child;
  duplicate.tombstone = true;
  EXPECT_FALSE(ValidateBookmarkGraph({child, duplicate}));

  BookmarkRecord first = Folder(1);
  first.root_kind.reset();
  first.parent_id = Id(2);
  BookmarkRecord second = Folder(2);
  second.root_kind.reset();
  second.parent_id = Id(1);
  EXPECT_FALSE(ValidateBookmarkGraph({first, second}));
  EXPECT_FALSE(ValidateBookmarkGraph({second, first}));
}

TEST(BookmarkSyncGraphTest, HandlesTenThousandLevelsAndDetectsADeepCycle) {
  constexpr unsigned kDepth = 10000;
  std::vector<BookmarkRecord> records;
  records.reserve(kDepth);
  for (unsigned id = 1; id <= kDepth; ++id) {
    BookmarkRecord record = Folder(id);
    if (id > 1) {
      record.root_kind.reset();
      record.parent_id = Id(id - 1);
    }
    records.push_back(std::move(record));
  }
  std::reverse(records.begin(), records.end());
  EXPECT_TRUE(ValidateBookmarkGraph(records));
  records.back().root_kind.reset();
  records.back().parent_id = records.front().id;
  EXPECT_FALSE(ValidateBookmarkGraph(records));
}

}  // namespace
}  // namespace ahoi::sync
