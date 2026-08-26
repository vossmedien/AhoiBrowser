// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_prefs.h"

#include <algorithm>

#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"

namespace ahoi::appearance {

namespace {

bool GlassEnabledByDefault() {
#if BUILDFLAG(IS_MAC)
  return base::mac::MacOSMajorVersion() >= 26;
#else
  return false;
#endif
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  // The persisted user preference defaults on for the native macOS 26 target.
  // Runtime availability remains independently gated by Chromium's native
  // GlassFrame feature and macOS version, so older/other platforms resolve to
  // the fully opaque fallback even if a migrated profile contains `true`.
  registry->RegisterBooleanPref(kGlassEnabledPref, GlassEnabledByDefault());
  registry->RegisterBooleanPref(kFloatingNavigationAutoHideEnabledPref, true);
  registry->RegisterBooleanPref(kFloatingNavigationRevealNotchEnabledPref,
                                true);
  registry->RegisterIntegerPref(kFloatingNavigationAutoHideDelayMsPref,
                                kDefaultFloatingNavigationAutoHideDelayMs);
}

bool IsGlassEnabled(const PrefService& prefs) {
  return prefs.FindPreference(kGlassEnabledPref) &&
         prefs.GetBoolean(kGlassEnabledPref);
}

FloatingNavigationPreferences GetFloatingNavigationPreferences(
    const PrefService& prefs) {
  FloatingNavigationPreferences result;
  if (prefs.FindPreference(kFloatingNavigationAutoHideEnabledPref)) {
    result.auto_hide_enabled =
        prefs.GetBoolean(kFloatingNavigationAutoHideEnabledPref);
  }
  if (prefs.FindPreference(kFloatingNavigationRevealNotchEnabledPref)) {
    result.reveal_notch_enabled =
        prefs.GetBoolean(kFloatingNavigationRevealNotchEnabledPref);
  }
  if (prefs.FindPreference(kFloatingNavigationAutoHideDelayMsPref)) {
    result.auto_hide_delay = base::Milliseconds(std::clamp(
        prefs.GetInteger(kFloatingNavigationAutoHideDelayMsPref),
        kMinimumFloatingNavigationAutoHideDelayMs,
        kMaximumFloatingNavigationAutoHideDelayMs));
  }
  return result;
}

}  // namespace ahoi::appearance
