// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_product_settings.h"

#include <algorithm>

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {
namespace {

constexpr PermittedSettingIdList kPermittedIds = {
    appearance::kGlassEnabledPref,
    appearance::kFloatingNavigationAutoHideEnabledPref,
    appearance::kFloatingNavigationRevealNotchEnabledPref,
    appearance::kFloatingNavigationAutoHideDelayMsPref,
};

bool IsBooleanSetting(std::string_view id) {
  return id == appearance::kGlassEnabledPref ||
         id == appearance::kFloatingNavigationAutoHideEnabledPref ||
         id == appearance::kFloatingNavigationRevealNotchEnabledPref;
}

bool IsValidValue(std::string_view id, const base::Value& value) {
  if (IsBooleanSetting(id)) {
    return value.is_bool();
  }
  return id == appearance::kFloatingNavigationAutoHideDelayMsPref &&
         value.is_int() &&
         value.GetInt() >=
             appearance::kMinimumFloatingNavigationAutoHideDelayMs &&
         value.GetInt() <=
             appearance::kMaximumFloatingNavigationAutoHideDelayMs;
}

}  // namespace

const PermittedSettingIdList& GetPermittedProductSettingIds() {
  return kPermittedIds;
}

bool IsPermittedProductSettingId(std::string_view setting_id) {
  return std::ranges::find(kPermittedIds, setting_id) != kPermittedIds.end();
}

std::optional<std::string> EncodePermittedProductSetting(
    const PrefService& prefs,
    std::string_view setting_id) {
  if (!IsPermittedProductSettingId(setting_id) ||
      !prefs.FindPreference(setting_id)) {
    return std::nullopt;
  }
  const base::Value& value = prefs.GetValue(setting_id);
  if (!IsValidValue(setting_id, value)) {
    return std::nullopt;
  }
  std::string encoded;
  if (!base::JSONWriter::Write(value, &encoded)) {
    return std::nullopt;
  }
  return encoded;
}

bool ApplyPermittedProductSetting(PrefService* prefs,
                                  std::string_view setting_id,
                                  std::string_view value_json) {
  if (!prefs || !IsPermittedProductSettingId(setting_id) ||
      !prefs->FindPreference(setting_id) ||
      !prefs->IsUserModifiablePreference(setting_id)) {
    return false;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(value_json, base::JSON_PARSE_RFC);
  if (!value || !IsValidValue(setting_id, *value)) {
    return false;
  }
  prefs->Set(setting_id, *value);
  return true;
}

}  // namespace ahoi::sync
