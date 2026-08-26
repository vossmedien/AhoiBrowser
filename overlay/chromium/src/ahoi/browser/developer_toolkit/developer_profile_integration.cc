// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace ahoi {
namespace {

std::string RuntimeAssetId(const url::Origin& owner_origin,
                           std::string_view asset_id) {
  const std::string owner = owner_origin.Serialize();
  // Length-prefixing makes the namespace unambiguous without a collision-prone
  // hash. Origins contain no whitespace, so the resulting value is also safe
  // for exact getElementById()/data-attribute lookups after JSON escaping.
  return base::StrCat(
      {base::NumberToString(owner.size()), ":", owner, ":", asset_id});
}

}  // namespace

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
        DeveloperAsset resolved = asset;
        resolved.runtime_id = RuntimeAssetId(owner_origin, asset.id);
        result.push_back(std::move(resolved));
      }
    }
  }
  return result;
}

}  // namespace ahoi
