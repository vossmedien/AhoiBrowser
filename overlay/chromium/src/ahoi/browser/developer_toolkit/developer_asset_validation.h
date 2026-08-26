// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_VALIDATION_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_VALIDATION_H_

#include <string_view>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "url/gurl.h"

namespace ahoi {

enum class DeveloperAssetValidationError {
  kNone,
  kInvalidId,
  kInvalidName,
  kInvalidSource,
  kInvalidScope,
  kDomainScopeNotAcknowledged,
  kInvalidLifetime,
  kInvalidWorld,
};

DeveloperAssetValidationError ValidateDeveloperAsset(
    const url::Origin& owner_origin,
    const DeveloperAsset& asset);

// Pref-backed profiles survive a browser restart. One-shot/reload assets and
// current-tab tokens therefore belong in a tab-owned ephemeral store instead.
bool IsDeveloperAssetPersistable(const DeveloperAsset& asset);

bool DoesDeveloperAssetMatch(const url::Origin& owner_origin,
                             const DeveloperAsset& asset,
                             const GURL& url,
                             std::string_view current_tab_token = {});

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_VALIDATION_H_
