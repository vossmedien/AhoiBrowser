// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_prefs.h"

#include <optional>
#include <string_view>

#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"

namespace ahoi::session {

namespace {

constexpr std::string_view kAskValue = "ask";
constexpr std::string_view kContinueValue = "continue";
constexpr std::string_view kEmptyValue = "empty";

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kStartupModePref, kAskValue);
}

std::string_view StartupModeToPrefValue(StartupMode mode) {
  switch (mode) {
    case StartupMode::kAsk:
      return kAskValue;
    case StartupMode::kContinue:
      return kContinueValue;
    case StartupMode::kEmpty:
      return kEmptyValue;
  }
  return {};
}

std::optional<StartupMode> StartupModeFromPrefValue(std::string_view value) {
  if (value == kAskValue) {
    return StartupMode::kAsk;
  }
  if (value == kContinueValue) {
    return StartupMode::kContinue;
  }
  if (value == kEmptyValue) {
    return StartupMode::kEmpty;
  }
  return std::nullopt;
}

StartupMode GetStartupMode(const PrefService& prefs) {
  if (!prefs.FindPreference(kStartupModePref)) {
    return StartupMode::kAsk;
  }
  return StartupModeFromPrefValue(prefs.GetString(kStartupModePref))
      .value_or(StartupMode::kAsk);
}

bool SetStartupMode(PrefService* prefs, StartupMode mode) {
  if (!prefs || !prefs->FindPreference(kStartupModePref) ||
      prefs->IsManagedPreference(kStartupModePref)) {
    return false;
  }
  const std::string_view value = StartupModeToPrefValue(mode);
  if (value.empty()) {
    return false;
  }
  prefs->SetString(kStartupModePref, value);
  return true;
}

bool IsStartupModeManaged(const PrefService& prefs) {
  return prefs.FindPreference(kStartupModePref) &&
         prefs.IsManagedPreference(kStartupModePref);
}

}  // namespace ahoi::session
