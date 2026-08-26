// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_package_verifier.h"

#include <optional>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/strings/string_number_conversions.h"
#include "components/crx_file/crx_verifier.h"
#include "crypto/hash.h"

namespace ahoi::extensions {

namespace {

UboVerificationError MapVerifierError(crx_file::VerifierResult result) {
  switch (result) {
    case crx_file::VerifierResult::ERROR_FILE_NOT_READABLE:
      return UboVerificationError::kPackageUnreadable;
    case crx_file::VerifierResult::ERROR_FILE_HASH_FAILED:
    case crx_file::VerifierResult::ERROR_EXPECTED_HASH_INVALID:
      return UboVerificationError::kPackageHashMismatch;
    case crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING:
      return UboVerificationError::kPackageKeyMismatch;
    case crx_file::VerifierResult::OK_FULL:
    case crx_file::VerifierResult::OK_DELTA:
    case crx_file::VerifierResult::ERROR_HEADER_INVALID:
    case crx_file::VerifierResult::ERROR_SIGNATURE_INITIALIZATION_FAILED:
    case crx_file::VerifierResult::ERROR_SIGNATURE_VERIFICATION_FAILED:
      return UboVerificationError::kPackageSignatureInvalid;
  }
}

}  // namespace

base::expected<VerifiedUboPackage, UboVerificationError> VerifyUboPackage(
    const UboCatalogEntry& entry,
    const base::FilePath& package_path) {
  std::vector<uint8_t> package_hash;
  std::vector<uint8_t> key_hash;
  if (!base::HexStringToBytes(entry.package_sha256, &package_hash)) {
    return base::unexpected(UboVerificationError::kInvalidPackageHash);
  }
  if (!base::HexStringToBytes(entry.crx_public_key_sha256, &key_hash)) {
    return base::unexpected(UboVerificationError::kInvalidCrxKeyHash);
  }

  std::string public_key;
  std::string crx_id;
  crx_file::VerifierResult result = crx_file::Verify(
      package_path, crx_file::VerifierFormat::CRX3, {key_hash}, package_hash,
      &public_key, &crx_id, /*compressed_verified_contents=*/nullptr);
  if (result != crx_file::VerifierResult::OK_FULL) {
    return base::unexpected(MapVerifierError(result));
  }
  if (crx_id != entry.extension_id || crx_id != kUboClassicExtensionId) {
    return base::unexpected(UboVerificationError::kPackageIdMismatch);
  }

  std::optional<std::vector<uint8_t>> decoded_key =
      base::Base64Decode(public_key);
  if (!decoded_key || base::HexEncodeLower(crypto::hash::Sha256(
                          *decoded_key)) != entry.crx_public_key_sha256) {
    return base::unexpected(UboVerificationError::kPackageKeyMismatch);
  }

  return VerifiedUboPackage{
      .path = package_path,
      .extension_id = crx_id,
      .package_sha256 = entry.package_sha256,
      .crx_public_key_sha256 = entry.crx_public_key_sha256,
  };
}

}  // namespace ahoi::extensions
