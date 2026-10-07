// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_density.h"

#include "components/prefs/pref_service.h"

namespace ahoi::appearance {

SidebarDensity GetSidebarDensity(const PrefService& prefs) {
  return prefs.FindPreference(kSidebarDensityPref)
             ? NormalizeSidebarDensity(prefs.GetInteger(kSidebarDensityPref))
             : SidebarDensity::kStandard;
}

bool ResetSidebarDensity(PrefService& prefs) {
  const auto* preference = prefs.FindPreference(kSidebarDensityPref);
  if (!preference || !preference->IsUserModifiable()) {
    return false;
  }
  prefs.ClearPref(kSidebarDensityPref);
  return true;
}

}  // namespace ahoi::appearance
