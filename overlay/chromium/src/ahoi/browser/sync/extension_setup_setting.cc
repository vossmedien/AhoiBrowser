// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/extension_setup_setting.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/browser_settings_sync_types.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace ahoi::sync {
namespace {
constexpr std::string_view kPrefix = "ahoi.extension.";
constexpr std::string_view kSuffix = ".desired";
}  // namespace

std::string ExtensionSetupSettingId(std::string_view extension_id) {
  return std::string(kPrefix) + std::string(extension_id) +
         std::string(kSuffix);
}

bool IsExtensionSetupSettingId(std::string_view setting_id) {
  return setting_id.starts_with(kPrefix) && setting_id.ends_with(kSuffix) &&
         setting_id.size() == kPrefix.size() + 32 + kSuffix.size() &&
         std::ranges::all_of(setting_id.substr(kPrefix.size(), 32),
                             [](char c) { return c >= 'a' && c <= 'p'; });
}

std::optional<ExtensionDesiredConfiguration> DecodeExtensionSetupSetting(
    const PermittedSettingRecord& record) {
  if (record.tombstone ||
      record.id != BrowserSettingRecordId(record.setting_id) ||
      !IsExtensionSetupSettingId(record.setting_id)) {
    return std::nullopt;
  }
  auto value = base::JSONReader::Read(record.value_json, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict() || value->GetDict().size() != 3) {
    return std::nullopt;
  }
  const auto& dict = value->GetDict();
  const auto* source = dict.FindString("source");
  auto installed = dict.FindBool("installed");
  auto enabled = dict.FindBool("enabled");
  if (!source || !installed || !enabled ||
      (*source != "chromeWebStore" && *source != "pinnedUblockClassic")) {
    return std::nullopt;
  }
  ExtensionDesiredConfiguration result{
      .extension_id = record.setting_id.substr(kPrefix.size(), 32),
      .source = *source == "chromeWebStore"
                    ? ExtensionInstallSource::kChromeWebStore
                    : ExtensionInstallSource::kPinnedUblockClassic,
      .installed = *installed,
      .enabled = *enabled};
  return IsValidExtensionDesiredConfiguration(result)
             ? std::make_optional(std::move(result))
             : std::nullopt;
}

std::optional<PermittedSettingRecord> EncodeExtensionSetupSetting(
    const ExtensionDesiredConfiguration& desired,
    SyncVersion version) {
  if (!IsValidExtensionDesiredConfiguration(desired)) {
    return std::nullopt;
  }
  const auto id = ExtensionSetupSettingId(desired.extension_id);
  base::DictValue value;
  value.Set("source", desired.source == ExtensionInstallSource::kChromeWebStore
                          ? "chromeWebStore"
                          : "pinnedUblockClassic");
  value.Set("installed", desired.installed);
  value.Set("enabled", desired.enabled);
  auto json = base::WriteJson(value);
  if (!json) {
    return std::nullopt;
  }
  return PermittedSettingRecord{.id = BrowserSettingRecordId(id),
                                .setting_id = id,
                                .value_json = std::move(*json),
                                .version = std::move(version)};
}

}  // namespace ahoi::sync
