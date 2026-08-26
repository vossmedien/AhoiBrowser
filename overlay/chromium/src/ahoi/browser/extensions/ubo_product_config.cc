// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_product_config.h"

#include <algorithm>

namespace ahoi::extensions {

namespace {

bool IsHttpsOrigin(const GURL& url) {
  return url.is_valid() && url.SchemeIs("https") && !url.host().empty() &&
         url.DeprecatedGetOriginAsURL() == url;
}

}  // namespace

bool UboProductConfig::IsProvisioned() const {
  return extension_id == kUboClassicExtensionId && catalog_url.is_valid() &&
         catalog_url.SchemeIs("https") && !catalog_url.has_username() &&
         !catalog_url.has_password() && !catalog_url.has_ref() &&
         IsHttpsOrigin(artifact_origin) &&
         std::ranges::any_of(catalog_public_key,
                             [](uint8_t byte) { return byte != 0; });
}

UboProductConfig GetProductionUboProductConfig() {
  // Public values are deliberately not guessed. Provisioning this requires:
  //  * an owned HTTPS catalog/artifact origin,
  //  * the corresponding Ed25519 public key, and
  //  * evidence that the distributed CRX signing key derives the fixed uBO ID.
  return UboProductConfig();
}

}  // namespace ahoi::extensions
