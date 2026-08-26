// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_mode_service.h"

#include <string_view>
#include <utility>

#include "base/containers/fixed_flat_set.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "url/origin.h"

namespace ahoi::privacy {

namespace {

constexpr std::string_view kStrictValue = "strict";
constexpr std::string_view kChromiumCompatibleValue = "chromium-compatible";

constexpr auto kTrackingParameters = base::MakeFixedFlatSet<std::string_view>({
    "_ga",
    "dclid",
    "fbclid",
    "gclid",
    "igshid",
    "mc_cid",
    "mc_eid",
    "msclkid",
    "sccid",
    "ttclid",
    "twclid",
    "utm_campaign",
    "utm_content",
    "utm_medium",
    "utm_source",
    "utm_term",
});

bool IsHttpOrHttps(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

std::string OriginKey(const GURL& url) {
  if (!IsHttpOrHttps(url)) {
    return std::string();
  }
  return url::Origin::Create(url).Serialize();
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kGlobalModePref, kChromiumCompatibleValue);
  registry->RegisterDictionaryPref(kOriginModesPref);
}

PrivacyMode PrivacyModeFromPrefValue(std::string_view value) {
  if (value == kStrictValue) {
    return PrivacyMode::kStrict;
  }
  if (value == kChromiumCompatibleValue) {
    return PrivacyMode::kChromiumCompatible;
  }
  // Unknown future values must not silently turn Ahoi into a site-breaking
  // content blocker. Chromium's own security and cookie controls remain
  // authoritative in compatibility mode.
  return PrivacyMode::kChromiumCompatible;
}

std::string_view PrivacyModeToPrefValue(PrivacyMode mode) {
  switch (mode) {
    case PrivacyMode::kStrict:
      return kStrictValue;
    case PrivacyMode::kChromiumCompatible:
      return kChromiumCompatibleValue;
  }
  return kChromiumCompatibleValue;
}

PrivacyMode PrivacyPolicy::ModeForUrl(const GURL& url) const {
  const std::string key = OriginKey(url);
  if (!key.empty()) {
    auto it = origin_modes.find(key);
    if (it != origin_modes.end()) {
      return it->second;
    }
  }
  return global_mode;
}

PrivacyPolicy GetPolicySnapshot(const PrefService& prefs,
                                bool is_off_the_record) {
  PrivacyPolicy policy;
  if (prefs.FindPreference(kGlobalModePref)) {
    policy.global_mode =
        PrivacyModeFromPrefValue(prefs.GetString(kGlobalModePref));
  }

  // OTR preference services can expose inherited values. Deliberately omit
  // origin exceptions in that case: an incognito window inherits the global
  // policy but cannot accidentally persist or replay a normal-profile
  // exception.
  if (is_off_the_record || !prefs.FindPreference(kOriginModesPref)) {
    return policy;
  }

  for (const auto [origin, value] : prefs.GetDict(kOriginModesPref)) {
    const auto* mode_value = value.GetIfString();
    if (!mode_value) {
      continue;
    }
    const GURL origin_url(origin);
    if (!IsHttpOrHttps(origin_url) ||
        url::Origin::Create(origin_url).Serialize() != origin) {
      continue;
    }
    policy.origin_modes.emplace(origin, PrivacyModeFromPrefValue(*mode_value));
  }
  return policy;
}

PrivacyMode GetGlobalMode(const PrefService& prefs) {
  if (!prefs.FindPreference(kGlobalModePref)) {
    return PrivacyMode::kChromiumCompatible;
  }
  return PrivacyModeFromPrefValue(prefs.GetString(kGlobalModePref));
}

PrivacyMode GetModeForUrl(const PrefService& prefs,
                          const GURL& url,
                          bool is_off_the_record) {
  return GetPolicySnapshot(prefs, is_off_the_record).ModeForUrl(url);
}

bool SetGlobalMode(PrefService* prefs, PrivacyMode mode) {
  if (!prefs || !prefs->FindPreference(kGlobalModePref) ||
      prefs->IsManagedPreference(kGlobalModePref)) {
    return false;
  }
  prefs->SetString(kGlobalModePref, PrivacyModeToPrefValue(mode));
  return true;
}

bool SetOriginMode(PrefService* prefs,
                   const GURL& url,
                   std::optional<PrivacyMode> mode,
                   bool is_off_the_record) {
  if (!prefs || is_off_the_record || !IsHttpOrHttps(url) ||
      !prefs->FindPreference(kOriginModesPref) ||
      prefs->IsManagedPreference(kOriginModesPref)) {
    return false;
  }
  const std::string key = OriginKey(url);
  base::DictValue overrides = prefs->GetDict(kOriginModesPref).Clone();
  if (!mode) {
    overrides.Remove(key);
  } else {
    overrides.Set(key, PrivacyModeToPrefValue(*mode));
  }
  prefs->SetDict(kOriginModesPref, std::move(overrides));
  return true;
}

bool IsGlobalModeManaged(const PrefService& prefs) {
  return prefs.FindPreference(kGlobalModePref) &&
         prefs.IsManagedPreference(kGlobalModePref);
}

CookieEnforcementPolicy BuildCookieEnforcementPolicy(const PrefService& prefs,
                                                     bool is_off_the_record) {
  CookieEnforcementPolicy enforcement;
  if (!prefs.FindPreference(kGlobalModePref)) {
    return enforcement;
  }

  // Enterprise cookie policy remains authoritative. In particular, an Ahoi
  // compatibility exception must never weaken a managed 3PC decision.
  if (prefs.FindPreference(prefs::kCookieControlsMode) &&
      prefs.IsManagedPreference(prefs::kCookieControlsMode)) {
    return enforcement;
  }

  const PrivacyPolicy policy = GetPolicySnapshot(prefs, is_off_the_record);
  enforcement.block_third_party_cookies =
      policy.global_mode == PrivacyMode::kStrict;
  for (const auto& [origin, mode] : policy.origin_modes) {
    enforcement.top_frame_overrides.emplace(origin,
                                            mode == PrivacyMode::kStrict);
  }
  return enforcement;
}

GURL StripKnownTrackingParameters(const GURL& url) {
  if (!IsHttpOrHttps(url) || !url.has_query()) {
    return url;
  }

  std::vector<std::string_view> kept;
  for (std::string_view parameter : base::SplitStringPiece(
           url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const size_t equals = parameter.find('=');
    const std::string_view key = parameter.substr(0, equals);
    if (kTrackingParameters.contains(base::ToLowerASCII(key))) {
      continue;
    }
    kept.push_back(parameter);
  }
  if (kept.size() == base::SplitStringPiece(url.query(), "&",
                                            base::KEEP_WHITESPACE,
                                            base::SPLIT_WANT_ALL)
                         .size()) {
    return url;
  }
  GURL::Replacements replacements;
  const std::string query = base::JoinString(kept, "&");
  replacements.SetQueryStr(query);
  return url.ReplaceComponents(replacements);
}

}  // namespace ahoi::privacy
