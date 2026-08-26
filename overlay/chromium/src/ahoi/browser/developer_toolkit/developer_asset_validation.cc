// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"

#include <algorithm>
#include <string>

#include "base/strings/string_util.h"

namespace ahoi {
namespace {

bool IsValidUtf8WithoutNul(std::string_view value) {
  return base::IsStringUTF8(value) &&
         value.find('\0') == std::string_view::npos;
}

bool IsSafeIdentifier(std::string_view value, size_t max_size) {
  if (value.empty() || value.size() > max_size) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
           byte == '.' || byte == ':';
  });
}

bool IsCanonicalDomain(std::string_view value) {
  if (value.empty() || value.size() > kMaxDeveloperAssetScopeBytes ||
      value.front() == '.' || value.back() == '.' ||
      value.find('/') != std::string_view::npos ||
      value.find(':') != std::string_view::npos ||
      value != base::ToLowerASCII(value)) {
    return false;
  }
  const GURL probe("https://" + std::string(value) + "/");
  return probe.is_valid() && probe.host() == value;
}

}  // namespace

DeveloperAssetValidationError ValidateDeveloperAsset(
    const url::Origin& owner_origin,
    const DeveloperAsset& asset) {
  if (!IsSafeIdentifier(asset.id, kMaxDeveloperAssetIdBytes) ||
      !asset.runtime_id.empty()) {
    return DeveloperAssetValidationError::kInvalidId;
  }
  if (asset.name.empty() || asset.name.size() > kMaxDeveloperAssetNameBytes ||
      !IsValidUtf8WithoutNul(asset.name)) {
    return DeveloperAssetValidationError::kInvalidName;
  }
  const size_t source_limit = asset.kind == DeveloperAssetKind::kStyle
                                  ? kMaxDeveloperCssBytes
                                  : kMaxDeveloperJavaScriptBytes;
  if (asset.source.size() > source_limit ||
      !IsValidUtf8WithoutNul(asset.source) ||
      (asset.enabled && asset.source.empty())) {
    return DeveloperAssetValidationError::kInvalidSource;
  }
  if (asset.compiled_css.size() > kMaxDeveloperCssBytes ||
      !IsValidUtf8WithoutNul(asset.compiled_css)) {
    return DeveloperAssetValidationError::kInvalidSource;
  }

  const GURL owner_url = owner_origin.GetURL();
  switch (asset.scope.kind) {
    case DeveloperAssetScopeKind::kCurrentTab:
      if (!IsSafeIdentifier(asset.scope.value, kMaxDeveloperAssetScopeBytes) ||
          asset.lifetime == DeveloperAssetLifetime::kRestart ||
          asset.sync_enabled) {
        return DeveloperAssetValidationError::kInvalidScope;
      }
      break;
    case DeveloperAssetScopeKind::kOrigin:
      if (asset.scope.value != owner_origin.Serialize()) {
        return DeveloperAssetValidationError::kInvalidScope;
      }
      break;
    case DeveloperAssetScopeKind::kDomain:
      if (!IsCanonicalDomain(asset.scope.value) ||
          !owner_url.DomainIs(asset.scope.value)) {
        return DeveloperAssetValidationError::kInvalidScope;
      }
      if (asset.enabled && !asset.domain_scope_warning_accepted) {
        return DeveloperAssetValidationError::kDomainScopeNotAcknowledged;
      }
      break;
    case DeveloperAssetScopeKind::kPath:
      if (asset.scope.value.empty() || asset.scope.value.front() != '/' ||
          asset.scope.value.size() > kMaxDeveloperAssetScopeBytes ||
          asset.scope.value.find('?') != std::string::npos ||
          asset.scope.value.find('#') != std::string::npos ||
          !IsValidUtf8WithoutNul(asset.scope.value)) {
        return DeveloperAssetValidationError::kInvalidScope;
      }
      break;
  }

  if (asset.scope.kind != DeveloperAssetScopeKind::kDomain &&
      asset.domain_scope_warning_accepted) {
    return DeveloperAssetValidationError::kInvalidScope;
  }

  if (asset.sync_enabled &&
      asset.lifetime != DeveloperAssetLifetime::kRestart) {
    return DeveloperAssetValidationError::kInvalidLifetime;
  }
  if (asset.kind == DeveloperAssetKind::kStyle) {
    if (asset.javascript_world != DeveloperJavaScriptWorld::kIsolated ||
        asset.main_world_warning_accepted) {
      return DeveloperAssetValidationError::kInvalidWorld;
    }
    if (asset.style_language == DeveloperStyleLanguage::kCss) {
      if (!asset.compiled_css.empty() || asset.compiled_style_version != 0) {
        return DeveloperAssetValidationError::kInvalidSource;
      }
    } else if (asset.compiled_css.empty()) {
      if (asset.enabled || asset.compiled_style_version != 0) {
        return DeveloperAssetValidationError::kInvalidSource;
      }
    } else if (asset.compiled_style_version != kDeveloperStyleCompilerVersion) {
      return DeveloperAssetValidationError::kInvalidSource;
    }
  } else if (asset.style_language != DeveloperStyleLanguage::kCss ||
             !asset.compiled_css.empty() || asset.compiled_style_version != 0 ||
             (asset.javascript_world == DeveloperJavaScriptWorld::kMain &&
              !asset.main_world_warning_accepted) ||
             (asset.javascript_world == DeveloperJavaScriptWorld::kIsolated &&
              asset.main_world_warning_accepted)) {
    return DeveloperAssetValidationError::kInvalidWorld;
  }
  return DeveloperAssetValidationError::kNone;
}

bool IsDeveloperAssetPersistable(const DeveloperAsset& asset) {
  return asset.lifetime == DeveloperAssetLifetime::kRestart &&
         asset.scope.kind != DeveloperAssetScopeKind::kCurrentTab;
}

bool DoesDeveloperAssetMatch(const url::Origin& owner_origin,
                             const DeveloperAsset& asset,
                             const GURL& url,
                             std::string_view current_tab_token) {
  if (!asset.enabled || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      ValidateDeveloperAsset(owner_origin, asset) !=
          DeveloperAssetValidationError::kNone) {
    return false;
  }
  const url::Origin target_origin = url::Origin::Create(url);
  switch (asset.scope.kind) {
    case DeveloperAssetScopeKind::kCurrentTab:
      return !current_tab_token.empty() &&
             current_tab_token == asset.scope.value;
    case DeveloperAssetScopeKind::kOrigin:
      return target_origin == owner_origin;
    case DeveloperAssetScopeKind::kDomain:
      return target_origin.scheme() == owner_origin.scheme() &&
             url.DomainIs(asset.scope.value);
    case DeveloperAssetScopeKind::kPath:
      return target_origin == owner_origin &&
             base::StartsWith(url.path(), asset.scope.value,
                              base::CompareCase::SENSITIVE);
  }
  return false;
}

}  // namespace ahoi
