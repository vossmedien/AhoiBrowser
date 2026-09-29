// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_product_settings.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "ahoi/browser/sync/browser_setting_catalog.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {
namespace {

std::optional<base::Value> NativeValue(std::string_view id,
                                       const base::Value& value) {
  const auto* descriptor = FindBrowserSetting(id);
  if (!descriptor || value.is_none() ||
      !ValidateBrowserSettingValue(id, value)) {
    return std::nullopt;
  }
  if (descriptor->value_kind == base::Value::Type::INTEGER) {
    const auto number = value.GetIfDouble();
    if (!number || !std::isfinite(*number) || std::trunc(*number) != *number ||
        *number < std::numeric_limits<int>::min() ||
        *number > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    return base::Value(static_cast<int>(*number));
  }
  if (descriptor->value_kind == base::Value::Type::DOUBLE) {
    const auto number = value.GetIfDouble();
    if (!number || !std::isfinite(*number)) {
      return std::nullopt;
    }
    return base::Value(*number);
  }
  return value.Clone();
}

}  // namespace

const PermittedSettingIdList& GetPermittedProductSettingIds() {
  static const base::NoDestructor<PermittedSettingIdList> ids([] {
    const auto catalog = GetBrowserSettingCatalog();
    PermittedSettingIdList result;
    result.reserve(catalog.size());
    for (const auto& descriptor : catalog) {
      result.push_back(descriptor.id);
    }
    return result;
  }());
  return *ids;
}

bool IsPermittedProductSettingId(std::string_view setting_id) {
  return FindBrowserSetting(setting_id) != nullptr;
}

bool IsSupportedProductSetting(const PrefService& prefs,
                               std::string_view setting_id) {
  const auto* descriptor = FindBrowserSetting(setting_id);
  if (!descriptor) {
    return false;
  }
  const auto* pref = prefs.FindPreference(setting_id);
  if (!pref || pref->GetType() != descriptor->value_kind) {
    return false;
  }
  constexpr uint32_t kSyncableFlags =
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF |
      user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF;
  return !descriptor->requires_syncable_flag ||
         (pref->registration_flags() & kSyncableFlags) != 0;
}

std::optional<std::string> EncodePermittedProductSetting(
    const PrefService& prefs,
    std::string_view setting_id,
    bool include_default_reset) {
  if (!IsSupportedProductSetting(prefs, setting_id)) {
    return std::nullopt;
  }
  const base::Value* value = prefs.GetUserPrefValue(setting_id);
  if (!value) {
    return include_default_reset ? std::make_optional(std::string("null"))
                                 : std::nullopt;
  }
  const auto normalized = NativeValue(setting_id, *value);
  if (!normalized) {
    return std::nullopt;
  }
  // The registered native type gives each accepted value one representation:
  // integral enum/delay values are ints, reading scale is always a double.
  return base::WriteJson(*normalized);
}

bool ApplyPermittedProductSetting(PrefService* prefs,
                                  std::string_view setting_id,
                                  std::string_view value_json) {
  if (!prefs || !IsSupportedProductSetting(*prefs, setting_id) ||
      !prefs->IsUserModifiablePreference(setting_id)) {
    return false;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(value_json, base::JSON_PARSE_RFC);
  if (!value || !ValidateBrowserSettingValue(setting_id, *value)) {
    return false;
  }
  if (value->is_none()) {
    prefs->ClearPref(setting_id);
    return true;
  }
  const auto normalized = NativeValue(setting_id, *value);
  if (!normalized) {
    return false;
  }
  prefs->Set(setting_id, *normalized);
  return true;
}

}  // namespace ahoi::sync
