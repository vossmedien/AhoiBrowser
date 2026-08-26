// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"

#include <array>

#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

namespace ahoi::developer_toolkit_prefs {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kToolkitEnabled, false);
  registry->RegisterBooleanPref(kShowCookieButton, false);
  registry->RegisterBooleanPref(kShowCacheButton, false);
  // Keep the master switch disabled on a fresh profile, but preselect one
  // discoverable entry so enabling it directly through chrome://settings
  // cannot leave the toolkit active with every address-bar action hidden.
  registry->RegisterBooleanPref(kShowToolkitButton, true);
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
  const PrefService::Preference* enabled_pref =
      prefs.FindPreference(kToolkitEnabled);
  if (enabled_pref && !enabled_pref->IsDefaultValue()) {
    return prefs.GetBoolean(kToolkitEnabled);
  }

  // Builds predating the explicit master switch persisted only the three
  // toolbar choices. Treat an existing user-controlled visible action as the
  // one-time activation signal so an update cannot silently remove a user's
  // developer tools. A fresh profile still has all four defaults and remains
  // disabled; an explicit master-switch choice always wins above.
  const auto has_legacy_visible_action = [&prefs](const char* pref_name) {
    const PrefService::Preference* pref = prefs.FindPreference(pref_name);
    return pref && !pref->IsDefaultValue() && prefs.GetBoolean(pref_name);
  };
  return has_legacy_visible_action(kShowCookieButton) ||
         has_legacy_visible_action(kShowCacheButton) ||
         has_legacy_visible_action(kShowToolkitButton);
}

void MigrateLegacyActivation(PrefService* prefs) {
  if (!prefs) {
    return;
  }
  const PrefService::Preference* enabled_pref =
      prefs->FindPreference(kToolkitEnabled);
  if (!enabled_pref || !enabled_pref->IsDefaultValue()) {
    return;
  }

  constexpr std::array<const char*, 3> kLegacyToolbarPrefs = {
      kShowCookieButton, kShowCacheButton, kShowToolkitButton};
  for (const char* pref_name : kLegacyToolbarPrefs) {
    const PrefService::Preference* pref = prefs->FindPreference(pref_name);
    if (pref && !pref->IsDefaultValue() && prefs->GetBoolean(pref_name)) {
      prefs->SetBoolean(kToolkitEnabled, true);
      return;
    }
  }
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
  if (enabled) {
    // A master-switch activation must always leave one recoverable entry. A
    // Settings surface can therefore call this API without having to duplicate
    // toolbar visibility policy.
    ActivateToolkit(prefs);
    return;
  }
  prefs.SetBoolean(kToolkitEnabled, false);
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
