// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_journal.h"

#include <unistd.h>

#include <optional>
#include <string>

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

constexpr char kFirstHash[] =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr char kSecondHash[] =
    "2222222222222222222222222222222222222222222222222222222222222222";
constexpr char kTransactionId[] = "10000000-0000-4000-8000-000000000001";
constexpr char kAffectedId[] = "20000000-0000-4000-8000-000000000002";

class ArcImportJournalTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    profile_path_ = temp_dir_.GetPath().AppendASCII("Default");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
  }

  base::FilePath JournalDirectory() const {
    return profile_path_.AppendASCII("Ahoi");
  }

  base::FilePath JournalPath() const {
    return JournalDirectory().AppendASCII("ArcImportJournal.json");
  }

  ArcImportCommitResult Result() const {
    ArcImportCommitResult result;
    result.stats.imported_workspace_count = 2;
    result.stats.imported_page_count = 3;
    result.reconstructed_split_count = 1;
    return result;
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath profile_path_;
};

TEST_F(ArcImportJournalTest, MissingJournalIsAnEmptySuccessfulState) {
  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  EXPECT_EQ(ArcImportStatus::kOk, read.status);
  EXPECT_EQ(ArcImportJournalState::kEmpty, read.state);
  EXPECT_FALSE(read.committed.has_value());
  EXPECT_FALSE(read.prepared.has_value());
}

TEST_F(ArcImportJournalTest, AtomicallyReplacesOwnerOnlyJournal) {
  ASSERT_TRUE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
  ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_EQ(ArcImportJournalState::kCommitted, read.state);
  ASSERT_TRUE(read.committed.has_value());
  EXPECT_EQ(kFirstHash, read.committed->snapshot_hash);

  ASSERT_TRUE(WriteArcImportJournal(profile_path_, kSecondHash, Result()));
  read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_TRUE(read.committed.has_value());
  EXPECT_EQ(kSecondHash, read.committed->snapshot_hash);

  int permissions = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(JournalDirectory(), &permissions));
  EXPECT_EQ(0700, permissions & 0777);
  ASSERT_TRUE(base::GetPosixFilePermissions(JournalPath(), &permissions));
  EXPECT_EQ(0600, permissions & 0777);

  base::FileEnumerator files(JournalDirectory(), false,
                             base::FileEnumerator::FILES);
  EXPECT_EQ(JournalPath(), files.Next());
  EXPECT_TRUE(files.Next().empty());
}

TEST_F(ArcImportJournalTest, ReadsLegacyV2CommittedState) {
  ASSERT_TRUE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
  std::string json;
  ASSERT_TRUE(base::ReadFileToString(JournalPath(), &json));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());
  parsed->GetDict().Set("version", 2);
  ASSERT_TRUE(base::JSONWriter::Write(*parsed, &json));
  ASSERT_TRUE(base::WriteFile(JournalPath(), json));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0600));

  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_EQ(ArcImportJournalState::kCommitted, read.state);
  ASSERT_TRUE(read.committed.has_value());
  EXPECT_EQ(kFirstHash, read.committed->snapshot_hash);
}

TEST_F(ArcImportJournalTest, RejectsSymlinkedDirectory) {
  const base::FilePath external = temp_dir_.GetPath().AppendASCII("external");
  ASSERT_TRUE(base::CreateDirectory(external));
  ASSERT_TRUE(base::CreateSymbolicLink(external, JournalDirectory()));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
  EXPECT_FALSE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
}

TEST_F(ArcImportJournalTest, RejectsSymlinkedJournalLeaf) {
  ASSERT_TRUE(base::CreateDirectory(JournalDirectory()));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalDirectory(), 0700));
  const base::FilePath external =
      temp_dir_.GetPath().AppendASCII("external.json");
  ASSERT_TRUE(base::WriteFile(external, "{}"));
  ASSERT_TRUE(base::CreateSymbolicLink(external, JournalPath()));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
  EXPECT_FALSE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
}

TEST_F(ArcImportJournalTest, RejectsHardLinkedJournalLeaf) {
  ASSERT_TRUE(base::CreateDirectory(JournalDirectory()));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalDirectory(), 0700));
  const base::FilePath external =
      temp_dir_.GetPath().AppendASCII("external.json");
  ASSERT_TRUE(base::WriteFile(external, "{}"));
  ASSERT_TRUE(base::SetPosixFilePermissions(external, 0600));
  ASSERT_EQ(0, link(external.value().c_str(), JournalPath().value().c_str()));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
  EXPECT_FALSE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
}

TEST_F(ArcImportJournalTest, RejectsOverPermissiveJournalState) {
  ASSERT_TRUE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0644));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
  EXPECT_FALSE(WriteArcImportJournal(profile_path_, kSecondHash, Result()));
}

TEST_F(ArcImportJournalTest, RejectsOverPermissiveJournalDirectory) {
  ASSERT_TRUE(base::CreateDirectory(JournalDirectory()));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalDirectory(), 0755));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
  EXPECT_FALSE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
}

TEST_F(ArcImportJournalTest, PreparedRecordIsPrivacyMinimalAndRestorable) {
  ASSERT_TRUE(WriteArcImportJournal(profile_path_, kFirstHash, Result()));
  const ArcImportJournalReadResult committed =
      ReadArcImportJournal(profile_path_);
  ASSERT_TRUE(committed.committed.has_value());
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .expected_native_structure_sha256 = kFirstHash,
      .native_member_ids = {kAffectedId},
      .affected_ids = {kAffectedId},
      .runtime_mutation_planned = true,
      .previous_committed = committed.committed};

  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));
  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportJournalState::kPrepared, read.state);
  ASSERT_TRUE(read.prepared.has_value());
  EXPECT_EQ(prepared.transaction_id, read.prepared->transaction_id);
  EXPECT_EQ(prepared.backup_identifier, read.prepared->backup_identifier);
  EXPECT_EQ(prepared.previous_tree_sha256, read.prepared->previous_tree_sha256);
  EXPECT_EQ(prepared.expected_tree_sha256, read.prepared->expected_tree_sha256);
  EXPECT_EQ(prepared.expected_native_structure_sha256,
            read.prepared->expected_native_structure_sha256);
  EXPECT_EQ(prepared.native_member_ids, read.prepared->native_member_ids);
  EXPECT_EQ(prepared.affected_ids, read.prepared->affected_ids);
  std::string json;
  ASSERT_TRUE(base::ReadFileToString(JournalPath(), &json));
  EXPECT_EQ(std::string::npos, json.find("https://"));
  EXPECT_EQ(std::string::npos, json.find("/Users/"));
  EXPECT_EQ(std::string::npos, json.find("Private title"));

  ASSERT_TRUE(RestoreArcImportJournalAfterRollback(
      profile_path_, read.prepared->previous_committed));
  const ArcImportJournalReadResult restored =
      ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportJournalState::kCommitted, restored.state);
  ASSERT_TRUE(restored.committed.has_value());
  EXPECT_EQ(kFirstHash, restored.committed->snapshot_hash);
}

TEST_F(ArcImportJournalTest, RejectsNonCanonicalAffectedIds) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .affected_ids = {kAffectedId, kAffectedId}};
  EXPECT_FALSE(WriteArcImportPreparedJournal(profile_path_, prepared));
}

TEST_F(ArcImportJournalTest, ManualRecoveryMarkerRoundTripsFailClosed) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .affected_ids = {kAffectedId},
      .phase = ArcImportPreparedPhase::kManualRecoveryRequired};
  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));

  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportJournalState::kPrepared, read.state);
  ASSERT_TRUE(read.prepared.has_value());
  EXPECT_EQ(ArcImportPreparedPhase::kManualRecoveryRequired,
            read.prepared->phase);
}

TEST_F(ArcImportJournalTest, RuntimePersistedReceiptRoundTrips) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .expected_native_structure_sha256 = kFirstHash,
      .native_receipt_sha256 = kSecondHash,
      .native_member_ids = {kAffectedId},
      .affected_ids = {kAffectedId},
      .phase = ArcImportPreparedPhase::kRuntimePersisted,
      .runtime_mutation_planned = true};
  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));

  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportJournalState::kPrepared, read.state);
  ASSERT_TRUE(read.prepared.has_value());
  EXPECT_EQ(ArcImportPreparedPhase::kRuntimePersisted,
            read.prepared->phase);
  EXPECT_EQ(kFirstHash, read.prepared->expected_native_structure_sha256);
  EXPECT_EQ(kSecondHash, read.prepared->native_receipt_sha256);
  EXPECT_EQ(prepared.native_member_ids, read.prepared->native_member_ids);
}

TEST_F(ArcImportJournalTest, FirstImportRollbackDeletesPreparedMarker) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .affected_ids = {kAffectedId}};
  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));

  ASSERT_TRUE(
      RestoreArcImportJournalAfterRollback(profile_path_, std::nullopt));

  EXPECT_EQ(ArcImportJournalState::kEmpty,
            ReadArcImportJournal(profile_path_).state);
  EXPECT_FALSE(base::PathExists(JournalPath()));
}

TEST_F(ArcImportJournalTest, RejectsMissingOrMalformedTreeFingerprints) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = "invalid",
      .expected_tree_sha256 = kSecondHash,
      .affected_ids = {kAffectedId}};
  EXPECT_FALSE(WriteArcImportPreparedJournal(profile_path_, prepared));

  prepared.previous_tree_sha256 = kFirstHash;
  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));
  std::string json;
  ASSERT_TRUE(base::ReadFileToString(JournalPath(), &json));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());
  EXPECT_TRUE(parsed->GetDict().Remove("expected_tree_sha256"));
  ASSERT_TRUE(base::JSONWriter::Write(*parsed, &json));
  ASSERT_TRUE(base::WriteFile(JournalPath(), json));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0600));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
}

TEST_F(ArcImportJournalTest, LegacyV2PreparedStateIsManualAndNeverMutable) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .affected_ids = {kAffectedId}};
  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));
  std::string json;
  ASSERT_TRUE(base::ReadFileToString(JournalPath(), &json));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());
  parsed->GetDict().Set("version", 2);
  EXPECT_TRUE(parsed->GetDict().Remove("previous_tree_sha256"));
  EXPECT_TRUE(parsed->GetDict().Remove("expected_tree_sha256"));
  ASSERT_TRUE(base::JSONWriter::Write(*parsed, &json));
  ASSERT_TRUE(base::WriteFile(JournalPath(), json));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0600));

  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_EQ(ArcImportJournalState::kPrepared, read.state);
  ASSERT_TRUE(read.prepared.has_value());
  EXPECT_EQ(ArcImportPreparedPhase::kManualRecoveryRequired,
            read.prepared->phase);
}

TEST_F(ArcImportJournalTest, LegacyV3PreparedStateIsManualAndNeverMutable) {
  ArcImportPreparedState prepared{
      .transaction_id = kTransactionId,
      .snapshot_hash = kSecondHash,
      .selection_fingerprint = kFirstHash,
      .idempotency_key = kSecondHash,
      .backup_identifier = "222222222222-10000000-0000-4000-8000-000000000001",
      .manifest_sha256 = kFirstHash,
      .previous_tree_sha256 = kFirstHash,
      .expected_tree_sha256 = kSecondHash,
      .affected_ids = {kAffectedId}};
  ASSERT_TRUE(WriteArcImportPreparedJournal(profile_path_, prepared));
  std::string json;
  ASSERT_TRUE(base::ReadFileToString(JournalPath(), &json));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());
  parsed->GetDict().Set("version", 3);
  EXPECT_TRUE(parsed->GetDict().Remove("expected_native_structure_sha256"));
  EXPECT_TRUE(parsed->GetDict().Remove("native_receipt_sha256"));
  EXPECT_TRUE(parsed->GetDict().Remove("native_member_ids"));
  ASSERT_TRUE(base::JSONWriter::Write(*parsed, &json));
  ASSERT_TRUE(base::WriteFile(JournalPath(), json));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0600));

  const ArcImportJournalReadResult read = ReadArcImportJournal(profile_path_);
  ASSERT_EQ(ArcImportStatus::kOk, read.status);
  ASSERT_EQ(ArcImportJournalState::kPrepared, read.state);
  ASSERT_TRUE(read.prepared.has_value());
  EXPECT_EQ(ArcImportPreparedPhase::kManualRecoveryRequired,
            read.prepared->phase);
}

TEST_F(ArcImportJournalTest, RejectsOversizedJournal) {
  ASSERT_TRUE(base::CreateDirectory(JournalDirectory()));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalDirectory(), 0700));
  ASSERT_TRUE(
      base::WriteFile(JournalPath(), std::string(1024 * 1024 + 1, 'x')));
  ASSERT_TRUE(base::SetPosixFilePermissions(JournalPath(), 0600));

  EXPECT_EQ(ArcImportStatus::kJournalError,
            ReadArcImportJournal(profile_path_).status);
}

}  // namespace

}  // namespace ahoi::importer::arc
