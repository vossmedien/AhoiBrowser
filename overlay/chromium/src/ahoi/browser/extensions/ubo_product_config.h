// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_PRODUCT_CONFIG_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_PRODUCT_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "url/gurl.h"

namespace ahoi::extensions {

// Official GitHub release 1.74.0 identity. The ID is derived from the CRX3 RSA
// public key whose full SHA-256 is pinned below. Changing any value is a
// product-security migration, not a release bump.
inline constexpr char kUboClassicExtensionId[] =
    "fkgkibajhfbepljeaefdnfnegdcjomkh";
inline constexpr char kUboFormerClassicWebStoreExtensionId[] =
    "cjpalhdlnbpafiamejdnhcphjbkeiagm";
inline constexpr char kUboLiteExtensionId[] =
    "ddkjiahejlhfcafbddmgiahcphecmpfh";
inline constexpr char kUboClassicVersion[] = "1.74.0";
inline constexpr uint64_t kUboClassicBootstrapSequence = 174000;
inline constexpr char kUboClassicReleaseCommit[] =
    "6dd2d95e50d134a477a4e183343c0b26e9147123";
inline constexpr char kUboClassicPackageUrl[] =
    "https://github.com/gorhill/uBlock/releases/download/1.74.0/"
    "uBlock0_1.74.0.chromium.crx";
inline constexpr char kUboClassicPackageSha256[] =
    "b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e";
inline constexpr char kUboClassicCrxPublicKeySha256[] =
    "5a6a81097514fb940453d5d46329eca78100e3cc0c5fca508e1a413f77f567bf";
inline constexpr char kUboClassicReleaseAssetPath[] =
    "/github-production-release-asset/33263118/"
    "ade4daf2-50e8-4953-8821-5c2d43f07a65";
inline constexpr char kUboUpstreamRepository[] =
    "https://github.com/gorhill/uBlock";
inline constexpr char kUboLicense[] = "GPL-3.0-or-later";

struct UboProductConfig {
  std::string extension_id = kUboClassicExtensionId;
  GURL catalog_url;
  GURL artifact_origin;
  std::array<uint8_t, 32> catalog_public_key = {};
  bool allow_pinned_bootstrap = false;

  // The pinned bootstrap is initial-install only. Signed catalog provisioning
  // remains a separate trust root for later updates.
  bool IsPinnedBootstrapProvisioned() const;
  bool IsSignedCatalogProvisioned() const;
  bool IsProvisioned() const;
};

// Compile-time product gate for the narrowly scoped uBO Classic MV2 path.
// It defaults to false and is enabled only by an explicit dogfood GN profile.
bool IsUboClassicEnabled();

bool IsPinnedUboBootstrapIdentity(std::string_view extension_id,
                                  std::string_view version,
                                  std::string_view package_sha256,
                                  std::string_view crx_public_key_sha256);

// Exact URL equality is required when no redirect occurs. The sole redirect
// exception is the immutable GitHub release-asset path for the pinned CRX.
bool IsAllowedUboPackageFinalUrl(const GURL& requested_url,
                                 const GURL& final_url);
bool IsAllowedUboPackageRedirect(const GURL& requested_url,
                                 const GURL& before_url,
                                 const GURL& redirect_url);

// The single production configuration seam enables the browser-pinned Official
// GitHub release bootstrap only in an explicitly opted-in dogfood build.
// Catalog networking remains unprovisioned until its separate signing and
// hosting gate passes.
UboProductConfig GetProductionUboProductConfig();

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_PRODUCT_CONFIG_H_
