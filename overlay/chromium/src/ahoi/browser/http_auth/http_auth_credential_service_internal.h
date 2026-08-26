// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_INTERNAL_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_INTERNAL_H_

#include <optional>
#include <vector>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "components/password_manager/core/browser/password_form.h"

class PrefService;

namespace ahoi::http_auth_internal {

struct MetadataState {
  bool valid = true;
  std::vector<HttpAuthCredentialMetadata> credentials;
  std::vector<HttpAuthProtectionSpace> never_save;
};

bool IsSupportedScheme(net::HttpAuth::Scheme scheme);
bool IsSupportedTarget(net::HttpAuth::Target target);
bool IsHttpOrHttps(const url::SchemeHostPort& origin);

password_manager::PasswordForm::Scheme ToPasswordFormScheme(
    net::HttpAuth::Scheme scheme);

std::optional<base::Value> NetworkAnonymizationKeyValueForMatching(
    const HttpAuthProtectionSpace& protection_space);
std::optional<base::DictValue> SerializeProtectionSpace(
    const HttpAuthProtectionSpace& protection_space);
std::optional<HttpAuthProtectionSpace> DeserializeProtectionSpace(
    const base::DictValue& value);

MetadataState ReadMetadataState(const PrefService* prefs);
bool WriteMetadataState(PrefService* prefs, const MetadataState& state);

bool IsSameNetworkAnonymizationKey(const HttpAuthProtectionSpace& lhs,
                                   const HttpAuthProtectionSpace& rhs);
bool IsSameRealmAndNetwork(const HttpAuthProtectionSpace& lhs,
                           const HttpAuthProtectionSpace& rhs);
bool IsCredentialNewer(const HttpAuthCredential& lhs,
                       const HttpAuthCredential& rhs);

}  // namespace ahoi::http_auth_internal

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_INTERNAL_H_
