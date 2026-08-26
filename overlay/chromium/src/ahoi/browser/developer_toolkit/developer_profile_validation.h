// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_VALIDATION_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_VALIDATION_H_

#include <string_view>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"

namespace ahoi {

enum class DeveloperProfileValidationError {
  kNone,
  kUnsupportedOrigin,
  kInvalidProfileName,
  kTooManyAssets,
  kInvalidAsset,
  kDuplicateAssetId,
  kEphemeralAssetCannotPersist,
  kCssTooLarge,
  kCssInvalidText,
  kJavaScriptTooLarge,
  kJavaScriptInvalidText,
  kUserAgentInvalid,
  kTooManyHeaderRules,
  kHeaderNameInvalid,
  kHeaderValueInvalid,
  kDuplicateHeaderName,
  kHeadersTooLarge,
  kAdvancedResponseHeadersNotAcknowledged,
};

DeveloperProfileValidationError ValidateDeveloperProfile(
    const url::Origin& origin,
    const DeveloperProfile& profile);
DeveloperProfileValidationError ValidateDeveloperProfileForPersistence(
    const url::Origin& origin,
    const DeveloperProfile& profile);

bool IsValidDeveloperHeaderName(std::string_view name);
bool IsValidDeveloperHeaderValue(std::string_view value);
bool IsValidDeveloperSecretReference(std::string_view reference);
bool IsAdvancedDeveloperResponseHeaderName(std::string_view name);
bool HasActiveAdvancedDeveloperResponseHeaderRules(
    const DeveloperProfile& profile);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_VALIDATION_H_
