// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/sync/sync_serialization.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

TEST(BookmarkSyncGoldenTest, SharedPayloadsRoundTripWithoutPlatformChanges) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string fixture;
  ASSERT_TRUE(base::ReadFileToString(
      root.AppendASCII("ahoi/browser/sync/testdata/bookmark_wire_v2.json"),
      &fixture));
  const auto document =
      base::JSONReader::ReadDict(fixture, base::JSON_PARSE_RFC);
  ASSERT_TRUE(document);
  ASSERT_EQ(11, document->FindInt("entity_type"));
  const auto* cases = document->FindList("cases");
  ASSERT_TRUE(cases);
  ASSERT_EQ(3u, cases->size());
  for (const auto& item : *cases) {
    ASSERT_TRUE(item.is_dict());
    const auto* name = item.GetDict().FindString("name");
    ASSERT_TRUE(name);
    SCOPED_TRACE(*name);
    const auto* payload = item.GetDict().FindDict("payload");
    ASSERT_TRUE(payload);
    std::string encoded;
    ASSERT_TRUE(
        base::JSONWriter::Write(base::Value(payload->Clone()), &encoded));
    SyncRecord decoded;
    ASSERT_TRUE(DeserializeRecord(EntityType::kBookmark, encoded, &decoded));
    ASSERT_TRUE(std::holds_alternative<BookmarkRecord>(decoded));
    std::string reencoded;
    ASSERT_TRUE(SerializeRecord(decoded, &reencoded));
    const auto readback =
        base::JSONReader::ReadDict(reencoded, base::JSON_PARSE_RFC);
    ASSERT_TRUE(readback);
    EXPECT_EQ(*payload, *readback);
  }
}

constexpr std::array<const char*, 6> kBookmarkFields = {
    "location", "kind", "title", "url", "created_at", "tombstone"};
constexpr char kParentId[] = "90000000-0000-4000-8000-000000000002";
constexpr char kDeviceAId[] = "90000000-0000-4000-8000-0000000000a0";
constexpr char kDeviceBId[] = "90000000-0000-4000-8000-0000000000b0";
constexpr int64_t kCreatedAt = 11644473600000123LL;
constexpr int64_t kVersionPhysical = 11644473601000100LL;

BookmarkRecord MakeBookmark() {
  BookmarkRecord record{
      .id = base::Uuid::ParseLowercase("90000000-0000-4000-8000-000000000001"),
      .kind = BookmarkKind::kUrl,
      .root_kind = BookmarkRoot::kBookmarkBar,
      .sort_key = "a",
      .title = "Überblick — 海 ⚓",
      .url = "https://example.test/%C3%BCberblick?q=%E6%B5%B7&empty=#section",
      .created_at = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds(kCreatedAt)),
      .version = {.stamp = {.physical_time_us = kVersionPhysical,
                            .logical = 8,
                            .device_tiebreak = kDeviceAId}}};
  for (size_t i = 0; i < kBookmarkFields.size(); ++i) {
    record.field_versions.emplace(
        kBookmarkFields[i],
        HlcStamp{
            .physical_time_us = kVersionPhysical - 10 + static_cast<int64_t>(i),
            .logical = static_cast<uint32_t>(i),
            .device_tiebreak = i % 2 == 0 ? kDeviceAId : kDeviceBId});
  }
  return record;
}

class BookmarkSyncSerializationTest : public testing::Test {
 public:
  void SetUp() override {
    std::string payload;
    ASSERT_TRUE(SerializeRecord(MakeBookmark(), &payload));
    auto parsed = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
    ASSERT_TRUE(parsed);
    wire_ = std::move(*parsed);
  }

 protected:
  void ExpectRejected(const base::DictValue& dict) {
    std::string payload;
    ASSERT_TRUE(base::JSONWriter::Write(base::Value(dict.Clone()), &payload));
    SyncRecord decoded = MakeBookmark();
    const SyncRecord unchanged = decoded;
    EXPECT_FALSE(DeserializeRecord(EntityType::kBookmark, payload, &decoded));
    EXPECT_EQ(unchanged, decoded);
  }

  void ExpectRejectedValue(std::string_view key, base::Value value) {
    SCOPED_TRACE(key);
    auto invalid = wire_.Clone();
    invalid.Set(key, std::move(value));
    ExpectRejected(invalid);
  }

  base::DictValue wire_;
};

TEST_F(BookmarkSyncSerializationTest,
       PreservesPositiveCreationMetadataBeforeUnixEpoch) {
  for (int64_t micros :
       {int64_t{1}, int64_t{11644473599999999}, int64_t{11644473600000000},
        std::numeric_limits<int64_t>::max() - 1}) {
    SCOPED_TRACE(micros);
    auto original = MakeBookmark();
    original.created_at =
        base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
    std::string payload;
    ASSERT_TRUE(SerializeRecord(original, &payload));
    const auto encoded =
        base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
    ASSERT_TRUE(encoded);
    ASSERT_TRUE(encoded->FindString("created_at"));
    EXPECT_EQ(base::NumberToString(micros), *encoded->FindString("created_at"));
    SyncRecord decoded;
    ASSERT_TRUE(DeserializeRecord(EntityType::kBookmark, payload, &decoded));
    EXPECT_EQ(micros, std::get<BookmarkRecord>(decoded)
                          .created_at.ToDeltaSinceWindowsEpoch()
                          .InMicroseconds());
  }
}

TEST_F(BookmarkSyncSerializationTest,
       RootAndNestedRecordsRoundTripWithoutRedundantRoots) {
  for (BookmarkRoot root : {BookmarkRoot::kBookmarkBar, BookmarkRoot::kOther,
                            BookmarkRoot::kMobile}) {
    for (BookmarkKind kind : {BookmarkKind::kFolder, BookmarkKind::kUrl}) {
      for (bool top_level : {true, false}) {
        SCOPED_TRACE(static_cast<int>(root));
        SCOPED_TRACE(static_cast<int>(kind));
        SCOPED_TRACE(top_level);
        BookmarkRecord original = MakeBookmark();
        original.kind = kind;
        if (kind == BookmarkKind::kFolder) {
          original.url.clear();
        }
        if (top_level) {
          original.root_kind = root;
        } else {
          original.root_kind.reset();
          original.parent_id = base::Uuid::ParseLowercase(kParentId);
        }

        std::string payload;
        ASSERT_TRUE(SerializeRecord(original, &payload));
        auto parsed = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(top_level, parsed->Find("root_kind") != nullptr);
        EXPECT_EQ(!top_level, parsed->Find("parent_id") != nullptr);
        EXPECT_EQ(static_cast<int>(kind), parsed->FindInt("kind").value_or(-1));
        ASSERT_TRUE(parsed->FindString("created_at"));
        EXPECT_EQ("11644473600000123", *parsed->FindString("created_at"));

        SyncRecord decoded;
        ASSERT_TRUE(
            DeserializeRecord(EntityType::kBookmark, payload, &decoded));
        ASSERT_TRUE(std::holds_alternative<BookmarkRecord>(decoded));
        EXPECT_EQ(original, std::get<BookmarkRecord>(decoded));
      }
    }
  }
}

TEST_F(BookmarkSyncSerializationTest,
       NormalizesExactlyTheSixWireV2FieldClocks) {
  BookmarkRecord original = MakeBookmark();
  original.field_versions.clear();
  std::string payload;
  ASSERT_TRUE(SerializeRecord(original, &payload));
  SyncRecord decoded;
  ASSERT_TRUE(DeserializeRecord(EntityType::kBookmark, payload, &decoded));
  const auto& clocks = std::get<BookmarkRecord>(decoded).field_versions;
  ASSERT_EQ(kBookmarkFields.size(), clocks.size());
  for (const char* field : kBookmarkFields) {
    SCOPED_TRACE(field);
    const auto found = clocks.find(field);
    ASSERT_NE(clocks.end(), found);
    EXPECT_EQ(original.version.stamp, found->second);
  }
}

TEST_F(BookmarkSyncSerializationTest,
       TombstoneEnvelopeRetainsBookmarkIdentity) {
  BookmarkRecord original = MakeBookmark();
  original.tombstone = true;
  std::string payload;
  ASSERT_TRUE(SerializeRecord(original, &payload));
  SyncChange change{.mutation_id = "bookmark-delete",
                    .entity_type = EntityType::kBookmark,
                    .entity_id = original.id,
                    .kind = ChangeKind::kDelete,
                    .version = original.version,
                    .payload = payload};
  SyncRecord decoded;
  ASSERT_TRUE(ValidateChangeEnvelope(change, &decoded));
  EXPECT_EQ(original, std::get<BookmarkRecord>(decoded));
  change.kind = ChangeKind::kUpsert;
  EXPECT_FALSE(ValidateChangeEnvelope(change, &decoded));
  change.kind = ChangeKind::kDelete;
  change.entity_type = EntityType::kTreeNode;
  EXPECT_FALSE(ValidateChangeEnvelope(change, &decoded));
}

TEST_F(BookmarkSyncSerializationTest, RejectsMissingOrConflictingLocation) {
  auto missing = wire_.Clone();
  missing.Remove("root_kind");
  ExpectRejected(missing);
  auto conflicting = wire_.Clone();
  conflicting.Set("parent_id", kParentId);
  ExpectRejected(conflicting);

  for (const char* malformed :
       {"null", "false", "0", "[]", "{}", "\"\"", "\"not-a-uuid\""}) {
    SCOPED_TRACE(malformed);
    auto invalid = wire_.Clone();
    invalid.Remove("root_kind");
    auto value = base::JSONReader::Read(malformed, base::JSON_PARSE_RFC);
    ASSERT_TRUE(value);
    invalid.Set("parent_id", std::move(*value));
    ExpectRejected(invalid);
  }
}

TEST_F(BookmarkSyncSerializationTest, RejectsInvalidEnumTypesAndValues) {
  for (const char* key : {"kind", "root_kind"}) {
    for (const char* malformed :
         {"null", "true", "-1", "3", "0.0", "\"0\"", "[]", "{}"}) {
      SCOPED_TRACE(malformed);
      auto value = base::JSONReader::Read(malformed, base::JSON_PARSE_RFC);
      ASSERT_TRUE(value);
      ExpectRejectedValue(key, std::move(*value));
    }
  }
  ExpectRejectedValue("kind", base::Value(2));
}

TEST_F(BookmarkSyncSerializationTest, RejectsMissingAndMistypedRequiredFields) {
  for (const char* key :
       {"kind", "sort_key", "title", "url", "created_at", "model_version", "id",
        "tombstone", "version_model", "version_physical", "version_logical",
        "version_device"}) {
    SCOPED_TRACE(key);
    auto missing = wire_.Clone();
    missing.Remove(key);
    ExpectRejected(missing);
    ExpectRejectedValue(key, base::Value());
  }
  for (const char* key : {"sort_key", "title", "url", "created_at", "id",
                          "version_physical", "version_device"}) {
    ExpectRejectedValue(key, base::Value(42));
  }
  for (const char* key :
       {"model_version", "version_model", "version_logical", "tombstone"}) {
    ExpectRejectedValue(key, base::Value("1"));
  }
  ExpectRejectedValue("created_at", base::Value("1e16"));
  ExpectRejectedValue("created_at", base::Value("9223372036854775808"));
  ExpectRejectedValue("id", base::Value("not-a-uuid"));
  ExpectRejectedValue("version_logical", base::Value(-1));
}

TEST_F(BookmarkSyncSerializationTest, RejectsLegacyOrMismatchedModelVersions) {
  for (int model : {0, 1, 3}) {
    SCOPED_TRACE(model);
    auto invalid = wire_.Clone();
    invalid.Set("model_version", model);
    invalid.Set("version_model", model);
    ExpectRejected(invalid);
  }
  ExpectRejectedValue("version_model", base::Value(1));
}

TEST_F(BookmarkSyncSerializationTest, RejectsIncompleteOrUnknownFieldClocks) {
  for (const char* field : kBookmarkFields) {
    SCOPED_TRACE(field);
    auto missing = wire_.Clone();
    missing.FindDict("field_versions")->Remove(field);
    ExpectRejected(missing);
  }
  auto absent = wire_.Clone();
  absent.Remove("field_versions");
  ExpectRejected(absent);
  ExpectRejectedValue("field_versions", base::Value(base::DictValue()));
  ExpectRejectedValue("field_versions", base::Value("invalid"));

  // Location has one clock; independent root/parent/order clocks are unknown.
  for (const char* field : {"root_kind", "parent_id", "sort_key", "unknown"}) {
    SCOPED_TRACE(field);
    auto invalid = wire_.Clone();
    auto* fields = invalid.FindDict("field_versions");
    fields->Set(field, fields->FindDict("location")->Clone());
    ExpectRejected(invalid);
  }
}

TEST_F(BookmarkSyncSerializationTest, RejectsMalformedFieldClockValues) {
  for (const char* key : {"physical", "logical", "device"}) {
    SCOPED_TRACE(key);
    auto invalid = wire_.Clone();
    invalid.FindDict("field_versions")->FindDict("location")->Remove(key);
    ExpectRejected(invalid);
  }
  for (
      const char* malformed :
      {"null", "[]", "0",
       R"({"physical":123,"logical":0,"device":"90000000-0000-4000-8000-0000000000a0"})",
       R"({"physical":"-1","logical":0,"device":"90000000-0000-4000-8000-0000000000a0"})",
       R"({"physical":"123","logical":-1,"device":"90000000-0000-4000-8000-0000000000a0"})",
       R"({"physical":"123","logical":"0","device":"90000000-0000-4000-8000-0000000000a0"})",
       R"({"physical":"123","logical":0,"device":""})"}) {
    SCOPED_TRACE(malformed);
    auto value = base::JSONReader::Read(malformed, base::JSON_PARSE_RFC);
    ASSERT_TRUE(value);
    auto invalid = wire_.Clone();
    invalid.FindDict("field_versions")->Set("location", std::move(*value));
    ExpectRejected(invalid);
  }
}

TEST_F(BookmarkSyncSerializationTest, RefusesToSerializeInvalidBookmarkShape) {
  std::string payload = "unchanged";
  BookmarkRecord invalid = MakeBookmark();
  invalid.root_kind.reset();
  EXPECT_FALSE(SerializeRecord(invalid, &payload));
  invalid.root_kind = BookmarkRoot::kBookmarkBar;
  invalid.parent_id = base::Uuid::ParseLowercase(kParentId);
  EXPECT_FALSE(SerializeRecord(invalid, &payload));
  invalid.root_kind.reset();
  invalid.parent_id = base::Uuid();
  EXPECT_FALSE(SerializeRecord(invalid, &payload));
  invalid = MakeBookmark();
  invalid.kind = static_cast<BookmarkKind>(2);
  EXPECT_FALSE(SerializeRecord(invalid, &payload));
  invalid = MakeBookmark();
  invalid.root_kind = static_cast<BookmarkRoot>(3);
  EXPECT_FALSE(SerializeRecord(invalid, &payload));
  EXPECT_EQ("unchanged", payload);
}

}  // namespace
}  // namespace ahoi::sync
