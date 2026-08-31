// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_PACKAGE_VERIFIER_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_PACKAGE_VERIFIER_H_

#include <string>

#include "ahoi/browser/extensions/ubo_catalog.h"
#include "base/files/file_path.h"
#include "base/types/expected.h"

namespace ahoi::extensions {

struct VerifiedUboPackage {
  base::FilePath path;
  std::string extension_id;
  std::string package_sha256;
  std::string crx_public_key_sha256;
};

// Uses Chromium's CRX3 verifier with both the verified-entry package hash and
// CRX signing-key hash pinned. The returned ID is also compared with the fixed
// uBO identity.
base::expected<VerifiedUboPackage, UboVerificationError> VerifyUboPackage(
    const UboCatalogEntry& entry,
    const base::FilePath& package_path);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_PACKAGE_VERIFIER_H_
