// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"

#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

namespace ahoi::developer_toolkit_prefs {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kToolkitEnabled, false);
  registry->RegisterBooleanPref(kShowCookieButton, false);
  registry->RegisterBooleanPref(kShowCacheButton, false);
  registry->RegisterBooleanPref(kShowToolkitButton, false);
  developer_profile_prefs::RegisterProfilePrefs(registry);
}

ToolbarVisibility GetToolbarVisibility(const PrefService& prefs) {
  if (!IsToolkitEnabled(prefs)) {
    return {};
  }
  ToolbarVisibility visibility{
      .cookie = prefs.GetBoolean(kShowCookieButton),
      .cache = prefs.GetBoolean(kShowCacheButton),
      .toolkit = prefs.GetBoolean(kShowToolkitButton),
  };
  return visibility;
}

bool IsToolkitEnabled(const PrefService& prefs) {
  return prefs.GetBoolean(kToolkitEnabled);
}

bool ActivateToolkit(PrefService& prefs) {
  prefs.SetBoolean(kToolkitEnabled, true);
  ToolbarVisibility visibility{
      .cookie = prefs.GetBoolean(kShowCookieButton),
      .cache = prefs.GetBoolean(kShowCacheButton),
      .toolkit = prefs.GetBoolean(kShowToolkitButton),
  };
  if (!visibility.any_visible()) {
    prefs.SetBoolean(kShowToolkitButton, true);
  }
  return true;
}

void SetToolkitEnabled(PrefService& prefs, bool enabled) {
  prefs.SetBoolean(kToolkitEnabled, enabled);
}

bool SetToolbarVisibility(PrefService& prefs, ToolbarVisibility visibility) {
  if (visibility.any_visible()) {
    prefs.SetBoolean(kToolkitEnabled, true);
  }
  prefs.SetBoolean(kShowCookieButton, visibility.cookie);
  prefs.SetBoolean(kShowCacheButton, visibility.cache);
  prefs.SetBoolean(kShowToolkitButton, visibility.toolkit);
  return true;
}

}  // namespace ahoi::developer_toolkit_prefs
