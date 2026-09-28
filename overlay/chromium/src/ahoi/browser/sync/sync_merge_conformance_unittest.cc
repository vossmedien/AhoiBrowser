// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Runs the shared format-3 merge conformance vectors
// (fixtures/sync-conformance/merge_v3.json, copied to testdata/) against
// MergeRecordFields. The Swift Companion runs the same file; both must agree.

#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/base_paths.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

std::string DecisionName(MergeDecision decision) {
  switch (decision) {
    case MergeDecision::kKeepExisting:
      return "keepExisting";
    case MergeDecision::kAcceptIncoming:
      return "acceptIncoming";
    case MergeDecision::kMergeFields:
      return "mergeFields";
    case MergeDecision::kDuplicate:
      return "duplicate";
    case MergeDecision::kInvalid:
      return "invalid";
  }
  return "unknown";
}

bool Decode(EntityType type, const base::DictValue& payload, SyncRecord* record) {
  std::string bytes;
  return base::JSONWriter::Write(base::Value(payload.Clone()), &bytes) &&
         DeserializeRecord(type, bytes, record);
}

// Canonical bytes of a record, so absent and explicit-null optional keys compare
// equal exactly as the wire contract defines them.
std::string Canonical(const SyncRecord& record) {
  std::string bytes;
  EXPECT_TRUE(SerializeRecord(record, &bytes));
  return bytes;
}

// Export only observed product results. Expectations never supply output values.
void ExportResults(const base::FilePath& fixture,
                   const std::string& fixture_bytes,
                   base::ListValue results) {
  base::Environment env;
  const auto directory = env.GetVar("AHOI_SYNC_CONFORMANCE_OUTPUT_DIR");
  if (!directory) {
    return;
  }
  const auto run_id = env.GetVar("AHOI_SYNC_CONFORMANCE_RUN_ID");
  ASSERT_TRUE(run_id && !run_id->empty() && run_id->size() <= 120);
  ASSERT_EQ(std::string::npos, run_id->find_first_not_of(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"));
  const base::FilePath output_dir = base::FilePath::FromUTF8Unsafe(*directory);
  ASSERT_TRUE(output_dir.IsAbsolute() && base::DirectoryExists(output_dir));
  base::DictValue document;
  document.Set("schemaVersion", 1);
  document.Set("kind", "ahoi-sync-merge-output");
  document.Set("implementation", "cpp");
  document.Set("runId", *run_id);
  document.Set("fixtureName", fixture.BaseName().AsUTF8Unsafe());
  document.Set("fixtureSha256",
               base::HexEncodeLower(crypto::hash::Sha256(fixture_bytes)));
  document.Set("complete", true);
  document.Set("cases", std::move(results));
  std::string bytes;
  ASSERT_TRUE(base::JSONWriter::Write(base::Value(std::move(document)), &bytes));
  // No overwrite/reuse of an earlier run, even after a failed or partial write.
  base::File file(output_dir.Append(base::FilePath::FromUTF8Unsafe(
                      "cpp-" + fixture.BaseName().AsUTF8Unsafe())),
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(file.IsValid());
  ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(base::as_byte_span(bytes)));
}

base::DictValue Result(const std::string& name, int type,
                       const std::string& outcome, const std::string& decision) {
  base::DictValue result;
  result.Set("name", name);
  result.Set("entityType", type);
  result.Set("outcome", outcome);
  result.Set("decision", decision);
  result.Set("payload", base::Value());
  result.Set("rejectionStage", base::Value());
  return result;
}

void CheckVectors(const char* filename) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  base::FilePath fixture =
      root.AppendASCII("ahoi/browser/sync/testdata").AppendASCII(filename);
  base::Environment env;
  if (std::string(filename) == "merge_v3.json") {
    if (const auto override_path = env.GetVar("AHOI_SYNC_CONFORMANCE_FIXTURE")) {
      fixture = base::FilePath::FromUTF8Unsafe(*override_path);
      ASSERT_TRUE(fixture.IsAbsolute());
    }
  }
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(fixture, &bytes));
  std::optional<base::DictValue> document =
      base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  ASSERT_TRUE(document);
  ASSERT_EQ(1, document->FindInt("schemaVersion"));
  const base::ListValue* cases = document->FindList("cases");
  ASSERT_TRUE(cases);
  ASSERT_FALSE(cases->empty());

  base::ListValue results;
  for (const base::Value& value : *cases) {
    const base::DictValue& vector = value.GetDict();
    const std::string name = *vector.FindString("name");
    SCOPED_TRACE(name);
    const auto type = static_cast<EntityType>(*vector.FindInt("entityType"));
    const base::DictValue& expect = *vector.FindDict("expect");
    const std::string expected_decision = *expect.FindString("decision");

    SyncRecord existing;
    SyncRecord incoming;
    const bool decoded = Decode(type, *vector.FindDict("existing"), &existing) &&
                         Decode(type, *vector.FindDict("incoming"), &incoming);
    if (vector.FindBool("inputValid").value_or(false)) {
      EXPECT_TRUE(decoded) << "valid wire inputs must reach the merge check";
    }
    if (!decoded) {
      // Rejection at the wire boundary is the "invalid" outcome.
      EXPECT_EQ("invalid", expected_decision) << "payload rejected by decoder";
      auto result = Result(name, static_cast<int>(type), "invalid", "invalid");
      result.Set("rejectionStage", "decode");
      results.Append(std::move(result));
      continue;
    }
    SyncRecord merged;
    std::string error;
    const MergeDecision decision =
        MergeRecordFields(existing, incoming, &merged, &error);
    EXPECT_EQ(expected_decision, DecisionName(decision)) << error;
    auto result = Result(name, static_cast<int>(type),
                         decision == MergeDecision::kInvalid ? "invalid" : "accepted",
                         DecisionName(decision));
    if (decision == MergeDecision::kInvalid) {
      result.Set("rejectionStage", "merge");
      results.Append(std::move(result));
      continue;
    }
    std::string payload;
    ASSERT_TRUE(SerializeRecord(merged, &payload));
    auto actual = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
    ASSERT_TRUE(actual);
    result.Set("payload", std::move(*actual));
    results.Append(std::move(result));
    if (expected_decision == "invalid") {
      continue;  // Export the unexpected accepted payload, never the oracle.
    }
    SyncRecord expected_merged;
    ASSERT_TRUE(Decode(type, *expect.FindDict("merged"), &expected_merged));
    EXPECT_EQ(Canonical(expected_merged), Canonical(merged));
  }
  ExportResults(fixture, bytes, std::move(results));
}

void CheckSequences() {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  base::FilePath fixture = root.AppendASCII("ahoi/browser/sync/testdata")
                               .AppendASCII("merge_sequences_v3.json");
  base::Environment env;
  if (const auto override_path =
          env.GetVar("AHOI_SYNC_CONFORMANCE_SEQUENCE_FIXTURE")) {
    fixture = base::FilePath::FromUTF8Unsafe(*override_path);
    ASSERT_TRUE(fixture.IsAbsolute());
  }
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(fixture, &bytes));
  auto document = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  ASSERT_TRUE(document);
  ASSERT_EQ(1, document->FindInt("schemaVersion"));
  const base::ListValue* cases = document->FindList("cases");
  ASSERT_TRUE(cases && !cases->empty());

  base::ListValue results;
  for (const base::Value& value : *cases) {
    const base::DictValue& sequence = value.GetDict();
    const std::string name = *sequence.FindString("name");
    SCOPED_TRACE(name);
    const auto type = static_cast<EntityType>(*sequence.FindInt("entityType"));
    SyncRecord current;
    ASSERT_TRUE(Decode(type, *sequence.FindDict("initial"), &current));
    const base::ListValue* steps = sequence.FindList("steps");
    ASSERT_TRUE(steps && !steps->empty());
    std::string last_decision;
    for (const base::Value& step_value : *steps) {
      const base::DictValue& step = step_value.GetDict();
      SCOPED_TRACE(*step.FindString("name"));
      ASSERT_TRUE(step.FindBool("inputValid").value_or(false));
      SyncRecord incoming;
      ASSERT_TRUE(Decode(type, *step.FindDict("incoming"), &incoming));
      const base::DictValue& expected = *step.FindDict("expect");
      const std::string expected_decision = *expected.FindString("decision");
      SyncRecord merged;
      std::string error;
      const MergeDecision decision =
          MergeRecordFields(current, incoming, &merged, &error);
      last_decision = DecisionName(decision);
      EXPECT_EQ(expected_decision, last_decision) << error;
      if (decision == MergeDecision::kInvalid) {
        EXPECT_EQ(expected_decision, "invalid");
        continue;  // Rejected input does not replace the actual accepted state.
      }
      if (expected_decision != "invalid") {
        SyncRecord expected_step;
        ASSERT_TRUE(Decode(type, *expected.FindDict("merged"), &expected_step));
        EXPECT_EQ(Canonical(expected_step), Canonical(merged));
      }
      current = std::move(merged);
    }
    const base::DictValue& expected = *sequence.FindDict("expect");
    EXPECT_EQ(last_decision, *expected.FindString("decision"));
    SyncRecord expected_final;
    ASSERT_TRUE(Decode(type, *expected.FindDict("merged"), &expected_final));
    EXPECT_EQ(Canonical(expected_final), Canonical(current));
    std::string payload;
    ASSERT_TRUE(SerializeRecord(current, &payload));
    auto actual = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
    ASSERT_TRUE(actual);
    auto result = Result(name, static_cast<int>(type), "accepted", last_decision);
    result.Set("payload", std::move(*actual));
    results.Append(std::move(result));
  }
  ExportResults(fixture, bytes, std::move(results));
}

TEST(SyncMergeConformanceTest, SharedVectors) {
  CheckVectors("merge_v3.json");
}

TEST(SyncMergeConformanceTest, UTF8SortKeyVectors) {
  CheckVectors("merge_utf8_sort_keys_v3.json");
}

TEST(SyncMergeConformanceTest, InventoryAssetVectors) {
  CheckVectors("merge_inventory_asset_v3.json");
}

TEST(SyncMergeConformanceTest, RemainingEntityVectors) {
  CheckVectors("merge_remaining_entities_v3.json");
}

TEST(SyncMergeConformanceTest, SeededMergeSequences) {
  CheckSequences();
}

// Crest 140: page target, Home, temporary state, accents, archive policy and
// extension setup/storage values, including the new-tab union rejection.
TEST(SyncMergeConformanceTest, DomainGroupVectors) {
  CheckVectors("merge_domain_groups_v3.json");
}

}  // namespace
}  // namespace ahoi::sync
