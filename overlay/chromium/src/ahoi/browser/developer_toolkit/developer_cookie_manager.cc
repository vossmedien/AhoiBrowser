// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_cookie_manager.h"

#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "net/cookies/cookie_inclusion_status.h"
#include "net/cookies/cookie_util.h"
#include "net/cookies/parsed_cookie.h"

namespace ahoi {
namespace {

constexpr size_t kMaximumEditableCookieBytes = 4096;

bool HasValidPrefix(const GURL& site_url, const DeveloperCookieDraft& draft) {
  const bool secure_attributes =
      draft.secure && net::cookie_util::ProvisionalAccessScheme(site_url) !=
                          net::CookieAccessScheme::kNonCryptographic;
  const bool host_only = !draft.domain.starts_with('.');
  const bool host_attributes =
      secure_attributes && host_only && draft.path == "/";
  const bool http_attributes = secure_attributes && draft.http_only;

  // Chromium's prefix classifier is intentionally internal to //net. Mirror
  // the short RFC 6265bis attribute matrix here instead of reaching through a
  // non-exported component symbol; matching remains case-insensitive exactly
  // like net::cookie_util.
  if (base::StartsWith(draft.name, "__Secure-",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return secure_attributes;
  }
  if (base::StartsWith(draft.name, "__Host-Http-",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return host_attributes && http_attributes;
  }
  if (base::StartsWith(draft.name, "__Http-",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return http_attributes;
  }
  if (base::StartsWith(draft.name, "__Host-",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return host_attributes;
  }
  return true;
}

}  // namespace

DeveloperCookieValidation ValidateDeveloperCookieDraft(
    const GURL& site_url,
    DeveloperCookieDraft draft,
    bool allow_keep_expiration) {
  DeveloperCookieValidation result;
  result.normalized = std::move(draft);
  DeveloperCookieDraft& normalized = result.normalized;

  if (!IsSupportedDeveloperTargetUrl(site_url)) {
    result.error = DeveloperCookieError::kUnsupportedTarget;
    return result;
  }

  base::TrimWhitespaceASCII(normalized.name, base::TRIM_ALL, &normalized.name);
  base::TrimWhitespaceASCII(normalized.domain, base::TRIM_ALL,
                            &normalized.domain);
  base::TrimWhitespaceASCII(normalized.path, base::TRIM_ALL, &normalized.path);
  if (normalized.domain.empty()) {
    normalized.domain = site_url.host();
  }
  if (normalized.path.empty()) {
    normalized.path = "/";
  }

  if (normalized.name.empty() ||
      !net::ParsedCookie::IsValidCookieName(normalized.name)) {
    result.error = DeveloperCookieError::kInvalidName;
    return result;
  }
  if (!net::ParsedCookie::IsValidCookieValue(normalized.value) ||
      normalized.name.size() + normalized.value.size() >
          kMaximumEditableCookieBytes) {
    result.error = DeveloperCookieError::kInvalidValue;
    return result;
  }

  const bool requested_domain_cookie = normalized.domain.starts_with('.');
  if (requested_domain_cookie) {
    net::CookieInclusionStatus status;
    std::optional<std::string> cookie_domain =
        net::cookie_util::GetCookieDomainWithString(site_url, normalized.domain,
                                                    status);
    if (!cookie_domain || !cookie_domain->starts_with('.')) {
      result.error = DeveloperCookieError::kInvalidDomain;
      return result;
    }
    normalized.domain = std::move(*cookie_domain);
  } else {
    normalized.domain = base::ToLowerASCII(normalized.domain);
    // A host-only cookie cannot be created for a sibling or parent host. Use a
    // leading dot when a deliberate parent-domain cookie is intended.
    if (normalized.domain != site_url.host()) {
      result.error = DeveloperCookieError::kInvalidDomain;
      return result;
    }
  }

  if (normalized.path.size() > kMaximumEditableCookieBytes ||
      !normalized.path.starts_with('/') || normalized.path.contains(';') ||
      normalized.path.contains('\r') || normalized.path.contains('\n') ||
      normalized.path.contains('\0')) {
    result.error = DeveloperCookieError::kInvalidPath;
    return result;
  }
  if (normalized.same_site == DeveloperCookieSameSite::kNone &&
      !normalized.secure) {
    result.error = DeveloperCookieError::kInvalidSameSite;
    return result;
  }
  if (!HasValidPrefix(site_url, normalized)) {
    result.error = DeveloperCookieError::kInvalidPrefix;
    return result;
  }
  if (normalized.expiration == DeveloperCookieExpiration::kKeep &&
      !allow_keep_expiration) {
    result.error = DeveloperCookieError::kInvalidExpiration;
    return result;
  }
  return result;
}

bool DeveloperCookieMatchesFilter(const DeveloperCookie& cookie,
                                  std::u16string_view filter) {
  std::string needle = base::ToLowerASCII(base::UTF16ToUTF8(filter));
  base::TrimWhitespaceASCII(needle, base::TRIM_ALL, &needle);
  if (needle.empty()) {
    return true;
  }
  const auto contains = [&needle](std::string_view value) {
    return base::ToLowerASCII(value).contains(needle);
  };
  return contains(cookie.name) || contains(cookie.value) ||
         contains(cookie.domain) || contains(cookie.path);
}

}  // namespace ahoi
