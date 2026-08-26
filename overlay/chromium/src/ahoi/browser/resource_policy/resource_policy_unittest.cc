// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/resource_policy/resource_policy_prefs.h"
#include "ahoi/browser/resource_policy/resource_policy_types.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::resource_policy {
namespace {

TEST(ResourcePolicyTypesTest, CriticalStatesHaveStablePrimaryReasons) {
  CriticalSignals signals;
  signals.active_pane = true;
  signals.download = true;
  EXPECT_EQ(SleepBlockReason::kActivePane, GetPrimaryBlockReason(signals));

  signals = {};
  signals.media_session = true;
  EXPECT_EQ(SleepBlockReason::kMediaSession, GetPrimaryBlockReason(signals));
  EXPECT_TRUE(HasAutomaticAhoiProtection(signals));

  signals = {};
  signals.permission_prompt = true;
  EXPECT_EQ(SleepBlockReason::kPermissionPrompt,
            GetPrimaryBlockReason(signals));
  EXPECT_TRUE(HasAutomaticAhoiProtection(signals));

  signals = {};
  signals.file_chooser = true;
  EXPECT_EQ(SleepBlockReason::kFileChooser, GetPrimaryBlockReason(signals));
  EXPECT_TRUE(HasAutomaticAhoiProtection(signals));
}

TEST(ResourcePolicyTypesTest, UpstreamOwnedSignalsDoNotNeedAhoiScheduler) {
  CriticalSignals signals;
  signals.active_pane = true;
  signals.audible = true;
  signals.picture_in_picture = true;
  signals.capture = true;
  signals.unsaved_form = true;
  signals.devtools = true;
  signals.enterprise_policy = true;
  EXPECT_FALSE(HasAutomaticAhoiProtection(signals));
}

TEST(ResourcePolicyTypesTest, ProductGapsUseLifecycleProtection) {
  CriticalSignals signals;
  signals.download = true;
  signals.upload = true;
  signals.before_unload = true;
  signals.http_auth = true;
  signals.permission_prompt = true;
  signals.modal_flow = true;
  EXPECT_TRUE(HasAutomaticAhoiProtection(signals));
}

TEST(ResourcePolicyPrefsTest, AhoiDefaultsToChromiumMemorySaverMedium) {
  TestingPrefServiceSimple local_state;
  performance_manager::user_tuning::prefs::RegisterLocalStatePrefs(
      local_state.registry());
  ApplyLocalStateDefaults(local_state.registry());

  EXPECT_EQ(
      static_cast<int>(performance_manager::user_tuning::prefs::
                           MemorySaverModeState::kEnabled),
      local_state.GetInteger(
          performance_manager::user_tuning::prefs::kMemorySaverModeState));
  EXPECT_EQ(static_cast<int>(performance_manager::user_tuning::prefs::
                                 MemorySaverModeAggressiveness::kMedium),
            local_state.GetInteger(performance_manager::user_tuning::prefs::
                                       kMemorySaverModeAggressiveness));
}

}  // namespace
}  // namespace ahoi::resource_policy
