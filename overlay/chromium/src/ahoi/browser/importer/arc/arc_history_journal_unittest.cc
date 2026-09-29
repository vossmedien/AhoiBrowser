// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_journal.h"

#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

constexpr char kFirstKey[] =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr char kSecondKey[] =
    "2222222222222222222222222222222222222222222222222222222222222222";
constexpr char kManifest[] =
    "3333333333333333333333333333333333333333333333333333333333333333";
constexpr char kSnapshot[] =
    "4444444444444444444444444444444444444444444444444444444444444444";
constexpr char kBackup[] = "444444444444-10000000-0000-4000-8000-000000000001";

class ArcHistoryJournalTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    profile_path_ = temp_dir_.GetPath().AppendASCII("Default");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
  }

  base::FilePath JournalPath() const {
    return profile_path_.AppendASCII("Ahoi").AppendASCII(
        "ArcHistoryImportJournal.json");
  }

  ArcHistoryCommittedState Committed(const char* key) const {
    return {.history_key = key,
            .metrics = {.added_pages = 12,
                        .created_urls = 10,
                        .deduplicated_pages = 3,
                        .expired_pages = 4,
                        .excluded_pages = 5}};
  }

  ArcHistoryPreparedState Prepared() const {
    return {.history_key = kSecondKey,
            .backup_identifier = kBackup,
            .manifest_sha256 = kManifest,
            .snapshot_sha256 = kSnapshot,
            .previous_committed = Committed(kFirstKey)};
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath profile_path_;
};

TEST_F(ArcHistoryJournalTest, MissingJournalIsAnEmptySuccess) {
  const ArcHistoryJournalReadResult read = ReadArcHistoryJournal(profile_path_);
  EXPECT_EQ(ArcImportStatus::kOk, read.status);
  EXPECT_FALSE(read.committed);
  EXPECT_FALSE(read.prepared);
}

TEST_F(ArcHistoryJournalTest, RoundTripsPreparedAndCommittedOwnerOnly) {
  ASSERT_TRUE(WriteArcHistoryPreparedJournal(profile_path_, Prepared()));
  ArcHistoryJournalReadResult read = ReadArcHistoryJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_TRUE(read.prepared);
  EXPECT_EQ(Prepared(), *read.prepared);
  EXPECT_FALSE(read.committed);

  int mode = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(JournalPath(), &mode));
  EXPECT_EQ(0600, mode);
  ASSERT_TRUE(base::GetPosixFilePermissions(JournalPath().DirName(), &mode));
  EXPECT_EQ(0700, mode);

  ASSERT_TRUE(
      WriteArcHistoryCommittedJournal(profile_path_, Committed(kSecondKey)));
  read = ReadArcHistoryJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_TRUE(read.committed);
  EXPECT_EQ(Committed(kSecondKey), *read.committed);
  EXPECT_FALSE(read.prepared);

  // Only keys, identifiers and counters are stored.
  std::string json;
  ASSERT_TRUE(base::ReadFileToString(JournalPath(), &json));
  EXPECT_EQ(std::string::npos, json.find("http"));
  EXPECT_EQ(std::string::npos, json.find("User Data"));
}

TEST_F(ArcHistoryJournalTest, RestoreReinstatesPreviousOrRemovesJournal) {
  ASSERT_TRUE(WriteArcHistoryPreparedJournal(profile_path_, Prepared()));
  ASSERT_TRUE(RestoreArcHistoryJournal(profile_path_, Committed(kFirstKey)));
  ArcHistoryJournalReadResult read = ReadArcHistoryJournal(profile_path_);
  ASSERT_TRUE(read.committed);
  EXPECT_EQ(Committed(kFirstKey), *read.committed);

  ArcHistoryPreparedState first_import = Prepared();
  first_import.previous_committed.reset();
  ASSERT_TRUE(WriteArcHistoryPreparedJournal(profile_path_, first_import));
  ASSERT_TRUE(RestoreArcHistoryJournal(profile_path_, std::nullopt));
  EXPECT_FALSE(base::PathExists(JournalPath()));
  read = ReadArcHistoryJournal(profile_path_);
  EXPECT_EQ(ArcImportStatus::kOk, read.status);
  EXPECT_FALSE(read.committed);
  EXPECT_FALSE(read.prepared);
}

TEST_F(ArcHistoryJournalTest, FailsClosedForUnsafeOrMalformedJournals) {
  ArcHistoryPreparedState invalid = Prepared();
  invalid.backup_identifier = "../escape";
  EXPECT_FALSE(WriteArcHistoryPreparedJournal(profile_path_, invalid));
  EXPECT_FALSE(WriteArcHistoryCommittedJournal(profile_path_,
                                               {.history_key = "not-a-key"}));

  ASSERT_TRUE(
      WriteArcHistoryCommittedJournal(profile_path_, Committed(kFirstKey)));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0644));
  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcHistoryJournal(profile_path_).status);

  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0600));
  ASSERT_TRUE(base::WriteFile(JournalPath(),
                              R"json({"version":1,"state":"committed"})json"));
  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcHistoryJournal(profile_path_).status);

  ASSERT_TRUE(base::DeleteFile(JournalPath()));
  ASSERT_TRUE(base::CreateSymbolicLink(
      temp_dir_.GetPath().AppendASCII("elsewhere.json"), JournalPath()));
  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcHistoryJournal(profile_path_).status);
  EXPECT_FALSE(
      WriteArcHistoryCommittedJournal(profile_path_, Committed(kFirstKey)));
}

}  // namespace

}  // namespace ahoi::importer::arc
