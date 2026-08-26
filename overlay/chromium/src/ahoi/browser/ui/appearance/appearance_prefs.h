// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_PREFS_H_
#define AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_PREFS_H_

#include "base/time/time.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi::appearance {

// Theme mode and the global primary color deliberately remain Chromium-owned:
// ThemeService::BrowserColorScheme and ThemeService's user-color theme are the
// single palette inputs for every semantic ColorProvider role. These Ahoi prefs
// contain only product-surface behavior that Chromium does not already own.
inline constexpr char kGlassEnabledPref[] = "ahoi.appearance.glass_enabled";
inline constexpr char kFloatingNavigationAutoHideEnabledPref[] =
    "ahoi.navigation.floating_auto_hide_enabled";
inline constexpr char kFloatingNavigationRevealNotchEnabledPref[] =
    "ahoi.navigation.floating_reveal_notch_enabled";
inline constexpr char kFloatingNavigationAutoHideDelayMsPref[] =
    "ahoi.navigation.floating_auto_hide_delay_ms";

inline constexpr int kDefaultFloatingNavigationAutoHideDelayMs = 650;
inline constexpr int kMinimumFloatingNavigationAutoHideDelayMs = 100;
inline constexpr int kMaximumFloatingNavigationAutoHideDelayMs = 10000;

struct FloatingNavigationPreferences {
  bool auto_hide_enabled = true;
  bool reveal_notch_enabled = true;
  base::TimeDelta auto_hide_delay =
      base::Milliseconds(kDefaultFloatingNavigationAutoHideDelayMs);
};

constexpr bool operator==(const FloatingNavigationPreferences& lhs,
                          const FloatingNavigationPreferences& rhs) {
  return lhs.auto_hide_enabled == rhs.auto_hide_enabled &&
         lhs.reveal_notch_enabled == rhs.reveal_notch_enabled &&
         lhs.auto_hide_delay == rhs.auto_hide_delay;
}

constexpr bool operator!=(const FloatingNavigationPreferences& lhs,
                          const FloatingNavigationPreferences& rhs) {
  return !(lhs == rhs);
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

bool IsGlassEnabled(const PrefService& prefs);
FloatingNavigationPreferences GetFloatingNavigationPreferences(
    const PrefService& prefs);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_PREFS_H_
