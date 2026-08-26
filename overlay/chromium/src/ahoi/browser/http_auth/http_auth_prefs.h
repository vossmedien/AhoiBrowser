// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_PREFS_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi::http_auth_prefs {

// This preference contains versioned, non-secret HTTP-auth metadata only.
// Password values are never written here; they remain in Chromium's secure
// PasswordStore.
inline constexpr char kCredentialMetadata[] = "ahoi.http_auth.metadata";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace ahoi::http_auth_prefs

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_PREFS_H_
