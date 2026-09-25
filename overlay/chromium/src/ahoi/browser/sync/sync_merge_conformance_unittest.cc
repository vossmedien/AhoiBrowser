// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Runs the shared format-3 merge conformance vectors
// (fixtures/sync-conformance/merge_v3.json, copied to testdata/) against
// MergeRecordFields. The Swift Companion runs the same file; both must agree.

#include <optional>
#include <string>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
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

TEST(SyncMergeConformanceTest, SharedVectors) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(
      root.AppendASCII("ahoi/browser/sync/testdata/merge_v3.json"), &bytes));
  std::optional<base::DictValue> document =
      base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  ASSERT_TRUE(document);
  ASSERT_EQ(1, document->FindInt("schemaVersion"));
  const base::ListValue* cases = document->FindList("cases");
  ASSERT_TRUE(cases);
  ASSERT_FALSE(cases->empty());

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
    if (!decoded) {
      // Rejection at the wire boundary is the "invalid" outcome.
      EXPECT_EQ("invalid", expected_decision) << "payload rejected by decoder";
      continue;
    }
    SyncRecord merged;
    std::string error;
    const MergeDecision decision =
        MergeRecordFields(existing, incoming, &merged, &error);
    EXPECT_EQ(expected_decision, DecisionName(decision)) << error;
    if (decision == MergeDecision::kInvalid ||
        expected_decision == "invalid") {
      continue;
    }
    SyncRecord expected_merged;
    ASSERT_TRUE(Decode(type, *expect.FindDict("merged"), &expected_merged));
    EXPECT_EQ(Canonical(expected_merged), Canonical(merged));
  }
}

}  // namespace
}  // namespace ahoi::sync
