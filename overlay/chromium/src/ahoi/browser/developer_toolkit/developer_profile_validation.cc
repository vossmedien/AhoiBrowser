// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"

#include <algorithm>
#include <string_view>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

bool IsValidUtf8WithoutNul(const std::string& value) {
  return base::IsStringUTF8(value) && value.find('\0') == std::string::npos;
}

bool IsSafeHeaderByte(unsigned char byte) {
  // Permit horizontal tab and visible ASCII only. Rejecting obs-text keeps
  // generated request headers deterministic and avoids ambiguous encoding.
  return byte == '\t' || (byte >= 0x20 && byte <= 0x7e);
}

bool IsTokenByte(unsigned char byte) {
  if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
      (byte >= '0' && byte <= '9')) {
    return true;
  }
  constexpr std::string_view kSeparators = "!#$%&'*+-.^_`|~";
  return kSeparators.find(static_cast<char>(byte)) != std::string_view::npos;
}

bool HasCaseInsensitiveName(const std::vector<DeveloperHeaderRule>& rules,
                            std::string_view name,
                            size_t end) {
  for (size_t i = 0; i < end; ++i) {
    if (base::EqualsCaseInsensitiveASCII(rules[i].name, name)) {
      return true;
    }
  }
  return false;
}

DeveloperProfileValidationError ValidateHeaderRules(
    const std::vector<DeveloperHeaderRule>& rules,
    size_t* total_header_bytes,
    size_t* total_header_rules) {
  *total_header_rules += rules.size();
  if (*total_header_rules > kMaxDeveloperHeaderRules) {
    return DeveloperProfileValidationError::kTooManyHeaderRules;
  }

  for (size_t index = 0; index < rules.size(); ++index) {
    const DeveloperHeaderRule& rule = rules[index];
    if (!IsValidDeveloperHeaderName(rule.name)) {
      return DeveloperProfileValidationError::kHeaderNameInvalid;
    }
    if (rule.action == DeveloperHeaderAction::kSet) {
      const bool has_value = !rule.value.empty();
      const bool has_secret = !rule.secret_reference.empty();
      if (has_value == has_secret ||
          (has_value && !IsValidDeveloperHeaderValue(rule.value)) ||
          (has_secret &&
           !IsValidDeveloperSecretReference(rule.secret_reference))) {
        return DeveloperProfileValidationError::kHeaderValueInvalid;
      }
    } else if (rule.action == DeveloperHeaderAction::kRemove) {
      if (!rule.value.empty() || !rule.secret_reference.empty()) {
        return DeveloperProfileValidationError::kHeaderValueInvalid;
      }
    } else {
      return DeveloperProfileValidationError::kHeaderValueInvalid;
    }
    if (HasCaseInsensitiveName(rules, rule.name, index)) {
      return DeveloperProfileValidationError::kDuplicateHeaderName;
    }
    *total_header_bytes +=
        rule.name.size() + rule.value.size() + rule.secret_reference.size();
    if (*total_header_bytes > kMaxDeveloperHeaderBytes) {
      return DeveloperProfileValidationError::kHeadersTooLarge;
    }
  }
  return DeveloperProfileValidationError::kNone;
}

}  // namespace

bool IsValidDeveloperHeaderName(std::string_view name) {
  if (name.empty() || name.size() > kMaxDeveloperHeaderNameBytes) {
    return false;
  }
  return std::all_of(name.begin(), name.end(),
                     [](unsigned char byte) { return IsTokenByte(byte); });
}

bool IsValidDeveloperHeaderValue(std::string_view value) {
  if (value.size() > kMaxDeveloperHeaderValueBytes) {
    return false;
  }
  return std::all_of(value.begin(), value.end(),
                     [](unsigned char byte) { return IsSafeHeaderByte(byte); });
}

bool IsValidDeveloperSecretReference(std::string_view reference) {
  constexpr std::string_view kPrefix = "ahoi-keychain:";
  if (reference.size() <= kPrefix.size() ||
      reference.size() > kMaxDeveloperSecretReferenceBytes ||
      !base::StartsWith(reference, kPrefix, base::CompareCase::SENSITIVE)) {
    return false;
  }
  return std::all_of(reference.begin() + kPrefix.size(), reference.end(),
                     [](unsigned char byte) {
                       return (byte >= 'a' && byte <= 'z') ||
                              (byte >= 'A' && byte <= 'Z') ||
                              (byte >= '0' && byte <= '9') || byte == '-' ||
                              byte == '_' || byte == '.';
                     });
}

bool IsAdvancedDeveloperResponseHeaderName(std::string_view name) {
  return base::EqualsCaseInsensitiveASCII(name, "Content-Security-Policy") ||
         base::EqualsCaseInsensitiveASCII(
             name, "Content-Security-Policy-Report-Only") ||
         base::EqualsCaseInsensitiveASCII(name, "X-Content-Security-Policy") ||
         base::EqualsCaseInsensitiveASCII(name, "X-WebKit-CSP") ||
         base::StartsWith(name, "Access-Control-",
                          base::CompareCase::INSENSITIVE_ASCII);
}

bool HasActiveAdvancedDeveloperResponseHeaderRules(
    const DeveloperProfile& profile) {
  return profile.response_header_rules_enabled &&
         std::ranges::any_of(
             profile.response_header_rules,
             [](const DeveloperHeaderRule& rule) {
               return IsAdvancedDeveloperResponseHeaderName(rule.name);
             });
}

DeveloperProfileValidationError ValidateDeveloperProfile(
    const url::Origin& origin,
    const DeveloperProfile& profile) {
  const GURL origin_url = origin.GetURL();
  if (origin.opaque() || !origin_url.is_valid() ||
      !origin_url.SchemeIsHTTPOrHTTPS()) {
    return DeveloperProfileValidationError::kUnsupportedOrigin;
  }
  if (profile.name.empty() ||
      profile.name.size() > kMaxDeveloperProfileNameBytes ||
      !IsValidUtf8WithoutNul(profile.name)) {
    return DeveloperProfileValidationError::kInvalidProfileName;
  }
  if (profile.assets.size() > kMaxDeveloperAssets) {
    return DeveloperProfileValidationError::kTooManyAssets;
  }
  for (size_t index = 0; index < profile.assets.size(); ++index) {
    if (ValidateDeveloperAsset(origin, profile.assets[index]) !=
        DeveloperAssetValidationError::kNone) {
      return DeveloperProfileValidationError::kInvalidAsset;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (profile.assets[previous].id == profile.assets[index].id) {
        return DeveloperProfileValidationError::kDuplicateAssetId;
      }
    }
  }
  if (profile.user_agent.empty() && profile.user_agent_enabled) {
    return DeveloperProfileValidationError::kUserAgentInvalid;
  }
  if (profile.user_agent.size() > kMaxDeveloperUserAgentBytes ||
      !IsValidDeveloperHeaderValue(profile.user_agent)) {
    return DeveloperProfileValidationError::kUserAgentInvalid;
  }
  size_t total_header_bytes = 0;
  size_t total_header_rules = 0;
  DeveloperProfileValidationError header_error = ValidateHeaderRules(
      profile.header_rules, &total_header_bytes, &total_header_rules);
  if (header_error != DeveloperProfileValidationError::kNone) {
    return header_error;
  }
  header_error = ValidateHeaderRules(profile.response_header_rules,
                                     &total_header_bytes, &total_header_rules);
  if (header_error != DeveloperProfileValidationError::kNone) {
    return header_error;
  }
  if (HasActiveAdvancedDeveloperResponseHeaderRules(profile) &&
      !profile.response_header_advanced_mode_acknowledged) {
    return DeveloperProfileValidationError::
        kAdvancedResponseHeadersNotAcknowledged;
  }
  return DeveloperProfileValidationError::kNone;
}

DeveloperProfileValidationError ValidateDeveloperProfileForPersistence(
    const url::Origin& origin,
    const DeveloperProfile& profile) {
  const DeveloperProfileValidationError validation =
      ValidateDeveloperProfile(origin, profile);
  if (validation != DeveloperProfileValidationError::kNone) {
    return validation;
  }
  for (const DeveloperAsset& asset : profile.assets) {
    if (!IsDeveloperAssetPersistable(asset)) {
      return DeveloperProfileValidationError::kEphemeralAssetCannotPersist;
    }
  }
  return DeveloperProfileValidationError::kNone;
}

}  // namespace ahoi
