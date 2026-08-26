// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_CODEC_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_CODEC_H_

#include <optional>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/values.h"

namespace ahoi {

base::ListValue SerializeDeveloperAssets(
    const std::vector<DeveloperAsset>& assets);
std::optional<std::vector<DeveloperAsset>> DeserializeDeveloperAssets(
    const base::ListValue& value);

// Converts the original v1 fixed CSS/JavaScript slots into explicit v2
// assets. The owner origin is canonical and becomes each asset's origin scope.
std::vector<DeveloperAsset> MigrateLegacyDeveloperAssets(
    const url::Origin& owner_origin,
    bool css_enabled,
    std::string css_source,
    bool javascript_enabled,
    std::string javascript_source);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_CODEC_H_
