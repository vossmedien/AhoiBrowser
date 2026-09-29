// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_CATALOG_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_CATALOG_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/version.h"
#include "url/gurl.h"

namespace ahoi::extensions {

enum class UboVerificationError {
  kNotProvisioned,
  kInsecureOrUnexpectedCatalogUrl,
  kCatalogTooLarge,
  kMalformedEnvelope,
  kInvalidSignatureEncoding,
  kInvalidSignature,
  kMalformedPayload,
  kUnsupportedSchema,
  kInvalidSequence,
  kCatalogNotYetValid,
  kCatalogExpired,
  kValidityWindowTooLong,
  kUnexpectedExtensionId,
  kInvalidVersion,
  kInsecureOrUnexpectedArtifactUrl,
  kInvalidUpdateManifestUrl,
  kInvalidPackageHash,
  kInvalidCrxKeyHash,
  kInvalidUpstreamSource,
  kUnexpectedLicense,
  kRollback,
  kPackageUnreadable,
  kPackageSignatureInvalid,
  kPackageHashMismatch,
  kPackageKeyMismatch,
  kPackageIdMismatch,
  kInstallFailed,
  kAuthorizationConflict,
  kInstalledExtensionMismatch,
  kStateWriteFailed,
};

std::string_view UboVerificationErrorToString(UboVerificationError error);

struct UboCatalogEntry {
  uint64_t sequence = 0;
  base::Time valid_from;
  base::Time valid_until;
  std::string extension_id;
  base::Version version;
  GURL package_url;
  GURL update_manifest_url;
  std::string package_sha256;
  std::string crx_public_key_sha256;
  std::string upstream_tag;
  std::string upstream_commit;
  GURL upstream_source_url;
  std::string license;
};

// Initial bootstrap install metadata is compiled into the signed browser. It
// never authorizes arbitrary MV2 packages and does not replace the signed
// catalog used for later updates.
UboCatalogEntry GetPinnedUboBootstrapCatalogEntry();
bool IsPinnedUboBootstrapCatalogEntry(const UboCatalogEntry& entry);

// The envelope contains exactly {"payload": <raw JSON string>,
// "signature": <base64 Ed25519 signature>}. The exact payload bytes are
// signed, avoiding ambiguous JSON reserialization.
base::expected<UboCatalogEntry, UboVerificationError> VerifyUboCatalog(
    const UboProductConfig& config,
    const GURL& fetched_from,
    std::string_view envelope_json,
    base::Time now);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_CATALOG_H_
