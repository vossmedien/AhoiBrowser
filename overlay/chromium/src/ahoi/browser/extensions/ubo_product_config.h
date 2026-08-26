// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_PRODUCT_CONFIG_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_PRODUCT_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>

#include "url/gurl.h"

namespace ahoi::extensions {

// The public Chrome Web Store identity documented by uBlock Origin upstream.
// A production catalog may only authorize a CRX whose signing key derives this
// exact ID. Changing it is a product-security migration, not a release bump.
inline constexpr char kUboClassicExtensionId[] =
    "cjpalhdlnbpafiamejdnhcphjbkeiagm";
inline constexpr char kUboUpstreamRepository[] =
    "https://github.com/gorhill/uBlock";
inline constexpr char kUboLicense[] = "GPL-3.0-or-later";

struct UboProductConfig {
  std::string extension_id = kUboClassicExtensionId;
  GURL catalog_url;
  GURL artifact_origin;
  std::array<uint8_t, 32> catalog_public_key = {};

  // Fails closed until the public catalog endpoint and Ed25519 public key are
  // provisioned. No signing secret belongs in this repository or app bundle.
  bool IsProvisioned() const;
};

// The single production configuration seam. It intentionally returns an
// unprovisioned configuration until catalog signing/hosting and CRX publisher
// provenance have passed the external release gate documented in README.md.
UboProductConfig GetProductionUboProductConfig();

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_PRODUCT_CONFIG_H_
