// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/browser_settings_sync_types.h"

#include <string>

#include "ahoi/browser/sync/browser_setting_catalog.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"

namespace ahoi::sync {

base::Uuid BrowserSettingRecordId(std::string_view setting_id) {
  // Preserve the existing native product-setting identity byte for byte.
  std::string material("setting:");
  material.append(setting_id);
  std::string hex =
      base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(material)));
  hex.resize(32);
  hex[12] = '4';
  hex[16] = '8';
  return base::Uuid::ParseLowercase(hex.substr(0, 8) + "-" + hex.substr(8, 4) +
                                    "-" + hex.substr(12, 4) + "-" +
                                    hex.substr(16, 4) + "-" + hex.substr(20));
}

bool IsPortableBrowserSetting(const PermittedSettingRecord& record) {
  if (record.tombstone ||
      record.id != BrowserSettingRecordId(record.setting_id)) {
    return false;
  }
  auto value = base::JSONReader::Read(record.value_json, base::JSON_PARSE_RFC);
  return value && ValidateBrowserSettingValue(record.setting_id, *value);
}

}  // namespace ahoi::sync
