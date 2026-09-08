// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/extension_storage_setting.h"

#include <utility>

#include "ahoi/browser/sync/browser_settings_sync_types.h"
#include "base/json/json_reader.h"

namespace ahoi::sync {

std::string ExtensionStorageSettingId(std::string_view extension_id,
                                      std::string_view key) {
  return "ahoi.extension." + std::string(extension_id) + ".storage.sync." +
         std::string(key);
}

bool IsExtensionStorageSettingId(std::string_view setting_id) {
  for (const auto& descriptor : GetExtensionStorageCatalog()) {
    if (setting_id ==
        ExtensionStorageSettingId(descriptor.extension_id, descriptor.key)) {
      return true;
    }
  }
  return false;
}

std::optional<ExtensionStorageValue> DecodeExtensionStorageSetting(
    const PermittedSettingRecord& record) {
  if (record.tombstone ||
      record.id != BrowserSettingRecordId(record.setting_id)) {
    return std::nullopt;
  }
  for (const auto& descriptor : GetExtensionStorageCatalog()) {
    if (record.setting_id !=
        ExtensionStorageSettingId(descriptor.extension_id, descriptor.key)) {
      continue;
    }
    auto value =
        base::JSONReader::Read(record.value_json, base::JSON_PARSE_RFC);
    if (!value || (!value->is_bool() && !value->is_none())) {
      return std::nullopt;
    }
    return ExtensionStorageValue{
        .extension_id = std::string(descriptor.extension_id),
        .key = std::string(descriptor.key),
        .value = value->is_bool() ? std::make_optional(value->GetBool())
                                  : std::nullopt};
  }
  return std::nullopt;
}

std::optional<PermittedSettingRecord> EncodeExtensionStorageSetting(
    const ExtensionStorageValue& desired,
    SyncVersion version) {
  if (!IsValidExtensionStorageValue(desired)) {
    return std::nullopt;
  }
  auto id = ExtensionStorageSettingId(desired.extension_id, desired.key);
  return PermittedSettingRecord{.id = BrowserSettingRecordId(id),
                                .setting_id = std::move(id),
                                .value_json = !desired.value   ? "null"
                                              : *desired.value ? "true"
                                                               : "false",
                                .version = std::move(version)};
}

}  // namespace ahoi::sync
