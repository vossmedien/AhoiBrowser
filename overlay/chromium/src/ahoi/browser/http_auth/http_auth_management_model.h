// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_MANAGEMENT_MODEL_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_MANAGEMENT_MODEL_H_

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"

class GURL;

namespace ahoi {

// A metadata-only row for the native HTTP-auth manager. Passwords are not a
// member of this type, which prevents the management surface from exposing a
// secret through labels, accessibility, screenshots, logs, or clipboard APIs.
struct HttpAuthManagementEntry {
  HttpAuthCredentialMetadata metadata;
  bool is_active_realm = false;
  bool can_switch_account = false;

  friend bool operator==(const HttpAuthManagementEntry& lhs,
                         const HttpAuthManagementEntry& rhs) = default;
};

// Filters and orders the profile-local metadata used by the management UI.
// Search is case- and accent-insensitive and covers origin, realm, username,
// auth scheme, and server/proxy target. Secrets are never queried.
std::vector<HttpAuthManagementEntry> BuildHttpAuthManagementEntries(
    std::vector<HttpAuthCredentialMetadata> metadata,
    std::u16string_view query,
    const GURL& source_url,
    const std::optional<HttpAuthProtectionSpace>& active_protection_space);

bool HttpAuthProtectionSpaceMatchesManagementQuery(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view query);

// Returns true only when the selected row belongs to the exact active realm,
// network partition, and source origin. This is the split-pane safety gate:
// clearing/reloading is bound to the WebContents that opened the manager.
bool CanSwitchManagedHttpAuthAccount(
    const HttpAuthCredentialMetadata& metadata,
    const GURL& source_url,
    const std::optional<HttpAuthProtectionSpace>& active_protection_space);

// Stable identity helpers used for grouping and two-step destructive
// confirmations. PasswordStore has no NetworkAnonymizationKey dimension, so
// these match the exact underlying stored-secret scope and deliberately ignore
// only the network partition. Account switching remains partition-exact via
// CanSwitchManagedHttpAuthAccount().
bool IsSameManagedHttpAuthCredential(const HttpAuthCredentialMetadata& lhs,
                                     const HttpAuthCredentialMetadata& rhs);
bool IsSameManagedHttpAuthRealm(const HttpAuthProtectionSpace& lhs,
                                const HttpAuthProtectionSpace& rhs);

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_MANAGEMENT_MODEL_H_
