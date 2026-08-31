// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_product_config.h"

#include <algorithm>

#include "ahoi/browser/extensions/ubo_buildflags.h"
#include "build/buildflag.h"

namespace ahoi::extensions {

namespace {

bool IsHttpsOrigin(const GURL& url) {
  return url.is_valid() && url.SchemeIs("https") && !url.host().empty() &&
         url.DeprecatedGetOriginAsURL() == url;
}

bool IsPinnedGithubReleaseAssetUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIs("https") &&
         url.host() == "release-assets.githubusercontent.com" &&
         url.port().empty() && !url.has_username() && !url.has_password() &&
         !url.has_ref() && url.has_query() &&
         url.path() == kUboClassicReleaseAssetPath;
}

}  // namespace

bool IsUboClassicEnabled() {
  return BUILDFLAG(ENABLE_AHOI_UBO_CLASSIC);
}

bool UboProductConfig::IsPinnedBootstrapProvisioned() const {
  return allow_pinned_bootstrap && extension_id == kUboClassicExtensionId;
}

bool UboProductConfig::IsSignedCatalogProvisioned() const {
  return extension_id == kUboClassicExtensionId && catalog_url.is_valid() &&
         catalog_url.SchemeIs("https") && !catalog_url.has_username() &&
         !catalog_url.has_password() && !catalog_url.has_ref() &&
         IsHttpsOrigin(artifact_origin) &&
         std::ranges::any_of(catalog_public_key,
                             [](uint8_t byte) { return byte != 0; });
}

bool UboProductConfig::IsProvisioned() const {
  return IsPinnedBootstrapProvisioned() || IsSignedCatalogProvisioned();
}

bool IsPinnedUboBootstrapIdentity(std::string_view extension_id,
                                  std::string_view version,
                                  std::string_view package_sha256,
                                  std::string_view crx_public_key_sha256) {
  return extension_id == kUboClassicExtensionId &&
         version == kUboClassicVersion &&
         package_sha256 == kUboClassicPackageSha256 &&
         crx_public_key_sha256 == kUboClassicCrxPublicKeySha256;
}

bool IsAllowedUboPackageFinalUrl(const GURL& requested_url,
                                 const GURL& final_url) {
  if (requested_url == final_url) {
    return true;
  }
  return requested_url == GURL(kUboClassicPackageUrl) &&
         IsPinnedGithubReleaseAssetUrl(final_url);
}

bool IsAllowedUboPackageRedirect(const GURL& requested_url,
                                 const GURL& before_url,
                                 const GURL& redirect_url) {
  return requested_url == GURL(kUboClassicPackageUrl) &&
         before_url == requested_url && redirect_url != requested_url &&
         IsAllowedUboPackageFinalUrl(requested_url, redirect_url);
}

UboProductConfig GetProductionUboProductConfig() {
  UboProductConfig config;
  config.allow_pinned_bootstrap = IsUboClassicEnabled();
  return config;
}

}  // namespace ahoi::extensions
