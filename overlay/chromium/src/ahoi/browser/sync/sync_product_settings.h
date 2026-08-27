// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_PRODUCT_SETTINGS_H_
#define AHOI_BROWSER_SYNC_SYNC_PRODUCT_SETTINGS_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>

class PrefService;

namespace ahoi::sync {

using PermittedSettingIdList = std::array<std::string_view, 5>;

const PermittedSettingIdList& GetPermittedProductSettingIds();
bool IsPermittedProductSettingId(std::string_view setting_id);
std::optional<std::string> EncodePermittedProductSetting(
    const PrefService& prefs,
    std::string_view setting_id);
bool ApplyPermittedProductSetting(PrefService* prefs,
                                  std::string_view setting_id,
                                  std::string_view value_json);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_PRODUCT_SETTINGS_H_
