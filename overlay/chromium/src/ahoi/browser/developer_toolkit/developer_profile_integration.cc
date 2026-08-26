// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"

#include <algorithm>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"

namespace ahoi {

std::optional<DeveloperProfile> GetDeveloperProfileForNavigation(
    const DeveloperProfileStore& store,
    const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  const url::Origin origin = url::Origin::Create(url);
  if (origin.opaque()) {
    return std::nullopt;
  }
  return store.Get(origin);
}

std::vector<DeveloperAsset> GetDeveloperAssetsForNavigation(
    const DeveloperProfileStore& store,
    const GURL& url,
    std::string_view current_tab_token) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return {};
  }
  std::vector<url::Origin> origins = store.ListOrigins();
  std::sort(origins.begin(), origins.end(),
            [](const url::Origin& left, const url::Origin& right) {
              return left.Serialize() < right.Serialize();
            });
  std::vector<DeveloperAsset> result;
  for (const url::Origin& owner_origin : origins) {
    const std::optional<DeveloperProfile> profile = store.Get(owner_origin);
    if (!profile) {
      continue;
    }
    for (const DeveloperAsset& asset : profile->assets) {
      if (DoesDeveloperAssetMatch(owner_origin, asset, url,
                                  current_tab_token)) {
        result.push_back(asset);
      }
    }
  }
  return result;
}

}  // namespace ahoi
