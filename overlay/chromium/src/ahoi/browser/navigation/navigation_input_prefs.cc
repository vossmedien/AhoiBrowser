// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/navigation_input_prefs.h"

#include <algorithm>

#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"

namespace ahoi::navigation_input_prefs {

namespace {

constexpr double kMinimumSwipeThreshold = 24.0;
constexpr double kMaximumSwipeThreshold = 240.0;
constexpr double kMinimumAxisBias = 1.0;
constexpr double kMaximumAxisBias = 4.0;
constexpr double kMaximumVerticalRejectDistance = 160.0;
constexpr double kMinimumCmdScrollThreshold = 4.0;
constexpr double kMaximumCmdScrollThreshold = 240.0;
constexpr int kMaximumCmdScrollIntervalMs = 2000;

double ReadClampedDouble(const PrefService& prefs,
                         const char* path,
                         double fallback,
                         double minimum,
                         double maximum) {
  return prefs.FindPreference(path)
             ? std::clamp(prefs.GetDouble(path), minimum, maximum)
             : fallback;
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kWorkspaceSwipeEnabled, true);
  registry->RegisterBooleanPref(kWorkspaceSwipeReverseDirection, false);
  registry->RegisterDoublePref(kWorkspaceSwipeThreshold, 80.0);
  registry->RegisterDoublePref(kWorkspaceSwipeAxisBias, 1.3);
  registry->RegisterDoublePref(kWorkspaceSwipeRejectVerticalDistance, 24.0);
  registry->RegisterBooleanPref(kCmdScrollEnabled, true);
  registry->RegisterDoublePref(kCmdScrollThreshold, 24.0);
  registry->RegisterIntegerPref(kCmdScrollMinimumIntervalMs, 250);
  registry->RegisterBooleanPref(kMiddleClickAutoscrollEnabled, true);
}

WorkspaceSwipeSettings ReadWorkspaceSwipeSettings(const PrefService& prefs) {
  WorkspaceSwipeSettings settings;
  if (prefs.FindPreference(kWorkspaceSwipeEnabled)) {
    settings.enabled = prefs.GetBoolean(kWorkspaceSwipeEnabled);
  }
  if (prefs.FindPreference(kWorkspaceSwipeReverseDirection)) {
    settings.reverse_direction =
        prefs.GetBoolean(kWorkspaceSwipeReverseDirection);
  }
  settings.threshold = static_cast<float>(
      ReadClampedDouble(prefs, kWorkspaceSwipeThreshold, settings.threshold,
                        kMinimumSwipeThreshold, kMaximumSwipeThreshold));
  settings.axis_bias = static_cast<float>(
      ReadClampedDouble(prefs, kWorkspaceSwipeAxisBias, settings.axis_bias,
                        kMinimumAxisBias, kMaximumAxisBias));
  settings.reject_vertical_distance = static_cast<float>(ReadClampedDouble(
      prefs, kWorkspaceSwipeRejectVerticalDistance,
      settings.reject_vertical_distance, 0.0, kMaximumVerticalRejectDistance));
  return settings;
}

CmdScrollTabSettings ReadCmdScrollTabSettings(const PrefService& prefs) {
  CmdScrollTabSettings settings;
  if (prefs.FindPreference(kCmdScrollEnabled)) {
    settings.enabled = prefs.GetBoolean(kCmdScrollEnabled);
  }
  settings.threshold = static_cast<float>(ReadClampedDouble(
      prefs, kCmdScrollThreshold, settings.threshold,
      kMinimumCmdScrollThreshold, kMaximumCmdScrollThreshold));
  if (prefs.FindPreference(kCmdScrollMinimumIntervalMs)) {
    settings.minimum_interval = base::Milliseconds(
        std::clamp(prefs.GetInteger(kCmdScrollMinimumIntervalMs), 0,
                   kMaximumCmdScrollIntervalMs));
  }
  return settings;
}

bool IsMiddleClickAutoscrollEnabled(const PrefService& prefs) {
  return !prefs.FindPreference(kMiddleClickAutoscrollEnabled) ||
         prefs.GetBoolean(kMiddleClickAutoscrollEnabled);
}

}  // namespace ahoi::navigation_input_prefs
