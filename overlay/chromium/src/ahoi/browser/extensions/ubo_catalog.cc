// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_catalog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace ahoi::extensions {

namespace {

constexpr size_t kMaximumCatalogBytes = 64 * 1024;
constexpr int kCatalogSchemaVersion = 2;
constexpr base::TimeDelta kMaximumValidityWindow = base::Days(45);
constexpr base::TimeDelta kClockSkew = base::Minutes(5);

bool ParseUint64(const base::DictValue& dict,
                 std::string_view key,
                 uint64_t* output) {
  const std::string* value = dict.FindString(key);
  return value && base::StringToUint64(*value, output);
}

bool IsLowercaseSha256(std::string_view value) {
  if (value.size() != 64 || !base::IsStringASCII(value)) {
    return false;
  }
  return std::ranges::all_of(value, [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

bool IsLowercaseGitCommit(std::string_view value) {
  return value.size() == 40 && base::IsStringASCII(value) &&
         std::ranges::all_of(value, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

bool IsHttpsUrlFromOrigin(const GURL& url, const GURL& origin) {
  return url.is_valid() && url.SchemeIs("https") && !url.has_username() &&
         !url.has_password() && !url.has_ref() &&
         url.DeprecatedGetOriginAsURL() == origin;
}

bool IsPinnedUpstreamTagUrl(const GURL& url, std::string_view tag) {
  if (!url.is_valid() || !url.SchemeIs("https") || url.host() != "github.com" ||
      url.has_username() || url.has_password() || url.has_ref() ||
      url.has_query()) {
    return false;
  }
  return url.path() == base::StrCat({"/gorhill/uBlock/releases/tag/", tag});
}

base::expected<UboCatalogEntry, UboVerificationError> ParsePayload(
    const UboProductConfig& config,
    std::string_view payload,
    base::Time now) {
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
  if (!parsed || parsed->size() != 14u) {
    return base::unexpected(UboVerificationError::kMalformedPayload);
  }

  const std::optional<int> schema_version = parsed->FindInt("schema_version");
  if (!schema_version || *schema_version != kCatalogSchemaVersion) {
    return base::unexpected(UboVerificationError::kUnsupportedSchema);
  }

  uint64_t sequence = 0;
  uint64_t valid_from_seconds = 0;
  uint64_t valid_until_seconds = 0;
  if (!ParseUint64(*parsed, "sequence", &sequence) || sequence == 0 ||
      !ParseUint64(*parsed, "valid_from", &valid_from_seconds) ||
      !ParseUint64(*parsed, "valid_until", &valid_until_seconds) ||
      valid_from_seconds > valid_until_seconds ||
      valid_until_seconds >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return base::unexpected(UboVerificationError::kInvalidSequence);
  }

  UboCatalogEntry entry;
  entry.sequence = sequence;
  entry.valid_from = base::Time::FromSecondsSinceUnixEpoch(
      static_cast<int64_t>(valid_from_seconds));
  entry.valid_until = base::Time::FromSecondsSinceUnixEpoch(
      static_cast<int64_t>(valid_until_seconds));
  if (now + kClockSkew < entry.valid_from) {
    return base::unexpected(UboVerificationError::kCatalogNotYetValid);
  }
  if (now - kClockSkew > entry.valid_until) {
    return base::unexpected(UboVerificationError::kCatalogExpired);
  }
  if (entry.valid_until - entry.valid_from > kMaximumValidityWindow) {
    return base::unexpected(UboVerificationError::kValidityWindowTooLong);
  }

  const std::string* extension_id = parsed->FindString("extension_id");
  if (!extension_id || *extension_id != config.extension_id ||
      *extension_id != kUboClassicExtensionId) {
    return base::unexpected(UboVerificationError::kUnexpectedExtensionId);
  }
  entry.extension_id = *extension_id;

  const std::string* version = parsed->FindString("version");
  entry.version = base::Version(version ? *version : std::string());
  if (!entry.version.IsValid()) {
    return base::unexpected(UboVerificationError::kInvalidVersion);
  }

  const std::string* package_url = parsed->FindString("package_url");
  entry.package_url = GURL(package_url ? *package_url : std::string());
  if (!IsHttpsUrlFromOrigin(entry.package_url, config.artifact_origin)) {
    return base::unexpected(
        UboVerificationError::kInsecureOrUnexpectedArtifactUrl);
  }

  const std::string* update_manifest_url =
      parsed->FindString("update_manifest_url");
  entry.update_manifest_url =
      GURL(update_manifest_url ? *update_manifest_url : std::string());
  if (!IsHttpsUrlFromOrigin(entry.update_manifest_url,
                            config.artifact_origin)) {
    return base::unexpected(UboVerificationError::kInvalidUpdateManifestUrl);
  }

  const std::string* package_hash = parsed->FindString("sha256");
  if (!package_hash || !IsLowercaseSha256(*package_hash)) {
    return base::unexpected(UboVerificationError::kInvalidPackageHash);
  }
  entry.package_sha256 = *package_hash;

  const std::string* key_hash = parsed->FindString("crx_public_key_sha256");
  if (!key_hash || !IsLowercaseSha256(*key_hash)) {
    return base::unexpected(UboVerificationError::kInvalidCrxKeyHash);
  }
  entry.crx_public_key_sha256 = *key_hash;

  const std::string* upstream_tag = parsed->FindString("upstream_tag");
  const std::string* upstream_source =
      parsed->FindString("upstream_source_url");
  if (!upstream_tag || upstream_tag->empty() || upstream_tag->size() > 64 ||
      upstream_tag->find_first_not_of(
          "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-") !=
          std::string::npos) {
    return base::unexpected(UboVerificationError::kInvalidUpstreamSource);
  }
  entry.upstream_tag = *upstream_tag;
  const std::string* upstream_commit = parsed->FindString("upstream_commit");
  if (!upstream_commit || !IsLowercaseGitCommit(*upstream_commit)) {
    return base::unexpected(UboVerificationError::kInvalidUpstreamSource);
  }
  entry.upstream_commit = *upstream_commit;
  entry.upstream_source_url =
      GURL(upstream_source ? *upstream_source : std::string());
  if (!IsPinnedUpstreamTagUrl(entry.upstream_source_url, entry.upstream_tag)) {
    return base::unexpected(UboVerificationError::kInvalidUpstreamSource);
  }

  const std::string* license = parsed->FindString("license");
  if (!license || *license != kUboLicense) {
    return base::unexpected(UboVerificationError::kUnexpectedLicense);
  }
  entry.license = *license;
  return entry;
}

}  // namespace

UboCatalogEntry GetPinnedUboBootstrapCatalogEntry() {
  UboCatalogEntry entry;
  entry.sequence = kUboClassicBootstrapSequence;
  entry.valid_from = base::Time::Min();
  entry.valid_until = base::Time::Max();
  entry.extension_id = kUboClassicExtensionId;
  entry.version = base::Version(kUboClassicVersion);
  entry.package_url = GURL(kUboClassicPackageUrl);
  // No extension-controlled or guessed update endpoint is assigned. A later
  // update must arrive through the separately provisioned signed catalog.
  entry.update_manifest_url = GURL();
  entry.package_sha256 = kUboClassicPackageSha256;
  entry.crx_public_key_sha256 = kUboClassicCrxPublicKeySha256;
  entry.upstream_tag = kUboClassicVersion;
  entry.upstream_commit = kUboClassicReleaseCommit;
  entry.upstream_source_url = GURL(base::StrCat(
      {kUboUpstreamRepository, "/releases/tag/", kUboClassicVersion}));
  entry.license = kUboLicense;
  return entry;
}

bool IsPinnedUboBootstrapCatalogEntry(const UboCatalogEntry& entry) {
  return entry.sequence == kUboClassicBootstrapSequence &&
         entry.extension_id == kUboClassicExtensionId &&
         entry.version == base::Version(kUboClassicVersion) &&
         entry.package_url == GURL(kUboClassicPackageUrl) &&
         entry.update_manifest_url.is_empty() &&
         entry.package_sha256 == kUboClassicPackageSha256 &&
         entry.crx_public_key_sha256 == kUboClassicCrxPublicKeySha256 &&
         entry.upstream_tag == kUboClassicVersion &&
         entry.upstream_commit == kUboClassicReleaseCommit &&
         entry.upstream_source_url ==
             GURL(base::StrCat({kUboUpstreamRepository, "/releases/tag/",
                                kUboClassicVersion})) &&
         entry.license == kUboLicense;
}

std::string_view UboVerificationErrorToString(UboVerificationError error) {
  switch (error) {
    case UboVerificationError::kNotProvisioned:
      return "product configuration is not provisioned";
    case UboVerificationError::kInsecureOrUnexpectedCatalogUrl:
      return "catalog URL is not the pinned HTTPS URL";
    case UboVerificationError::kCatalogTooLarge:
      return "catalog exceeds the size limit";
    case UboVerificationError::kMalformedEnvelope:
      return "catalog envelope is malformed";
    case UboVerificationError::kInvalidSignatureEncoding:
      return "catalog signature encoding is invalid";
    case UboVerificationError::kInvalidSignature:
      return "catalog signature is invalid";
    case UboVerificationError::kMalformedPayload:
      return "catalog payload is malformed";
    case UboVerificationError::kUnsupportedSchema:
      return "catalog schema is unsupported";
    case UboVerificationError::kInvalidSequence:
      return "catalog sequence or validity time is invalid";
    case UboVerificationError::kCatalogNotYetValid:
      return "catalog is not yet valid";
    case UboVerificationError::kCatalogExpired:
      return "catalog is expired";
    case UboVerificationError::kValidityWindowTooLong:
      return "catalog validity window is too long";
    case UboVerificationError::kUnexpectedExtensionId:
      return "extension ID is not the fixed pinned uBO identity";
    case UboVerificationError::kInvalidVersion:
      return "extension version is invalid";
    case UboVerificationError::kInsecureOrUnexpectedArtifactUrl:
      return "package URL is outside the pinned HTTPS origin";
    case UboVerificationError::kInvalidUpdateManifestUrl:
      return "update manifest URL is outside the pinned HTTPS origin";
    case UboVerificationError::kInvalidPackageHash:
      return "package hash is invalid";
    case UboVerificationError::kInvalidCrxKeyHash:
      return "CRX public-key hash is invalid";
    case UboVerificationError::kInvalidUpstreamSource:
      return "upstream tag source is invalid";
    case UboVerificationError::kUnexpectedLicense:
      return "license is not the pinned GPL license";
    case UboVerificationError::kRollback:
      return "catalog or package would roll back trusted state";
    case UboVerificationError::kPackageUnreadable:
      return "package is unreadable";
    case UboVerificationError::kPackageSignatureInvalid:
      return "CRX signature is invalid";
    case UboVerificationError::kPackageHashMismatch:
      return "package hash does not match trusted entry metadata";
    case UboVerificationError::kPackageKeyMismatch:
      return "CRX signing key does not match trusted entry metadata";
    case UboVerificationError::kPackageIdMismatch:
      return "CRX ID does not match the fixed uBO identity";
    case UboVerificationError::kInstallFailed:
      return "Chromium rejected or could not complete the CRX installation";
    case UboVerificationError::kAuthorizationConflict:
      return "another verified installation is already pending";
    case UboVerificationError::kInstalledExtensionMismatch:
      return "installed extension does not match the verified package";
    case UboVerificationError::kStateWriteFailed:
      return "verified authorization state could not be committed";
  }
}

base::expected<UboCatalogEntry, UboVerificationError> VerifyUboCatalog(
    const UboProductConfig& config,
    const GURL& fetched_from,
    std::string_view envelope_json,
    base::Time now) {
  if (!config.IsSignedCatalogProvisioned()) {
    return base::unexpected(UboVerificationError::kNotProvisioned);
  }
  if (fetched_from != config.catalog_url || !fetched_from.SchemeIs("https")) {
    return base::unexpected(
        UboVerificationError::kInsecureOrUnexpectedCatalogUrl);
  }
  if (envelope_json.size() > kMaximumCatalogBytes) {
    return base::unexpected(UboVerificationError::kCatalogTooLarge);
  }

  std::optional<base::DictValue> envelope =
      base::JSONReader::ReadDict(envelope_json, base::JSON_PARSE_RFC);
  if (!envelope || envelope->size() != 2u) {
    return base::unexpected(UboVerificationError::kMalformedEnvelope);
  }
  const std::string* payload = envelope->FindString("payload");
  const std::string* encoded_signature = envelope->FindString("signature");
  if (!payload || !encoded_signature) {
    return base::unexpected(UboVerificationError::kMalformedEnvelope);
  }

  std::optional<std::vector<uint8_t>> signature =
      base::Base64Decode(*encoded_signature);
  if (!signature || signature->size() != ED25519_SIGNATURE_LEN) {
    return base::unexpected(UboVerificationError::kInvalidSignatureEncoding);
  }
  if (ED25519_verify(reinterpret_cast<const uint8_t*>(payload->data()),
                     payload->size(), signature->data(),
                     config.catalog_public_key.data()) != 1) {
    return base::unexpected(UboVerificationError::kInvalidSignature);
  }
  return ParsePayload(config, *payload, now);
}

}  // namespace ahoi::extensions
