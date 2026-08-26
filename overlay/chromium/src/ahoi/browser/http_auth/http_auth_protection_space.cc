// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "ahoi/browser/http_auth/http_auth_credential_service_internal.h"
#include "ahoi/browser/http_auth/http_auth_prefs.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_auth_scheme.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace ahoi {

HttpAuthProtectionSpace::HttpAuthProtectionSpace() = default;

HttpAuthProtectionSpace::HttpAuthProtectionSpace(
    net::HttpAuth::Target target,
    url::SchemeHostPort origin,
    net::HttpAuth::Scheme scheme,
    std::string realm,
    std::vector<std::string> permitted_paths,
    net::NetworkAnonymizationKey network_anonymization_key)
    : target(target),
      origin(std::move(origin)),
      scheme(scheme),
      realm(std::move(realm)),
      network_anonymization_key(std::move(network_anonymization_key)) {
  for (const std::string& path : permitted_paths) {
    AddPermittedPath(path);
  }
}

HttpAuthProtectionSpace::~HttpAuthProtectionSpace() = default;
HttpAuthProtectionSpace::HttpAuthProtectionSpace(
    const HttpAuthProtectionSpace& other)
    : target(other.target),
      origin(other.origin),
      scheme(other.scheme),
      realm(other.realm),
      permitted_paths(other.permitted_paths),
      network_anonymization_key(other.network_anonymization_key),
      network_anonymization_key_metadata_value(
          other.network_anonymization_key_metadata_value
              ? std::optional<base::Value>(
                    other.network_anonymization_key_metadata_value->Clone())
              : std::nullopt) {}
HttpAuthProtectionSpace& HttpAuthProtectionSpace::operator=(
    const HttpAuthProtectionSpace& other) {
  if (this == &other) {
    return *this;
  }
  target = other.target;
  origin = other.origin;
  scheme = other.scheme;
  realm = other.realm;
  permitted_paths = other.permitted_paths;
  network_anonymization_key = other.network_anonymization_key;
  network_anonymization_key_metadata_value =
      other.network_anonymization_key_metadata_value
          ? std::optional<base::Value>(
                other.network_anonymization_key_metadata_value->Clone())
          : std::nullopt;
  return *this;
}
HttpAuthProtectionSpace::HttpAuthProtectionSpace(
    HttpAuthProtectionSpace&&) noexcept = default;
HttpAuthProtectionSpace& HttpAuthProtectionSpace::operator=(
    HttpAuthProtectionSpace&&) noexcept = default;

// static
std::optional<HttpAuthProtectionSpace> HttpAuthProtectionSpace::FromChallenge(
    const net::AuthChallengeInfo& auth_info,
    const GURL& request_url,
    const net::NetworkAnonymizationKey& network_anonymization_key) {
  net::HttpAuth::Scheme scheme;
  if (base::EqualsCaseInsensitiveASCII(auth_info.scheme,
                                       net::kBasicAuthScheme)) {
    scheme = net::HttpAuth::AUTH_SCHEME_BASIC;
  } else if (base::EqualsCaseInsensitiveASCII(auth_info.scheme,
                                              net::kDigestAuthScheme)) {
    scheme = net::HttpAuth::AUTH_SCHEME_DIGEST;
  } else {
    return std::nullopt;
  }

  url::SchemeHostPort origin = auth_info.challenger;
  if (!origin.IsValid() && request_url.is_valid()) {
    origin = url::SchemeHostPort(request_url);
  }
  if (!origin.IsValid() || !http_auth_internal::IsHttpOrHttps(origin)) {
    return std::nullopt;
  }

  HttpAuthProtectionSpace protection_space(
      auth_info.is_proxy ? net::HttpAuth::AUTH_PROXY
                         : net::HttpAuth::AUTH_SERVER,
      std::move(origin), scheme, auth_info.realm, {},
      network_anonymization_key);
  if (!auth_info.is_proxy) {
    protection_space.AddPermittedPath(
        auth_info.path.empty() ? request_url.path() : auth_info.path);
  }
  return protection_space.IsValid()
             ? std::optional<HttpAuthProtectionSpace>(protection_space)
             : std::nullopt;
}

bool HttpAuthProtectionSpace::IsValid() const {
  return http_auth_internal::IsSupportedTarget(target) && origin.IsValid() &&
         http_auth_internal::IsHttpOrHttps(origin) &&
         http_auth_internal::IsSupportedScheme(scheme);
}

// static
std::string HttpAuthProtectionSpace::CanonicalProtectionPath(
    std::string_view path) {
  if (path.empty()) {
    return std::string();
  }
  std::string normalized(path);
  const size_t query_or_fragment = normalized.find_first_of("?#");
  if (query_or_fragment != std::string::npos) {
    normalized.erase(query_or_fragment);
  }
  if (normalized.empty()) {
    return std::string();
  }
  if (normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }
  const size_t last_slash = normalized.rfind('/');
  if (last_slash == std::string::npos) {
    return "/";
  }
  return normalized.substr(0, last_slash + 1);
}

void HttpAuthProtectionSpace::AddPermittedPath(std::string_view path) {
  if (target == net::HttpAuth::AUTH_PROXY) {
    return;
  }
  const std::string canonical_path = CanonicalProtectionPath(path);
  if (canonical_path.empty()) {
    return;
  }

  for (const std::string& existing : permitted_paths) {
    if (canonical_path.starts_with(existing)) {
      return;
    }
  }

  std::erase_if(permitted_paths,
                [&canonical_path](const std::string& existing) {
                  return existing.starts_with(canonical_path);
                });
  permitted_paths.push_back(canonical_path);
  std::sort(permitted_paths.begin(), permitted_paths.end());
}

bool HttpAuthProtectionSpace::AllowsPath(std::string_view path) const {
  if (target == net::HttpAuth::AUTH_PROXY) {
    return true;
  }
  const std::string canonical_path = CanonicalProtectionPath(path);
  if (canonical_path.empty()) {
    return false;
  }
  return std::ranges::any_of(
      permitted_paths, [&canonical_path](const std::string& permitted_path) {
        return canonical_path.starts_with(permitted_path);
      });
}

bool HttpAuthProtectionSpace::Matches(const HttpAuthProtectionSpace& other,
                                      std::string_view request_path) const {
  return MatchesRealm(other) &&
         http_auth_internal::IsSameNetworkAnonymizationKey(*this, other) &&
         AllowsPath(request_path);
}

bool HttpAuthProtectionSpace::MatchesRealm(
    const HttpAuthProtectionSpace& other) const {
  return target == other.target && origin == other.origin &&
         scheme == other.scheme && realm == other.realm;
}

std::string HttpAuthProtectionSpace::SignonRealm() const {
  if (target == net::HttpAuth::AUTH_PROXY) {
    return net::HostPortPair(origin.host(), origin.port()).ToString() + "/" +
           realm;
  }
  std::string signon_realm = OriginUrl().DeprecatedGetOriginAsURL().spec();
  if (!signon_realm.ends_with('/')) {
    signon_realm.push_back('/');
  }
  signon_realm.append(realm);
  return signon_realm;
}

GURL HttpAuthProtectionSpace::OriginUrl() const {
  return origin.GetURL();
}

bool operator==(const HttpAuthProtectionSpace& lhs,
                const HttpAuthProtectionSpace& rhs) {
  return lhs.MatchesRealm(rhs) && lhs.permitted_paths == rhs.permitted_paths &&
         http_auth_internal::IsSameNetworkAnonymizationKey(lhs, rhs);
}

bool operator==(const HttpAuthCredentialMetadata& lhs,
                const HttpAuthCredentialMetadata& rhs) {
  return lhs.version == rhs.version &&
         lhs.protection_space == rhs.protection_space &&
         lhs.username == rhs.username && lhs.preferred == rhs.preferred &&
         lhs.last_successful == rhs.last_successful;
}

}  // namespace ahoi
