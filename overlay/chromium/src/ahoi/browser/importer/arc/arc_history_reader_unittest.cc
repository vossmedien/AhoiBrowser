// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_reader.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_history_unittest_support.h"
#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_recovery.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

using test_support::SyntheticHistoryRow;
using test_support::WriteSyntheticHistory;

class ArcHistoryReaderTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    now_ = base::Time::Now();
    window_ = {.not_before = now_ - base::Days(90),
               .not_after = now_ + base::Days(1)};
  }

  base::FilePath CreateDatabase(
      const std::string& name,
      const std::vector<SyntheticHistoryRow>& rows,
      int version = test_support::kSyntheticHistoryVersion) {
    const base::FilePath path = temp_dir_.GetPath().AppendASCII(name);
    sql::Database database(sql::test::kTestTag);
    EXPECT_TRUE(database.Open(path));
    EXPECT_TRUE(WriteSyntheticHistory(database, rows, version));
    return path;
  }

  base::Time DaysAgo(int days) const { return now_ - base::Days(days); }

  base::ScopedTempDir temp_dir_;
  base::Time now_;
  ArcHistoryReadWindow window_;
};

TEST_F(ArcHistoryReaderTest, ReadsVisibleSafePagesNewestFirst) {
  const base::FilePath path = CreateDatabase(
      "History",
      {{.url = "http://older.example/",
        .title = "Older",
        .visit_count = 3,
        .last_visit = DaysAgo(2)},
       {.url = "https://newer.example/page",
        .title = "Newer",
        .visit_count = 7,
        .typed_count = 2,
        .last_visit = DaysAgo(1)},
       {.url = "https://hidden.example/",
        .last_visit = DaysAgo(1),
        .hidden = true},
       {.url = "arc://internal", .last_visit = DaysAgo(1)},
       {.url = "chrome-extension://abcdefghijklmnop/page.html",
        .last_visit = DaysAgo(1)},
       {.url = "ftp://files.example/", .last_visit = DaysAgo(1)},
       {.url = "https://user:secret@login.example/", .last_visit = DaysAgo(1)},
       {.url = "https://long.example/" + std::string(kMaxUrlBytes, 'a'),
        .last_visit = DaysAgo(1)},
       {.url = "https://expired.example/", .last_visit = DaysAgo(120)},
       {.url = "https://future.example/", .last_visit = now_ + base::Days(9)},
       {.url = "https://zero.example/", .last_visit = base::Time()}});

  const ArcHistoryReadResult result = ReadArcHistoryDatabase(path, window_);
  ASSERT_EQ(ArcImportStatus::kOk, result.status);
  ASSERT_EQ(2u, result.entries.size());
  EXPECT_EQ(GURL("https://newer.example/page"), result.entries[0].url);
  EXPECT_EQ(u"Newer", result.entries[0].title);
  EXPECT_EQ(7, result.entries[0].visit_count);
  EXPECT_EQ(2, result.entries[0].typed_count);
  EXPECT_EQ(DaysAgo(1), result.entries[0].last_visit);
  EXPECT_EQ(GURL("http://older.example/"), result.entries[1].url);
  EXPECT_EQ(DaysAgo(2), result.entries[1].last_visit);

  EXPECT_EQ(11u, result.stats.source_rows);
  EXPECT_EQ(2u, result.stats.accepted_rows);
  EXPECT_EQ(1u, result.stats.hidden_rows);
  EXPECT_EQ(5u, result.stats.unsafe_urls);
  EXPECT_EQ(1u, result.stats.expired_rows);
  EXPECT_EQ(2u, result.stats.invalid_times);
}

TEST_F(ArcHistoryReaderTest, MergesCanonicalDuplicatesAndClearsBadTitles) {
  const base::FilePath path =
      CreateDatabase("History", {{.url = "https://Example.com/a",
                                  .title = "Old title",
                                  .visit_count = 2,
                                  .last_visit = DaysAgo(3)},
                                 {.url = "https://example.com/a",
                                  .title = "New title",
                                  .visit_count = 5,
                                  .typed_count = 1,
                                  .last_visit = DaysAgo(1)},
                                 {.url = "https://invalid-title.example/",
                                  .title = "\xff\xfe",
                                  .last_visit = DaysAgo(1)},
                                 {.url = "https://long-title.example/",
                                  .title = std::string(kMaxTitleBytes + 1, 't'),
                                  .last_visit = DaysAgo(1)}});

  const ArcHistoryReadResult result = ReadArcHistoryDatabase(path, window_);
  ASSERT_EQ(ArcImportStatus::kOk, result.status);
  ASSERT_EQ(3u, result.entries.size());
  EXPECT_EQ(1u, result.stats.merged_duplicates);
  EXPECT_EQ(2u, result.stats.cleared_titles);
  for (const ArcHistoryEntry& entry : result.entries) {
    if (entry.url == GURL("https://example.com/a")) {
      EXPECT_EQ(u"New title", entry.title);
      EXPECT_EQ(7, entry.visit_count);
      EXPECT_EQ(1, entry.typed_count);
      EXPECT_EQ(DaysAgo(1), entry.last_visit);
    } else {
      EXPECT_TRUE(entry.title.empty());
    }
  }
}

TEST_F(ArcHistoryReaderTest, KeepsNewestRowsUpToTheRowLimit) {
  std::vector<SyntheticHistoryRow> rows;
  for (int index = 0; index < 5; ++index) {
    rows.push_back(
        {.url = "https://page" + base::NumberToString(index) + ".example/",
         .last_visit = DaysAgo(index + 1)});
  }
  const base::FilePath path = CreateDatabase("History", rows);

  const ArcHistoryReadResult result =
      ReadArcHistoryDatabase(path, window_, {.max_rows = 3});
  ASSERT_EQ(ArcImportStatus::kOk, result.status);
  ASSERT_EQ(3u, result.entries.size());
  EXPECT_EQ(GURL("https://page0.example/"), result.entries[0].url);
  EXPECT_EQ(GURL("https://page2.example/"), result.entries[2].url);
  EXPECT_EQ(2u, result.stats.over_limit_rows);

  const ArcHistoryReadResult too_many = ReadArcHistoryDatabase(
      path, window_, {.max_rows = 3, .max_scanned_rows = 4});
  EXPECT_EQ(ArcImportStatus::kLimitExceeded, too_many.status);
  EXPECT_TRUE(too_many.entries.empty());
}

TEST_F(ArcHistoryReaderTest, FailsClosedForUnknownSchemaAndDamagedFiles) {
  const base::FilePath old_schema = CreateDatabase(
      "Old", {{.url = "https://a.example/", .last_visit = DaysAgo(1)}},
      /*version=*/kMinArcHistorySchemaVersion - 1);
  EXPECT_EQ(ArcImportStatus::kUnsupportedSchema,
            ReadArcHistoryDatabase(old_schema, window_).status);

  const base::FilePath new_schema = CreateDatabase(
      "New", {{.url = "https://a.example/", .last_visit = DaysAgo(1)}},
      /*version=*/kMaxArcHistorySchemaVersion + 1);
  EXPECT_EQ(ArcImportStatus::kUnsupportedSchema,
            ReadArcHistoryDatabase(new_schema, window_).status);

  const base::FilePath missing_column =
      temp_dir_.GetPath().AppendASCII("MissingColumn");
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(missing_column));
    ASSERT_TRUE(database.Execute(
        "CREATE TABLE meta(key LONGVARCHAR PRIMARY KEY, value LONGVARCHAR)"));
    ASSERT_TRUE(database.Execute(
        "INSERT INTO meta(key, value) VALUES('version', '70')"));
    ASSERT_TRUE(database.Execute(
        "CREATE TABLE urls(id INTEGER PRIMARY KEY, url LONGVARCHAR)"));
  }
  EXPECT_EQ(ArcImportStatus::kUnsupportedSchema,
            ReadArcHistoryDatabase(missing_column, window_).status);

  const base::FilePath garbage = temp_dir_.GetPath().AppendASCII("Garbage");
  ASSERT_TRUE(base::WriteFile(garbage, std::string(4096, 'x')));
  EXPECT_EQ(ArcImportStatus::kUnsupportedSchema,
            ReadArcHistoryDatabase(garbage, window_).status);

  EXPECT_EQ(
      ArcImportStatus::kInvalidPath,
      ReadArcHistoryDatabase(base::FilePath(FILE_PATH_LITERAL("rel")), window_)
          .status);
}

TEST_F(ArcHistoryReaderTest, MergesProfilesDeterministically) {
  ArcHistoryReadResult first{.status = ArcImportStatus::kOk};
  first.entries = {{.url = GURL("https://shared.example/"),
                    .title = u"First",
                    .visit_count = 2,
                    .last_visit = DaysAgo(4)},
                   {.url = GURL("https://first.example/"),
                    .visit_count = 1,
                    .last_visit = DaysAgo(3)}};
  first.stats.unsafe_urls = 1;
  ArcHistoryReadResult second{.status = ArcImportStatus::kOk};
  second.entries = {{.url = GURL("https://shared.example/"),
                     .title = u"Second",
                     .visit_count = 3,
                     .typed_count = 1,
                     .last_visit = DaysAgo(2)}};
  second.stats.hidden_rows = 2;

  std::vector<ArcHistoryReadResult> results;
  results.push_back(first);
  results.push_back(second);
  const ArcHistoryReadResult merged = MergeArcHistoryResults(results);
  ASSERT_EQ(ArcImportStatus::kOk, merged.status);
  ASSERT_EQ(2u, merged.entries.size());
  EXPECT_EQ(GURL("https://shared.example/"), merged.entries[0].url);
  EXPECT_EQ(u"Second", merged.entries[0].title);
  EXPECT_EQ(5, merged.entries[0].visit_count);
  EXPECT_EQ(1, merged.entries[0].typed_count);
  EXPECT_EQ(DaysAgo(2), merged.entries[0].last_visit);
  EXPECT_EQ(1u, merged.stats.merged_duplicates);
  EXPECT_EQ(1u, merged.stats.unsafe_urls);
  EXPECT_EQ(2u, merged.stats.hidden_rows);

  results.clear();
  results.push_back(first);
  results.push_back(second);
  const ArcHistoryReadResult limited =
      MergeArcHistoryResults(std::move(results), {.max_rows = 1});
  ASSERT_EQ(1u, limited.entries.size());
  EXPECT_EQ(GURL("https://shared.example/"), limited.entries[0].url);
  EXPECT_EQ(1u, limited.stats.over_limit_rows);

  std::vector<ArcHistoryReadResult> failing;
  failing.push_back(first);
  failing.push_back({.status = ArcImportStatus::kUnsupportedSchema});
  const ArcHistoryReadResult failed = MergeArcHistoryResults(failing);
  EXPECT_EQ(ArcImportStatus::kUnsupportedSchema, failed.status);
  EXPECT_TRUE(failed.entries.empty());
}

// Reads through the real backup path: rows committed only to the WAL of a
// still-open synthetic Arc profile reach the reader via the verified backup.
class ArcHistoryBackupReadTest : public ArcHistoryReaderTest {
 protected:
  static constexpr char kSidebar[] = R"json({"version":1})json";

  void SetUp() override {
    ArcHistoryReaderTest::SetUp();
    profile_path_ = temp_dir_.GetPath().AppendASCII("AhoiProfile");
    arc_root_ = temp_dir_.GetPath().AppendASCII("Arc");
    arc_profile_ = arc_root_.AppendASCII("User Data").AppendASCII("Default");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
    ASSERT_TRUE(base::CreateDirectory(arc_profile_));
    ASSERT_TRUE(base::WriteFile(arc_root_.AppendASCII("StorableSidebar.json"),
                                kSidebar));
    tab_tree::TabTreeStore store;
    ASSERT_TRUE(
        store.Initialize(profile_path_.AppendASCII(kTabTreeDatabaseFilename)));
  }

  std::string SnapshotHash() const {
    return base::HexEncodeLower(crypto::hash::Sha256(kSidebar));
  }

  ArcImportBackupResult CreateBackup() {
    const ArcSource source{
        .arc_root = arc_root_,
        .browser_profiles = {{.directory_name = "Default",
                              .path = arc_profile_}},
        .sidebar_file = arc_root_.AppendASCII("StorableSidebar.json")};
    return internal::CreateArcImportBackupForTesting(
        profile_path_, source, SnapshotHash(),
        base::BindRepeating([](const ArcSource&) { return false; }));
  }

  base::FilePath profile_path_;
  base::FilePath arc_root_;
  base::FilePath arc_profile_;
};

TEST_F(ArcHistoryBackupReadTest, ReadsWalOnlyRowsFromVerifiedBackupCopy) {
  const base::FilePath history = arc_profile_.AppendASCII("History");
  sql::Database arc_database(sql::DatabaseOptions().set_wal_mode(true),
                             sql::test::kTestTag);
  ASSERT_TRUE(arc_database.Open(history));
  ASSERT_TRUE(WriteSyntheticHistory(
      arc_database,
      {{.url = "https://wal.example/", .title = "Wal", .last_visit = now_}}));
  // The database stays open, so nothing is checkpointed into `History`.
  ASSERT_TRUE(base::PathExists(
      base::FilePath(history.value() + FILE_PATH_LITERAL("-wal"))));
  const std::optional<int64_t> history_bytes = base::GetFileSize(history);

  const ArcImportBackupResult backup = CreateBackup();
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  base::ScopedTempDir copies;
  ASSERT_TRUE(copies.CreateUniqueTempDir());
  const ArcHistoryBackupCopyResult copy = CopyArcHistoryFromBackup(
      profile_path_, backup.backup_identifier, backup.manifest_sha256,
      SnapshotHash(), copies.GetPath());
  ASSERT_EQ(ArcImportStatus::kOk, copy.status);
  ASSERT_EQ(1u, copy.copies.size());
  EXPECT_EQ(64u, copy.history_key.size());

  const ArcHistoryReadResult result =
      ReadArcHistoryDatabase(copy.copies[0].database, window_);
  ASSERT_EQ(ArcImportStatus::kOk, result.status);
  ASSERT_EQ(1u, result.entries.size());
  EXPECT_EQ(GURL("https://wal.example/"), result.entries[0].url);
  EXPECT_EQ(u"Wal", result.entries[0].title);

  // Arc's files were only read: the source stays byte-for-byte unchanged.
  EXPECT_EQ(history_bytes, base::GetFileSize(history));

  // The same backup yields the same content key; a changed one does not.
  base::ScopedTempDir second_copies;
  ASSERT_TRUE(second_copies.CreateUniqueTempDir());
  EXPECT_EQ(copy.history_key,
            CopyArcHistoryFromBackup(profile_path_, backup.backup_identifier,
                                     backup.manifest_sha256, SnapshotHash(),
                                     second_copies.GetPath())
                .history_key);
  EXPECT_EQ(ArcImportStatus::kBackupError,
            CopyArcHistoryFromBackup(profile_path_, backup.backup_identifier,
                                     std::string(64, '0'), SnapshotHash(),
                                     second_copies.GetPath())
                .status);
}

TEST_F(ArcHistoryBackupReadTest, ProfilesWithoutHistoryYieldNoCopies) {
  const ArcImportBackupResult backup = CreateBackup();
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  base::ScopedTempDir copies;
  ASSERT_TRUE(copies.CreateUniqueTempDir());
  const ArcHistoryBackupCopyResult copy = CopyArcHistoryFromBackup(
      profile_path_, backup.backup_identifier, backup.manifest_sha256,
      SnapshotHash(), copies.GetPath());
  EXPECT_EQ(ArcImportStatus::kOk, copy.status);
  EXPECT_TRUE(copy.copies.empty());
}

}  // namespace

}  // namespace ahoi::importer::arc
