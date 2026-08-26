// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_PRIVACY_MODE_SERVICE_H_
#define AHOI_BROWSER_PRIVACY_PRIVACY_MODE_SERVICE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/flat_map.h"
#include "url/gurl.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi::privacy {

// This enum is persisted as a string. Keep the values stable for profile
// migrations and future Chromium rolls.
enum class PrivacyMode {
  kStrict,
  kChromiumCompatible,
};

inline constexpr char kGlobalModePref[] = "ahoi.privacy.global_mode";
inline constexpr char kOriginModesPref[] = "ahoi.privacy.origin_modes";

// A UI-thread snapshot. Throttles copy this small value before they leave the
// browser thread and never retain a PrefService pointer.
struct PrivacyPolicy {
  // Ahoi is not an ad blocker. Keep ordinary websites compatible by default
  // and let users opt into stricter third-party-cookie enforcement globally
  // or per origin. Content filtering remains the responsibility of extensions
  // such as uBlock Origin.
  PrivacyMode global_mode = PrivacyMode::kChromiumCompatible;
  base::flat_map<std::string, PrivacyMode> origin_modes;

  PrivacyMode ModeForUrl(const GURL& url) const;
  bool IsStrictForUrl(const GURL& url) const {
    return ModeForUrl(url) == PrivacyMode::kStrict;
  }
};

// A thread-safe-by-value policy snapshot consumed by Chromium's browser and
// network-service CookieSettings implementations. CookieSettings remains the
// authority for CHIPS, Storage Access grants, explicit content settings and
// enterprise policy precedence; Ahoi only supplies the product's 3PC blocking
// default and per-top-frame mode overrides.
struct CookieEnforcementPolicy {
  std::optional<bool> block_third_party_cookies;
  base::flat_map<std::string, bool> top_frame_overrides;
};

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

PrivacyMode PrivacyModeFromPrefValue(std::string_view value);
std::string_view PrivacyModeToPrefValue(PrivacyMode mode);

PrivacyPolicy GetPolicySnapshot(const PrefService& prefs,
                                bool is_off_the_record);
PrivacyMode GetGlobalMode(const PrefService& prefs);
PrivacyMode GetModeForUrl(const PrefService& prefs,
                          const GURL& url,
                          bool is_off_the_record);

// All origin overrides are restricted to HTTP(S). Overrides are never written
// from an OTR profile, preventing an incognito exception from leaking into the
// regular profile.
bool SetGlobalMode(PrefService* prefs, PrivacyMode mode);
bool SetOriginMode(PrefService* prefs,
                   const GURL& url,
                   std::optional<PrivacyMode> mode,
                   bool is_off_the_record);
bool IsGlobalModeManaged(const PrefService& prefs);

// Returns no Ahoi layer if the standard cookie-controls pref is enterprise
// managed or Ahoi's prefs are unavailable. Otherwise, Strict adds third-party
// blocking and compatibility removes only that Ahoi layer; Chromium's own
// CookieControls decision remains authoritative. OTR profiles inherit the
// global mode but never regular-profile exceptions.
CookieEnforcementPolicy BuildCookieEnforcementPolicy(const PrefService& prefs,
                                                     bool is_off_the_record);

// Removes only a deliberately small, documented allowlist of marketing
// parameters. Unknown parameters and their original encoding are preserved.
GURL StripKnownTrackingParameters(const GURL& url);

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_PRIVACY_MODE_SERVICE_H_
