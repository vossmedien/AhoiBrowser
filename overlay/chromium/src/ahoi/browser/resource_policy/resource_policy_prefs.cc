// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/resource_policy/resource_policy_prefs.h"

#include "base/values.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_registry_simple.h"

namespace ahoi::resource_policy {

void ApplyLocalStateDefaults(PrefRegistrySimple* registry) {
  if (!registry) {
    return;
  }
  using performance_manager::user_tuning::prefs::MemorySaverModeAggressiveness;
  using performance_manager::user_tuning::prefs::MemorySaverModeState;
  registry->SetDefaultPrefValue(
      performance_manager::user_tuning::prefs::kMemorySaverModeState,
      base::Value(static_cast<int>(MemorySaverModeState::kEnabled)));
  registry->SetDefaultPrefValue(
      performance_manager::user_tuning::prefs::kMemorySaverModeAggressiveness,
      base::Value(static_cast<int>(MemorySaverModeAggressiveness::kMedium)));
}

}  // namespace ahoi::resource_policy
