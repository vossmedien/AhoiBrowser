// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"

#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"

namespace ahoi::sidebar {

namespace {

constexpr int kDefaultMode = static_cast<int>(SidebarPresentationMode::kDocked);

std::optional<SidebarPresentationMode> ModeFromInt(int value) {
  switch (value) {
    case static_cast<int>(SidebarPresentationMode::kDocked):
      return SidebarPresentationMode::kDocked;
    case static_cast<int>(SidebarPresentationMode::kFloating):
      return SidebarPresentationMode::kFloating;
    case static_cast<int>(SidebarPresentationMode::kHidden):
      return SidebarPresentationMode::kHidden;
  }
  return std::nullopt;
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(kSidebarPresentationModePref, kDefaultMode);
  registry->RegisterIntegerPref(kSidebarPresentationModeBeforeHiddenPref,
                                kDefaultMode);
  registry->RegisterBooleanPref(kSidebarMiniPlayerExpandedPref, false);
}

SidebarPresentationMode GetPresentationMode(const PrefService& prefs) {
  return ModeFromInt(prefs.GetInteger(kSidebarPresentationModePref))
      .value_or(SidebarPresentationMode::kDocked);
}

SidebarPresentationMode GetVisibleModeBeforeHidden(const PrefService& prefs) {
  const auto mode =
      ModeFromInt(prefs.GetInteger(kSidebarPresentationModeBeforeHiddenPref));
  return mode.value_or(SidebarPresentationMode::kDocked) ==
                 SidebarPresentationMode::kFloating
             ? SidebarPresentationMode::kFloating
             : SidebarPresentationMode::kDocked;
}

bool IsMiniPlayerExpanded(const PrefService& prefs) {
  return prefs.FindPreference(kSidebarMiniPlayerExpandedPref) &&
         prefs.GetBoolean(kSidebarMiniPlayerExpandedPref);
}

bool SetMiniPlayerExpanded(PrefService* prefs, bool expanded) {
  if (!prefs || !prefs->FindPreference(kSidebarMiniPlayerExpandedPref) ||
      prefs->IsManagedPreference(kSidebarMiniPlayerExpandedPref)) {
    return false;
  }
  prefs->SetBoolean(kSidebarMiniPlayerExpandedPref, expanded);
  return true;
}

bool SetPresentationMode(PrefService* prefs, SidebarPresentationMode mode) {
  if (!prefs || !prefs->FindPreference(kSidebarPresentationModePref) ||
      prefs->IsManagedPreference(kSidebarPresentationModePref) ||
      !ModeFromInt(static_cast<int>(mode)).has_value()) {
    return false;
  }

  const SidebarPresentationMode current = GetPresentationMode(*prefs);
  if (mode == SidebarPresentationMode::kHidden && IsVisible(current)) {
    prefs->SetInteger(kSidebarPresentationModeBeforeHiddenPref,
                      static_cast<int>(current));
  }
  prefs->SetInteger(kSidebarPresentationModePref, static_cast<int>(mode));
  return true;
}

bool IsVisible(SidebarPresentationMode mode) {
  return mode != SidebarPresentationMode::kHidden;
}

bool IsFloating(SidebarPresentationMode mode) {
  return mode == SidebarPresentationMode::kFloating;
}

SidebarLayoutPolicy GetLayoutPolicy(SidebarPresentationMode mode) {
  switch (mode) {
    case SidebarPresentationMode::kDocked:
      return {.visible = true,
              .reserve_viewport = true,
              .overlays_web_contents = false};
    case SidebarPresentationMode::kFloating:
      return {.visible = true,
              .reserve_viewport = false,
              .overlays_web_contents = true};
    case SidebarPresentationMode::kHidden:
      return {.visible = false,
              .reserve_viewport = false,
              .overlays_web_contents = false};
  }
  return {};
}

}  // namespace ahoi::sidebar
