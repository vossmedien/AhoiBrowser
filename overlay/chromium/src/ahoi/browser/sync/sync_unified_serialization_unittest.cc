// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <set>
#include <string>

#include "ahoi/browser/sync/extension_setup_setting.h"
#include "ahoi/browser/sync/extension_storage_setting.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

class UnifiedSyncGoldenTest : public testing::Test {
 protected:
  void SetUp() override {
    base::FilePath root;
    ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
    std::string bytes;
    ASSERT_TRUE(base::ReadFileToString(
        root.AppendASCII("ahoi/browser/sync/testdata/sync_wire_v3.json"),
        &bytes));
    ASSERT_EQ(
        "18d3a0e5140359ecc6a768681029ec01b09cf4508e11c61552f050795ac7934d",
        base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(bytes))));
    document_ = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
    ASSERT_TRUE(document_);
    ASSERT_EQ(3, document_->FindInt("model_version"));
    ASSERT_TRUE(document_->FindList("records"));
    ASSERT_EQ(30u, cases().size());
    for (const auto& value : cases()) {
      ASSERT_TRUE(value.is_dict());
      ASSERT_TRUE(value.GetDict().FindString("name"));
      ASSERT_TRUE(value.GetDict().FindString("payload"));
      ASSERT_TRUE(value.GetDict().FindInt("entity_type"));
    }
  }

  const base::ListValue& cases() const {
    return *document_->FindList("records");
  }

  bool Decodes(EntityType type, const base::DictValue& payload) {
    std::string bytes;
    EXPECT_TRUE(base::JSONWriter::Write(base::Value(payload.Clone()), &bytes));
    SyncRecord result;
    return DeserializeRecord(type, bytes, &result);
  }

  std::optional<base::DictValue> document_;
};

TEST_F(UnifiedSyncGoldenTest, EveryEntityRoundTripsTheSameCanonicalBytes) {
  std::set<EntityType> seen;
  for (const auto& value : cases()) {
    const auto& item = value.GetDict();
    SCOPED_TRACE(*item.FindString("name"));
    const auto type = static_cast<EntityType>(*item.FindInt("entity_type"));
    const std::string& payload = *item.FindString("payload");
    SyncRecord decoded;
    ASSERT_TRUE(DeserializeRecord(type, payload, &decoded));
    EXPECT_EQ(type, GetEntityType(decoded));
    EXPECT_EQ(3, GetVersion(decoded).model_version);
    EXPECT_TRUE(HasCompleteFieldVersions(decoded));
    if (const auto* setting = std::get_if<PermittedSettingRecord>(&decoded);
        setting && IsExtensionSetupSettingId(setting->setting_id)) {
      const auto desired = DecodeExtensionSetupSetting(*setting);
      ASSERT_TRUE(desired);
      auto typed = EncodeExtensionSetupSetting(*desired, setting->version);
      ASSERT_TRUE(typed);
      typed->field_versions = setting->field_versions;
      EXPECT_EQ(*setting, *typed);
    }
    if (const auto* setting = std::get_if<PermittedSettingRecord>(&decoded);
        setting && IsExtensionStorageSettingId(setting->setting_id)) {
      const auto desired = DecodeExtensionStorageSetting(*setting);
      ASSERT_TRUE(desired);
      auto typed = EncodeExtensionStorageSetting(*desired, setting->version);
      ASSERT_TRUE(typed);
      typed->field_versions = setting->field_versions;
      EXPECT_EQ(*setting, *typed);
    }
    std::string encoded;
    ASSERT_TRUE(SerializeRecord(decoded, &encoded));
    EXPECT_EQ(payload, encoded);
    seen.insert(type);
  }
  EXPECT_EQ(13u, seen.size());
}

TEST_F(UnifiedSyncGoldenTest, OldAndUnknownFormatsAreRejectedForEveryEntity) {
  constexpr std::array<int, 4> kUnsupported = {0, 1, 2, 4};
  for (const auto& value : cases()) {
    const auto& item = value.GetDict();
    SCOPED_TRACE(*item.FindString("name"));
    const auto type = static_cast<EntityType>(*item.FindInt("entity_type"));
    for (int version : kUnsupported) {
      auto payload = base::JSONReader::ReadDict(*item.FindString("payload"),
                                                base::JSON_PARSE_RFC);
      ASSERT_TRUE(payload);
      payload->Set("model_version", version);
      payload->Set("version_model", version);
      EXPECT_FALSE(Decodes(type, *payload));
    }
  }
}

TEST_F(UnifiedSyncGoldenTest,
       IncomingMissingOrUnknownFieldsNeverGetSyntheticClocks) {
  for (const auto& value : cases()) {
    const auto& item = value.GetDict();
    SCOPED_TRACE(*item.FindString("name"));
    const auto type = static_cast<EntityType>(*item.FindInt("entity_type"));
    auto payload = base::JSONReader::ReadDict(*item.FindString("payload"),
                                              base::JSON_PARSE_RFC);
    ASSERT_TRUE(payload);
    auto* fields = payload->FindDict("field_versions");
    ASSERT_TRUE(fields);
    ASSERT_FALSE(fields->empty());
    const std::string first(fields->begin()->first);
    const auto clock = fields->begin()->second.Clone();
    fields->Remove(first);
    EXPECT_FALSE(Decodes(type, *payload));
    fields->Set("unknown_field", clock.Clone());
    EXPECT_FALSE(Decodes(type, *payload));
    payload->Remove("field_versions");
    EXPECT_FALSE(Decodes(type, *payload));
  }
}

TEST_F(UnifiedSyncGoldenTest,
       LogicalNumbersRejectBooleanFractionNegativeAndOverflow) {
  const std::array<base::Value, 4> invalid = {base::Value(true),
                                              base::Value(0.5), base::Value(-1),
                                              base::Value(4294967296.0)};
  for (const auto& value : cases()) {
    const auto& item = value.GetDict();
    SCOPED_TRACE(*item.FindString("name"));
    const auto type = static_cast<EntityType>(*item.FindInt("entity_type"));
    for (const auto& number : invalid) {
      auto payload = base::JSONReader::ReadDict(*item.FindString("payload"),
                                                base::JSON_PARSE_RFC);
      ASSERT_TRUE(payload);
      payload->Set("version_logical", number.Clone());
      EXPECT_FALSE(Decodes(type, *payload));
    }
  }
}

TEST_F(UnifiedSyncGoldenTest,
       PresenceRequiresDistinctPageIdentityForEveryTarget) {
  for (const auto& value : cases()) {
    const auto& item = value.GetDict();
    if (item.FindInt("entity_type") !=
        static_cast<int>(EntityType::kRemoteTab)) {
      continue;
    }
    SCOPED_TRACE(*item.FindString("name"));
    auto payload = base::JSONReader::ReadDict(*item.FindString("payload"),
                                              base::JSON_PARSE_RFC);
    ASSERT_TRUE(payload);
    payload->Set("tree_node_id", *payload->FindString("id"));
    EXPECT_FALSE(Decodes(EntityType::kRemoteTab, *payload));
    payload->Set("tree_node_id", base::Value());
    EXPECT_FALSE(Decodes(EntityType::kRemoteTab, *payload));
    payload->Remove("tree_node_id");
    EXPECT_FALSE(Decodes(EntityType::kRemoteTab, *payload));
  }
}

}  // namespace
}  // namespace ahoi::sync
