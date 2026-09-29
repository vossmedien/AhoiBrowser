// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_PRODUCT_SETTINGS_H_
#define AHOI_BROWSER_SYNC_SYNC_PRODUCT_SETTINGS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class PrefService;

namespace ahoi::sync {

using PermittedSettingIdList = std::vector<std::string_view>;

// Catalog membership is independent of platform registration and user consent.
const PermittedSettingIdList& GetPermittedProductSettingIds();
bool IsPermittedProductSettingId(std::string_view setting_id);
// Checks the actual registered native type and required syncable flags.
bool IsSupportedProductSetting(const PrefService& prefs,
                               std::string_view setting_id);
// Only USER-store values are exported. Absence is a reset only when the caller
// explicitly requests it; a fresh profile's defaults are not local mutations.
std::optional<std::string> EncodePermittedProductSetting(
    const PrefService& prefs,
    std::string_view setting_id,
    bool include_default_reset = false);
bool ApplyPermittedProductSetting(PrefService* prefs,
                                  std::string_view setting_id,
                                  std::string_view value_json);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_PRODUCT_SETTINGS_H_
